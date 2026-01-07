#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

/* ============================================================
 * Globale Zustände (UDP + GBN)
 * ============================================================ */

static int gSock = -1;

static struct sockaddr_storage gServerAddr;
static socklen_t gServerAddrLen = 0;

/* GBN Sendefenster */
static unsigned long gBase = 0;        /* kleinste unbestätigte Seq */
static unsigned long gNext = 0;        /* nächste neue Seq (zu vergeben) */
static int           gCount = 0;       /* # unbestätigte Pakete im Fenster */

static struct request gBuf[GBN_BUFFER_SIZE];   /* Ringpuffer für Requests */
static unsigned long  gLastSendTick[GBN_BUFFER_SIZE]; /* "Zeit" der letzten Sendung je Paket */

/* "Zeit" in Intervallen (jedes doRequest = 1 Intervall) */
static unsigned long gTick = 0;

/* Retransmit-Mode: Go-Back-N resend ab base bis next-1 (1 Paket pro Intervall) */
static int           gRetransmitActive = 0;
static unsigned long gRetransmitPos    = 0;

/* statischer Antwortpuffer */
static struct answer gLastAnswer;

/* ============================================================
 * Hilfsfunktionen
 * ============================================================ */

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static int send_request(const struct request *req)
{
    ssize_t n = sendto(gSock,
                       req,
                       sizeof(*req),
                       0,
                       (const struct sockaddr *)&gServerAddr,
                       gServerAddrLen);
    if (n < 0) return -1;
    if ((size_t)n != sizeof(*req)) return -1;
    return 0;
}

static struct answer *recv_answer_if_any(void)
{
    struct sockaddr_storage src;
    socklen_t srclen = sizeof(src);

    ssize_t n = recvfrom(gSock, &gLastAnswer, sizeof(gLastAnswer), 0,
                         (struct sockaddr *)&src, &srclen);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) return NULL;
        return NULL;
    }
    if ((size_t)n < sizeof(gLastAnswer)) {
        return NULL;
    }
    return &gLastAnswer;
}

/* Fenster nach kumulativem ACK verschieben:
 * ACK bedeutet: alle SeNr < ackNo sind korrekt angekommen.
 */
static void slide_window(unsigned long ackNo)
{
    while (gCount > 0 && gBase < ackNo) {
        int idx = (int)(gBase % GBN_BUFFER_SIZE);
        /* Slot "freigeben" ist implizit – wir überschreiben später */
        gLastSendTick[idx] = 0;
        gBase++;
        gCount--;
    }

    /* Wenn Retransmit lief und base nach vorn ging: ggf. abbrechen */
    if (gRetransmitActive && gBase >= gNext) {
        gRetransmitActive = 0;
    }
    if (gRetransmitActive && gRetransmitPos < gBase) {
        gRetransmitPos = gBase;
    }
}

/* ============================================================
 * init / close
 * ============================================================ */

void initClient(char *name, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = PF_INET6;     /* i.d.R. PF_INET6 */
    hints.ai_socktype = SOCK_DGRAM;   /* UDP */
    hints.ai_protocol = 0;

    const char *host = name;
    if (host == NULL) host = DEFAULT_LOOPBACK_HOST;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0 || !res) {
        fprintf(stderr, "initClient: getaddrinfo failed: %s\n", gai_strerror(rc));
        exit(EXIT_FAILURE);
    }

    gSock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (gSock < 0) {
        perror("initClient: socket");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    if (set_nonblocking(gSock) < 0) {
        perror("initClient: fcntl(O_NONBLOCK)");
        freeaddrinfo(res);
        close(gSock);
        gSock = -1;
        exit(EXIT_FAILURE);
    }

    memset(&gServerAddr, 0, sizeof(gServerAddr));
    memcpy(&gServerAddr, res->ai_addr, res->ai_addrlen);
    gServerAddrLen = (socklen_t)res->ai_addrlen;

    freeaddrinfo(res);

    /* GBN State reset */
    gBase = 0;
    gNext = 0;
    gCount = 0;
    gTick = 0;
    gRetransmitActive = 0;
    gRetransmitPos = 0;
    memset(gLastSendTick, 0, sizeof(gLastSendTick));
    memset(gBuf, 0, sizeof(gBuf));
}

