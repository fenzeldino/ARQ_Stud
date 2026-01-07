/* Data Declarations for Praktikum
 * used by C and C++ !
 * Only data concerning Client and Server !
 */

#ifndef DATA_H_INCLUDED
#define DATA_H_INCLUDED

/* Fehlertexte werden in error.c definiert */
extern char *errorTable[];

/* Puffergröße für Daten / Zeilen */
#ifndef BufferSize
#define BufferSize 512
#endif

/* Anwendungssicht: reine Nutzdaten-Einheit (ohne Sequenznummern etc.) */
struct app_unit {
    unsigned long len;              /* Länge der Nutzdaten in Bytes      */
    char          data[BufferSize]; /* Nutzdaten                         */
};

/* Request vom Client zum Server.
 *
 * ReqType:
 *   ReqHello : Verbindungsaufbau / Beginn der Übertragung
 *   ReqData  : Datenpaket
 *   ReqClose : Übertragung beendet
 *
 * SeNr   : Paketnummer (0, 1, 2, ...) im ARQ-Protokoll
 *          (keine Byteposition)
 * FlNr   : Länge der Nutzdaten in Bytes
 */
struct request {
    unsigned char  ReqType;
#define ReqHello 'H'
#define ReqData  'D'
#define ReqClose 'C'

    unsigned long  FlNr;   /* Länge der übertragenen Daten in Bytes      */
    unsigned long  SeNr;   /* Byte-Offset (Sequence Number) im File      */

    char           name[BufferSize];  /* Nutzdaten (Zeileninhalt)       */
};

/* Fehlercodes für AnswWarn / AnswErr.
 * In AnswOk hat SeNo eine andere Bedeutung (siehe struct answer).
 */
enum {
    ERR_NONE            = 0,
    ERR_WRONG_SEQ       = 1, /* falsche Sequenznummer / Out-of-order */
    ERR_FILE_ERROR      = 2, /* Datei konnte nicht verarbeitet werden */
    ERR_ILLEGAL_REQUEST = 3, /* falscher ReqType / Protokollverletzung */
    /* 4–6 für eigene ARQ-Fehler reserviert */
    ERR_INTERNAL        = 7
};

/* Antwort des Servers.
 *
 * Bedeutung von SeNo:
 *  - AnswOk  : SeNo = Nummer des nächsten erwarteten Pakets
 *              (kumulativ: alle Pakete mit SeNr < SeNo sind korrekt angekommen)
 *  - AnswWarn/AnswErr : SeNo = Fehlercode (ERR_*)
 */
struct answer {
    unsigned char AnswType;
#define AnswHello 'H'
#define AnswOk    'O'
#define AnswWarn  'W'
#define AnswErr   0xFF

    unsigned long FlNr;  /* derzeit ungenutzt, für spätere Erweiterungen */
    unsigned long SeNo;  /* siehe Erklärung oben                          */

#define ErrNo SeNo       /* Alias: bei Warn/Err ist SeNo der Fehlercode   */
};

/* ARQ-Protokollparameter (Client-Seite, zentral dokumentiert) */
#define GBN_MAX_WINDOW       10
#define GBN_BUFFER_SIZE      (2 * GBN_MAX_WINDOW) // als Ringpuffer zu implementieren auf Client-Seite
#define GBN_TIMEOUT_INT_MS   100  /* Zeiteinheit eines Intervalls in Millisekunden */
#define GBN_TIMEOUT_UNITS    3    /* Timeout in Einheiten à TIMEOUT_INT   */

#endif /* DATA_H_INCLUDED */
