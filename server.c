#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

/* Anwendungszustand: Ausgabedatei */
static const char* gOutputFile = NULL;
static FILE* gFp = NULL;
static int         gFileOk = 0;

static void usage(const char* progName)
{
    fprintf(stderr, "Usage: %s -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>]\n",
        progName);
    fprintf(stderr, "   -p <port>    : Server-Port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "   -f <outfile> : Ausgabedatei\n");
    fprintf(stderr, "   -r <lossReq> : Request-Verlustwahrscheinlichkeit (0.0..1.0)\n");
    fprintf(stderr, "   -a <lossAck> : ACK-Verlustwahrscheinlichkeit (0.0..1.0)\n");
    exit(EXIT_FAILURE);
}

/* Anwendungscallbacks für die ARQ-Schicht */

/* Ausgabedatei öffnen/neu anlegen. */
static int appStartTransfer(void)
{
    gFileOk = 0;

    if (!gOutputFile) {
        fprintf(stderr, "Server: no output file specified.\n");
        return -1;
    }

    /* TODO:
     *   - Datei gOutputFile zum Schreiben öffnen
     *   - Zeiger in gFp ablegen
     *   - bei Erfolg gFileOk = 1 setzen
     *   - bei Fehler Fehlermeldung ausgeben und <0 zurückgeben
     */

    if (gFp) {
        fclose(gFp);
        gFp = NULL;
    }

    gFp = fopen(gOutputFile, "wb");
    if (!gFp) {
        fprintf(stderr, "Server: cannot open output file '%s': %s\n",
            gOutputFile, strerror(errno));
        gFileOk = 0;
        return -1;
    }

    gFileOk = 1;

    printf("Server: start transfer -> writing to '%s'\n", gOutputFile);
    return 0;
}

/* Nutzdaten in Datei schreiben. */
static int appWriteData(const char* buf, unsigned long len)
{
    /* TODO:
     *   - prüfen, ob gFileOk und gFp gültig sind
     *   - len Bytes aus buf in gFp schreiben (fwrite)
     *   - bei Fehler <0 zurückgeben
     *   - bei Erfolg 0 zurückgeben
     */

    if (!gFileOk || !gFp) {
        fprintf(stderr, "Server: write failed (file not open)\n");
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    size_t written = fwrite(buf, 1, (size_t)len, gFp);
    if (written != (size_t)len) {
        fprintf(stderr, "Server: fwrite failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/* Datei schließen. */
static void appEndTransfer(void)
{
    /* TODO:
     *   - falls gFp != NULL: Datei schließen (fclose)
     *   - gFp auf NULL setzen
     *   - gFileOk zurücksetzen
     */

    if (gFp != NULL) {
        fclose(gFp);
        gFp = NULL;
    }
    gFileOk = 0;
}

/* --- main: Argumente auswerten, ARQ-Schicht starten --- */

int main(int argc, char* argv[])
{
    const char* port = DEFAULT_PORT;
    double lossReq = 0.0;
    double lossAck = 0.0;
    long i;

    /* Programmargumente auswerten */
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                case 'p': /* Server-Port */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f': /* Ausgabedatei */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        gOutputFile = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'r': /* Request-Verlust */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossReq = atof(argv[++i]);
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'a': /* ACK-Verlust */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossAck = atof(argv[++i]);
                        break;
                    }
                    usage(argv[0]);
                    break;

                default:
                    usage(argv[0]);
                    break;
                }
            }
            else {
                usage(argv[0]);
            }
        }
    }

    if (!gOutputFile) {
        usage(argv[0]);
    }

    printf("Server: listening on port %s\n", port);
    printf("Server: lossReq = %f, lossAck = %f\n", lossReq, lossAck);

    if (arqServerLoop(port, lossReq, lossAck,
        appStartTransfer, appWriteData, appEndTransfer) < 0) {
        fprintf(stderr, "Server: arqServerLoop failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

