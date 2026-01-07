/* serverSy.c - UDP + ARQ/Go-Back-N-Server mit Loss-Simulation
 *
 * Schichten:
 *   - SAP-Schicht (UDP): initServer, getRequest, sendAnswer, exitServer
 *   - ARQ-Schicht: processRequest(), arqServerLoop()
 *
 * Die Anwendung (Datei öffnen/schreiben/schließen) wird über Callbacks
 * aus server.c angebunden:
 *   appStartFn  appStartTransfer
 *   appWriteFn  appWriteData
 *   appEndFn    appEndTransfer
 *
 * WICHTIG:
 *   - Dateiname und Funktionssignaturen in serverSy.h sollen
 *     unverändert beibehalten werden.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

/* Optionale globale Variablen:
 *   - Socket-Deskriptor
 *   - zuletzt bekannte Client-Adresse (für sendto)
 *   - nächst erwartete Sequenznummer (nextExpected)
 *   - Zeiger auf die Anwendungscallbacks
 */

/* --------------------------------------------------------------- */
/*  SAP-Schicht (UDP)                                              */
/* --------------------------------------------------------------- */
static int serverSock = -1;
static struct sockaddr_storage lastClientAddr;
static socklen_t lastClientAddrLen = 0;

static unsigned int nextExpected = 0;
static int initialized = 0;

static appStartFn g_appStart = NULL;
static appWriteFn g_appWrite = NULL;
static appEndFn g_appEnd = NULL;

int initServer(const char *port)
{
    (void)port;

    /* TODO:
     *  - mit getaddrinfo(NULL, port, ...) eine lokale Adresse für UDP/IPv6
     *    ermitteln
     *  - mit socket(...) einen UDP/IPv6-Socket erzeugen
     *  - mit bind(...) an die Adresse binden
     *  - Socket-Deskriptor in globaler Variable speichern
     *  - bei Erfolg 0, bei Fehler <0 zurückgeben
     */

     struct addrinfo hints,*res, *rp;
     int sfd = -1;
     int ret;
     int opt = 1;
     const char *use_port = port ? port : DEFAULT_PORT;

     memset(&hints,0,sizeof(hints)); //Struct hint 0
     hints.ai_family = DEFAULT_FAMILY;
     hints.ai_socktype = DEFAULT_SOCKTYPE; //UDP
     hints.ai_flags = AI_PASSIVE;

     ret = getaddrinfo(NULL,use_port,&hints,&res);
        if(ret != 0){
            fprintf(stderr,"initServer: getaddrinfo: %s\n",gai_strerror(ret));
            return -1;
        }

        for(rp = res;rp != NULL;rp = rp->ai_next){
            sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if(sfd == -1){
                continue;
            }
            (void)setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
            
            if(bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0){
                break;
            }
            close(sfd);
            sfd = -1;
        }
        freeaddrinfo(res);
        
        if(sfd == -1){
            fprintf(stderr,"initServer: unable to bind in port %s: %s\n",use_port,strerror(errno));
            return -1;
        }
        serverSock = sfd;
        return 0;
}

struct request *getRequest(void)
{
    static struct request req;

    /* TODO:
     *  - mit recvfrom(...) ein Request-Paket vom Socket lesen
     *  - die Adresse des Clients (struct sockaddr_storage) merken,
     *    damit sendAnswer() dorthin antworten kann
     *  - bei Erfolg &req zurückgeben
     *  - bei Fehler oder wenn keine Daten vorliegen: NULL zurückgeben
     */
    ssize_t n;

    if(serverSock < 0) return NULL; //Verhindert recvfrom() auf ungültige Socket

    lastClientAddrLen = sizeof(lastClientAddr);
    memset(&req,0,sizeof(req));

    n = recvfrom(serverSock,
                 &req,
                sizeof(req),
                0,
        (struct sockaddr *)&lastClientAddr,&lastClientAddrLen);

    if(n == -1){
        if(errno == EAGAIN || errno == EWOULDBLOCK) return NULL;
        perror("getRequest: recvfrom");
        return NULL;
    }

    return &req;
}

int sendAnswer(struct answer *answerPtr)
{
    (void)answerPtr;

    /* TODO:
     *  - mit sendto(...) eine Antwort an die zuletzt bekannte
     *    Client-Adresse schicken
     *  - bei Erfolg 0, bei Fehler <0 zurückgeben
     */

     ssize_t n;

     if(serverSock < 0){
        fprintf(stderr,"sendAnswer: server socket not initialized\n");
        return -1;
     }

     if(lastClientAddrLen == 0){
        fprintf(stderr,"sendAnswer: no client address known\n");
        return -1;
     }

     n = sendto(serverSock,
                answerPtr,
                sizeof(*answerPtr),
                0,
                (struct sockaddr *)&lastClientAddr,
                lastClientAddrLen);
    if(n == -1){
        perror("sendAnswer: sendto");
        return -1;
    }

    if((size_t)n != sizeof(*answerPtr)){
        fprintf(stderr,"sendAnswer: partial send(%zd bytes)\n",n);
        return -1;
    }

    return 0;
}

int exitServer(void)
{
    /* TODO:
     *  - Socket schließen (close)
     *  - globale Zustandsvariablen zurücksetzen
     */

    if(serverSock >= 0){
        if(close(serverSock) == -1){
            perror("exitServer: close");
            serverSock = -1;
            lastClientAddrLen = 0;
            memset(&lastClientAddr, 0, sizeof(lastClientAddr));
            return -1;
        }
        serverSock = -1;
    }
    lastClientAddrLen = 0;
    memset(&lastClientAddr, 0 , sizeof(lastClientAddr));
    return 0;
}

