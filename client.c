#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

/* ==========================================
 * Schritt 1: Usage-Funktion zur Kommandozeilen-Argumentbehandlung
 * ========================================== */

static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -a <server> -p <port> -f <file> -w <window>\n", progName);
    fprintf(stderr, "       -a <server> : Server-Adresse (Default: %s)\n",
            (DEFAULT_SERVER == NULL) ? "loopback" : DEFAULT_SERVER);
    fprintf(stderr, "       -p <port>   : Server-Port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "       -f <file>   : Eingabedatei\n");
    fprintf(stderr, "       -w <window> : Fenstergröße (1..10)\n");
    exit(EXIT_FAILURE);
}

/* ==========================================
 * Schritt 2: Kommandozeilen-Argumente verarbeiten
 * ========================================== */

int main(int argc, char *argv[])
{
    const char *server = DEFAULT_SERVER;
    const char *filename = NULL;
    const char *port = DEFAULT_PORT;
    const char *windowSize = "1";

    FILE *fp = NULL;
    long i;

    // Kommandozeilenargumente auswerten
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) && (argv[i][1] != 0) && (argv[i][2] == 0)) {
                switch (tolower((unsigned char)argv[i][1])) {
                    case 'a': /* Server-Adresse */
                        if (argv[i + 1] && argv[i + 1][0] != '-') {
                            server = argv[++i];
                            break;
                        }
                        usage(argv[0]);
                    case 'p': /* Server-Port */
                        if (argv[i + 1] && argv[i + 1][0] != '-') {
                            port = argv[++i];
                            break;
                        }
                        usage(argv[0]);
                    case 'f': /* Eingabedatei */
                        if (argv[i + 1] && argv[i + 1][0] != '-') {
                            filename = argv[++i];
                            break;
                        }
                        usage(argv[0]);
                    case 'w': /* Fenstergröße */
                        if (argv[i + 1] && argv[i + 1][0] != '-') {
                            windowSize = argv[++i];
                            break;
                        }
                        usage(argv[0]);
                    default:
                        usage(argv[0]);
                }
            } else {
                usage(argv[0]);
            }
        }
    }

    if (!filename) {
        usage(argv[0]);
    }

/* ==========================================
 * Schritt 3: Datei öffnen und Fehlerbehandlung
 * ========================================== */

    fp = fopen(filename, "r");
    if (!fp) {
        perror("File opening failed");
        return EXIT_FAILURE;
    }
    printf("Client: sending file '%s'\n", filename);

/* ==========================================
 * Schritt 4: ARQ-Client initialisieren und Hello-Nachricht senden
 * ========================================== */

    initClient((char *)server, port);

    if (arqSendHello(atoi(windowSize)) != 0) {
        fprintf(stderr, "Client: Hello failed, aborting.\n");
        fclose(fp);
        closeClient();
        return EXIT_FAILURE;
    }

/* ==========================================
 * Schritt 5: Datei Zeile für Zeile senden
 * ========================================== */

    struct app_unit app;
    while (fgets(app.data, BufferSize, fp)) {
        app.len = strlen(app.data);

        if (arqSendData(&app, atoi(windowSize)) != 0) {
            fprintf(stderr, "Client: Data send failed, aborting.\n");
            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        }
    }

/* ==========================================
 * Schritt 6: Verbindung schließen
 * ========================================== */

    if (arqSendClose(atoi(windowSize)) != 0) {
        fprintf(stderr, "Client: error while sending close.\n");
    }

    fclose(fp);
    closeClient();

/* ==========================================
 * Schritt 7: Rückgabewert
 * ========================================== */

    return EXIT_SUCCESS;
}