void closeClient(void)
{
    if (gSock >= 0) {
        close(gSock);
        gSock = -1;
    }
    gServerAddrLen = 0;

    gBase = 0;
    gNext = 0;
    gCount = 0;
    gTick = 0;
    gRetransmitActive = 0;
    gRetransmitPos = 0;
    memset(gLastSendTick, 0, sizeof(gLastSendTick));
    memset(gBuf, 0, sizeof(gBuf));
}

/* ============================================================
 * doRequest: 1 Intervall (max 1 Send) + Empfang/ACK Auswertung
 * ============================================================ */

static struct answer *doRequest(struct request *req, int winSize, int *windowFull, int *retransmission)
{
    if (windowFull) *windowFull = 0;
    if (retransmission) *retransmission = 0;

    if (winSize < 1) winSize = 1;
    if (winSize > GBN_MAX_WINDOW) winSize = GBN_MAX_WINDOW;

    /* Intervall-Tick */
    gTick++;

    /* 1) Timeout prüfen -> Retransmit starten */
    if (gCount > 0) {
        int baseIdx = (int)(gBase % GBN_BUFFER_SIZE);
        unsigned long last = gLastSendTick[baseIdx];
        if (last > 0 && (gTick - last) >= (unsigned long)GBN_TIMEOUT_UNITS) {
            gRetransmitActive = 1;
            gRetransmitPos = gBase;
            if (retransmission) *retransmission = 1;
        }
    }

    /* 2) Entscheiden: welches Paket senden (max. 1 pro Intervall) */
    int sentSomething = 0;

    if (gRetransmitActive && gCount > 0) {
        /* Go-Back-N: resend ab gRetransmitPos bis gNext-1 */
        if (gRetransmitPos < gNext) {
            int idx = (int)(gRetransmitPos % GBN_BUFFER_SIZE);
            if (send_request(&gBuf[idx]) == 0) {
                gLastSendTick[idx] = gTick;
                sentSomething = 1;
            }
            gRetransmitPos++;
            if (gRetransmitPos >= gNext) {
                gRetransmitActive = 0;
            }
        } else {
            gRetransmitActive = 0;
        }
    }
    else if (req != NULL) {
        /* Neues Paket aufnehmen und senden, falls Fenster Platz hat */
        if (gCount >= winSize) {
            if (windowFull) *windowFull = 1;
        } else {
            /* req->SeNr muss == gNext sein (Wrapper setzt das) */
            int idx = (int)(req->SeNr % GBN_BUFFER_SIZE);

            /* in Ringpuffer kopieren */
            gBuf[idx] = *req;

            /* sofort senden */
            if (send_request(&gBuf[idx]) == 0) {
                gLastSendTick[idx] = gTick;
                sentSomething = 1;
            } else {
                /* Senden fehlgeschlagen: wir lassen es im Buffer, nächste Intervalle retry/timeout */
                gLastSendTick[idx] = gTick;
            }

            gNext++;
            gCount++;
        }
    }

    (void)sentSomething; /* nur für Debug interessant */

    /* 3) Warten/Empfangen in diesem Intervall (select) */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(gSock, &rfds);

    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = (int)GBN_TIMEOUT_INT_MS * 1000;

    int rc = select(gSock + 1, &rfds, NULL, NULL, &tv);
    if (rc > 0 && FD_ISSET(gSock, &rfds)) {
        struct answer *a = recv_answer_if_any();
        if (!a) return NULL;

        /* ACK auswerten */
        if (a->AnswType == AnswOk) {
            unsigned long ackNo = a->SeNo; /* kumulatives ACK: next expected */
            if (ackNo >= gBase && ackNo <= gNext) {
                slide_window(ackNo);
            }
        }
        return a;
    }

