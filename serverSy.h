#ifndef SERVERSY_H_INCLUDED
#define SERVERSY_H_INCLUDED

#include "data.h"

/*
 * Anwendungscallbacks:
 * Die ARQ-Schicht ruft diese Funktionen auf, um empfangene Daten
 * an die Anwendung (server.c) zu übergeben.
 */

typedef int  (*appStartFn)(void);
/* Start eines Transfers (z.B. Ausgabedatei öffnen).
 * Rückgabewert: 0 bei Erfolg, <0 bei Fehler.
 */

typedef int  (*appWriteFn)(const char *buf, unsigned long len);
/* Nutzdaten in die Anwendung schreiben (z.B. in Datei).
 * Rückgabewert: 0 bei Erfolg, <0 bei Fehler.
 */

typedef void (*appEndFn)(void);
/* Transferende (z.B. Datei schließen). */


/*
 * SAP-Funktionen – UDP-Schicht:
 * Diese Funktionen kapseln Socket-Erzeugung, recvfrom/sendto, close.
 * Die Signaturen sind vorgegeben und sollen beibehalten werden.
 */

int initServer(const char *port);
struct request *getRequest(void);
int sendAnswer(struct answer *answerPtr);
int exitServer(void);

/*
 * ARQ-Server-Hauptschleife:
 *   - empfängt Requests über UDP
 *   - führt ARQ-Logik aus
 *   - ruft bei in-Order empfangenen Datenpaketen die Callbacks auf.
 *
 * lossReq / lossAck: Paket- und ACK-Verlustwahrscheinlichkeit (0.0–1.0)
 * appStart/appWrite/appEnd: Anwendungscallbacks.
 * Die Signatur ist vorgegeben und soll beibehalten werden.
 */

int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd);

#endif /* SERVERSY_H_INCLUDED */
