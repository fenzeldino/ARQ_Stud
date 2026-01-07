#ifndef CLIENTSY_H
#define CLIENTSY_H

#include "data.h"

/*
 * ARQ-Client-API
 *
 * Diese Funktionen werden von der Anwendung (client.c) aufgerufen.
 * Die Implementierung in clientSy.c kapselt:
 *   - UDP-Transport (Socket, sendto/recvfrom)
 *   - ARQ-Protokoll (Fenster, Timer, Retransmits)
 */

/* UDP- und ARQ-Client initialisieren (Servername & Port) */
void initClient(char *name, const char *port);

/* UDP- und ARQ-Client schließen (Socket freigeben etc.) */
void closeClient(void);

/* Verbindungsaufbau: Hello senden, Antwort abwarten.
 * Rückgabewert: 0 bei Erfolg, !=0 bei Fehler.
 */
int arqSendHello(int winSize);

/* Eine app_unit zuverlässig zum Server übertragen.
 * Es darf pro Aufruf genau eine app_unit gesendet werden.
 * Rückgabewert: 0 bei Erfolg, !=0 bei Fehler.
 */
int arqSendData(const struct app_unit *app, int winSize);

/* Verbindung ordentlich schließen (Close/ACK).
 * Rückgabewert: 0 bei Erfolg, !=0 bei Fehler.
 */
int arqSendClose(int winSize);

#endif /* CLIENTSY_H */