    /* kein relevantes Paket in diesem Intervall */
    return NULL;
}

/* ============================================================
 * API Wrapper (blockierend bis Erfolg/Fehler)
 * ============================================================ */

int arqSendHello(int winSize)
{
    /* Zustand neu starten */
    gBase = 0;
    gNext = 0;
    gCount = 0;
    gTick  = 0;
    gRetransmitActive = 0;
    gRetransmitPos = 0;
    memset(gLastSendTick, 0, sizeof(gLastSendTick));

    struct request req;
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqHello;
    req.FlNr    = 0;
    req.SeNr    = 0;

    /* Hello: so lange senden bis AnswHello/AnswOk kommt oder zu viele Versuche */
    const int maxTries = 50; /* 50 * 100ms = 5s */
    for (int i = 0; i < maxTries; i++) {
        int wf = 0, rt = 0;
        struct answer *a = doRequest(&req, winSize, &wf, &rt);
        (void)wf; (void)rt;

        if (a) {
            if (a->AnswType == AnswHello || a->AnswType == AnswOk) {
                return 0;
            }
            if (a->AnswType == AnswErr) {
                return -1;
            }
        }
        usleep(100000);
        /* wenn keine Antwort: nächste Runde -> erneutes Hello (ggf. Retransmit-Mechanismus greift) */
    }
    return -1;
}

int arqSendData(const struct app_unit *app, int winSize)
{
    if (!app) return -1;

    /* Request vorbereiten */
    struct request req;
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqData;

    unsigned long len = app->len;
    if (len > (unsigned long)BufferSize) len = (unsigned long)BufferSize;

    req.FlNr = len;
    req.SeNr = gNext; /* nächste Sequenznummer */
    memcpy(req.name, app->data, len);

    unsigned long mySeq = req.SeNr;

    /* blockierend, bis unser Paket bestätigt wurde */
    const int maxLoops = 20000; /* Sicherheitslimit */
    for (int i = 0; i < maxLoops; i++) {
        int wf = 0, rt = 0;

        /* Nur beim ersten Mal "req" als neues Paket anbieten.
           Danach req=NULL, damit doRequest nicht wieder ein neues Paket einreiht. */
        struct request *toSend = (i == 0) ? &req : NULL;

        struct answer *a = doRequest(toSend, winSize, &wf, &rt);

        /* Falls Fenster voll wäre (kommt bei sequenzieller Nutzung selten): weiter iterieren */
        if (wf) continue;

        if (a) {
            if (a->AnswType == AnswErr) {
                return -1;
            }
            if (a->AnswType == AnswOk) {
                /* Unser Paket ist geliefert, wenn ackNo >= mySeq+1 */
                if (a->SeNo >= (mySeq + 1)) {
                    return 0;
                }
            }
            /* Warn behandeln wir wie "weiter versuchen" */
        }

        /* keine Antwort -> Schleife läuft weiter, Timeouts triggern Retransmits */
    }

    return -1;
}

int arqSendClose(int winSize)
{
    struct request req;
    memset(&req, 0, sizeof(req));
    req.ReqType = ReqClose;
    req.FlNr    = 0;
    req.SeNr    = gNext; /* Close bekommt auch eine Seq */

    unsigned long mySeq = req.SeNr;

    const int maxLoops = 20000;
    for (int i = 0; i < maxLoops; i++) {
        int wf = 0, rt = 0;

        struct request *toSend = (i == 0) ? &req : NULL;
        struct answer *a = doRequest(toSend, winSize, &wf, &rt);

        if (wf) continue;

        if (a) {
            if (a->AnswType == AnswErr) return -1;

            if (a->AnswType == AnswOk || a->AnswType == AnswHello) {
                if (a->SeNo >= (mySeq + 1)) {
                    return 0;
                }
            }
        }
    }
    return -1;
}
