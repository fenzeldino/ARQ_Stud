/* config_example.h - example configuration for Go-Back-N client/server */

#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include <sys/socket.h>

/*
 * Einstellungen für Server & Client
 */

#define DEFAULT_SERVER       NULL          /* NULL ... will use the loopback interface */
#define DEFAULT_FAMILY       PF_INET6      /* IPv6 | PF_UNSPEC ... accept either IPv4 or IPv6 */
#define DEFAULT_SOCKTYPE     SOCK_DGRAM    /* UDP  | SOCK_STREAM ... TCP */
#define DEFAULT_PORT         "3333"        /* default UDP port as string */

#define DEFAULT_LOOPBACK_HOST "::1"

#define BUFFER_SIZE          65500

#define UNKNOWN_NAME "<unknown>"

/* Beispiel-Usage-Texte für den Client (anpassen wie gewünscht) */
#define P_MESSAGE_1 "Simple ARQ UDP client\n"
#define P_MESSAGE_6 "Usage: %s -f filename [-a address] [-p port] [-w window]\n"
#define P_MESSAGE_7 "  -a <address> : server address (default: %s)\n"
#define P_MESSAGE_8 "  -p <port>    : server port (default: %s)\n"
#define P_MESSAGE_9 "  -w <window>  : Go-Back-N window size\n"

#endif /* CONFIG_H_INCLUDED */