/* --------------------------------------------------------------- */
/*  ARQ-/GBN-Logik (Empfänger)                                     */
/* --------------------------------------------------------------- */

/*
 * processRequest:
 *  - nimmt ein Request-Paket entgegen
 *  - führt die ARQ-/GBN-Empfangslogik aus
 *  - erzeugt eine passende Antwort (ACK/Fehler)
 *
 *   ReqHello:
 *     - Sequenznummernzustand initialisieren (z.B. nextExpected = 0)
 *     - Anwendung per appStartFn informieren
 *     - eine passende Antwort (AnswHello/AnswOk) eintragen
 *
 *   ReqData:
 *         * ggf. Nutzdaten an appWriteFn übergeben
 *         * (kumulatives) ACK senden 
 *       
 *   ReqClose:
 *     - appEndFn aufrufen
 *     - Abschluss-ACK senden
 *
 * lossReq:
 *   - simulierte Paketverlustrate für Requests (0.0..1.0)
 *     (z.B. über Zufallszahlvergleich ein Paket "fallen lassen")
 *
 * Rückgabewert:
 *   - Zeiger auf ausgefüllte Antwortstruktur (answPtr)
 *   - NULL, wenn das Request-Paket vollständig verworfen wurde
 */
static struct answer *processRequest(struct request *reqPtr,
                                     struct answer *answPtr,
                                     double lossReq)
{
    (void)reqPtr;
    (void)answPtr;
    (void)lossReq;
    int writeRet;
    double r;
    if(reqPtr == NULL || answPtr == NULL) return NULL;

    //Verlustsimulation

    r = (double)rand() / (double)RAND_MAX;
    if(r < lossReq){
        return NULL; //Paket wird verworfen
    }

    //Default-Antwort intitialisieren
    memset(answPtr,0,sizeof(*answPtr));

    switch (reqPtr->ReqType)
    {
    case ReqHello:
    //Neustart / Initialisierung
        
        if (g_appStart)
            (void)g_appStart();

        /* Das Hello-Paket selbst ist die Nummer 0. 
         * Nach erfolgreichem Hello erwarten wir als nächstes Paket 1. */
        nextExpected = 1;

        answPtr->AnswType = AnswHello;
        answPtr->SeNo = 1; /* Wir bestätigen 0 und erwarten 1 */
        break;

    case ReqData:
       if (reqPtr->SeNr == nextExpected) {
            /* In-order: an Anwendung weitergeben */
            if (g_appWrite) {
                writeRet = g_appWrite(reqPtr->name, reqPtr->FlNr);
                if (writeRet < 0) {
                    /* Anwendungsfehler -> Warnung/Err zurückgeben */
                    answPtr->AnswType = AnswWarn;
                    answPtr->SeNo = ERR_FILE_ERROR;
                    break;
                }
            }
            nextExpected++;
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = nextExpected; /* kumulatives ACK = nextExpected */
        } else {
            /* Duplikat / out-of-order: ACK für bereits empfangenes (kumulativ) */
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = nextExpected;
        }
        break;

    case ReqClose:
        /* Sitzung beenden */
        if (g_appEnd)
            g_appEnd();
        answPtr->AnswType = AnswOk;
        answPtr->SeNo = nextExpected;
        nextExpected = 0;
        printf("Server: Transfer beendet, Datei geschlossen.\n");
        break;
    default:
    /* unbekannter Request-Typ -> Fehler */
        answPtr->AnswType = AnswErr;
        answPtr->ErrNo = 1;
        break;
    }
    
    /* TODO:
     *  - Paketverlust über lossReq simulieren
     *  - je nach ReqType (ReqHello / ReqData / ReqClose) handeln
     *  - Antwort (AnswType, SeNo/ErrNo) setzen
     *  - bei verworfenem Request NULL zurückgeben
     */
    return answPtr; 
}

/* --------------------------------------------------------------- */
/*  ARQ-Server-Hauptschleife                                       */
/* --------------------------------------------------------------- */

int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd)
{
    (void)port;
    (void)lossReq;
    (void)lossAck;
    (void)appStart;
    (void)appWrite;
    (void)appEnd;

    /* TODO:
     *  - Callbacks in globalen Variablen speichern oder für Weiterreichen 
	 *    Parameterliste in processRequest erweitern
     *  - initServer(port) aufrufen
     *  - Sequenznummerzustand initialisieren (z.B. nextExpected = 0)
     *  - Endlosschleife:
	 *		 * Paketverlust mit lossReq simulieren
     *       * ggf. getRequest() aufrufen
     *       * processRequest(..) aufrufen
     *       * ACK-Verlust mit lossAck simulieren
     *       * bei "kein ACK-Verlust": sendAnswer(..) aufrufen
     *  - exitServer() im Fehlerfall oder bei Beenden verwenden
     */

    struct request *req;
    struct answer answ;
    struct answer *resp;
    double r;

    /* callbacks speichern (können in processRequest verwendet werden) */
    g_appStart = appStart;
    g_appWrite = appWrite;
    g_appEnd = appEnd;

   

    if (initServer(port) < 0) {
        return -1;
    }

    nextExpected = 0;

    for (;;) {
        req = getRequest();
        if (req == NULL) {
            /* keine Daten oder Fehler (getRequest loggt Fehler) */
            continue;
        }

        /* Request verarbeiten (kann NULL zurückgeben = verworfen) */
        resp = processRequest(req, &answ, lossReq);
        if (resp == NULL) {
            /* Request wurde simuliert verworfen -> weiter warten */
            continue;
        }

        if (sendAnswer(&answ) < 0) {
            /* schwerer Fehler beim Senden -> beenden */
            exitServer();
            return -1;
        }
    }


    return -1;
}
