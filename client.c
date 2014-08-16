/*
 * Name: Emma Mirică
 * Project: TWAMP Protocol
 * Class: OSS
 * Email: emma.mirica@cti.pub.ro
 *
 * Source: client.c
 * Note: contains the TWAMP client implementation.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/stat.h>
#include <time.h>
#include "twamp.h"

#define PORTBASE_SEND    30000
#define PORTBASE_RECV    20000
#define TEST_SESSIONS    1
#define TEST_MESSAGES    1
#define TIMEOUT          2 /* SECONDS */

struct twamp_test_info {
    int testfd;
    int testport;
    uint16_t port;
};

static enum Mode authmode = kModeUnauthenticated;
static int port_send = PORTBASE_SEND;
static int port_recv = PORTBASE_RECV;

static uint16_t test_sessions_no = TEST_SESSIONS;
static uint32_t test_sessions_msg = TEST_MESSAGES;
static uint16_t active_sessions = 0;

/* The function that prints the help for this program */
static void usage(char *progname)
{
    fprintf(stderr, "Usage: %s [options]\n", progname);
    fprintf(stderr, "\n\nWhere \"options\" are:\n");
    fprintf(stderr,
            "   -s  server      The TWAMP server IP [Mandatory]\n"
            "   -a  authmode    Default Unauthenticated\n"
            "   -p  port_sender The miminum Test port sender\n"
            "   -P  port_recv   The minimum Test port receiver\n"
            "   -n  test_sess   The number of Test sessions\n"
            "   -m  no_test_msg The number of Test packets per Test session\n"
           );
    return;
}

/* The parse_options will check the command line arguments */
static int parse_options(struct hostent **server, char *progname, int argc, char *argv[])
{
    if (argc < 2) {
        return 1;
    }
    int opt;

    while ((opt = getopt(argc, argv, "s:a:p:P:n:m:h")) != -1) {
        switch (opt) {
        case 's':
            /* Get the Server's IP */
            *server = gethostbyname(optarg);
            break;
        case 'a':
            /* For now it only supports unauthenticated mode */
            authmode = kModeUnauthenticated;
            break;
        case 'p':
            port_send = atoi(optarg);
            /* The port must be a valid one */
            if (port_send < 1024 || port_send > 65535)
                return 1;
            break;
        case 'P':
            port_recv = atoi(optarg);
            /* The port must be a valid one */
            if (port_recv < 1024 || port_recv > 65535)
                return 1;
            break;
        case 'n':
            test_sessions_no = atoi(optarg);
            /* Test sessions number must be a valid one */
            if (test_sessions_no < 0)
                return 1;
            break;
        case 'm':
            test_sessions_msg = atoi(optarg);
            /* Test messages per session must be a valid one */
            if (test_sessions_msg < 0)
                return 1;
            break;
        case 'h':
        default:
            return 1;
        }
    }

    return 0;
}

/* This function sends StopSessions to stop all active Test sessions */
static int send_stop_session(int socket, int accept, int sessions)
{
    StopSessions stop;
    memset(&stop, 0, sizeof(stop));
    stop.Type = kStopSessions;
    stop.Accept = accept;
    stop.SessionsNo = sessions;
    return send(socket, &stop, sizeof(stop), 0);
}

static int send_start_sessions(int socket)
{
    StartSessions start;
    memset(&start, 0, sizeof(start));
    start.Type = kStartSessions;
    return send(socket, &start, sizeof(start), 0);
}

/* The function will return a significant message for a given code */
static char *get_accept_str(int code)
{
    switch (code) {
    case kOK:
        return "OK.";
    case kFailure:
        return "Failure, reason unspecified.";
    case kInternalError:
        return "Internal error.";
    case kAspectNotSupported:
        return "Some aspect of request is not supported.";
    case kPermanentResourceLimitation:
        return "Cannot perform request due to permanent resource limitations.";
    case kTemporaryResourceLimitation:
        return "Cannot perform request due to temporary resource limitations.";
    default:
        return "Undefined failure";
    }
}

int main(int argc, char *argv[])
{
    char *progname = NULL;
    srand(time(NULL));
    progname = (progname = strrchr(argv[0], '/')) ? progname + 1: *argv;

    struct sockaddr_in serv_addr;
    struct hostent *server = NULL;

    /* Sanity check */
    if (getuid() == 0) {
        fprintf(stderr, "%s should not be run as root\n", progname);
        exit(EXIT_FAILURE);
    }

    /* Check client options */
    if (parse_options(&server, progname, argc, argv)) {
        usage(progname);
        exit(EXIT_FAILURE);
    }
    if (server == NULL) {
        perror("Error, no such host");
        exit(EXIT_FAILURE);
    }

    /* Create server socket connection for the TWAMP-Control session */
    int servfd = socket(AF_INET, SOCK_STREAM, 0);
    if (servfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(SERVER_PORT);

    printf("Connecting to server %s...\n", inet_ntoa(serv_addr.sin_addr));
    if (connect(servfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error connecting");
        exit(EXIT_FAILURE);
    }

    /* TWAMP-Control change of messages after TCP connection is established */

    /* Receive Server Greeting and check Modes */
    ServerGreeting greet;
    memset(&greet, 0, sizeof(greet));
    int rv = recv(servfd, &greet, sizeof(greet), 0);
    if (rv <= 0) {
        close(servfd);
        perror("Error receiving Server Greeting");
        exit(EXIT_FAILURE);
    }
    if (greet.Modes == 0) {
        close(servfd);
        fprintf(stderr, "The server does not support any usable Mode\n");
        exit(EXIT_FAILURE);
    }
    printf("Received ServerGreeting.\n");

    /* Compute SetUpResponse */
    printf("Sending SetUpResponse...\n");
    SetUpResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.Mode = greet.Modes & authmode;
    rv = send(servfd, &resp, sizeof(resp), 0);
    if (rv <= 0) {
        close(servfd);
        perror("Error sending Greeting Response");
        exit(EXIT_FAILURE);
    }

    /* Receive ServerStart message */
    ServerStart start;
    memset(&start, 0, sizeof(start));
    rv = recv(servfd, &start, sizeof(start), 0);
    if (rv <= 0) {
        close(servfd);
        perror("Error Receiving Server Start");
        exit(EXIT_FAILURE);
    }
    /* If Server did not accept our request */
    if (start.Accept != kOK) {
        close(servfd);
        fprintf(stderr, "Request failed: %s\n", get_accept_str(start.Accept));
        exit(EXIT_FAILURE);
    }
    printf("Received ServerStart.\n");

    /* After the TWAMP-Control connection has been established, the
     * Control-Client will negociate and set up some TWAMP-Test sessions */

    struct twamp_test_info *twamp_test = malloc(test_sessions_no * sizeof(struct twamp_test_info));
    if (!twamp_test) {
        fprintf(stderr, "Error on malloc\n");
        close(servfd);
        exit(EXIT_FAILURE);
    }

    uint16_t i;
    /* Set TWAMP-Test sessions */
    for (i = 0; i < test_sessions_no; i++) {

        /* Setup test socket */
        twamp_test[active_sessions].testfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (twamp_test[active_sessions].testfd < 0) {
            perror("Error opening socket");
            continue;
        }

        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = INADDR_ANY;

        /* Try to bind on an available port */
        int check_time = CHECK_TIMES;
        while (check_time--) {
            twamp_test[active_sessions].testport = port_send + rand() % 1000;
            local_addr.sin_port = ntohs(twamp_test[active_sessions].testport);
            if (!bind(twamp_test[active_sessions].testfd, (struct sockaddr *)&local_addr,
                      sizeof(struct sockaddr)))
                break;
        }
        if (check_time == 0) {
            fprintf(stderr, "Couldn't find a port to bind to for session %d\n", i + 1);
            continue;
        }

        printf("Sending RequestTWSession for port %d...\n", twamp_test[active_sessions].testport);
        RequestSession req;
        memset(&req, 0, sizeof(req));
        req.Type = kRequestTWSession;
        req.IPVN = 4;
        req.SenderPort = ntohs(twamp_test[active_sessions].testport);
        req.ReceiverPort = ntohs(port_recv + rand() % 1000);
        req.PaddingLength = 27;     // As defined in RFC 6038#4.2
        TWAMPTimestamp timestamp = get_timestamp();
        timestamp.integer = htonl(ntohl(timestamp.integer) + 10);   // 10 seconds for start time
        req.StartTime = timestamp;
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;
        timeval_to_timestamp(&timeout, &req.Timeout);

        /* Trying to send the RequestTWSession request for this TWAMP-Test */
        rv = send(servfd, &req, sizeof(req), 0);
        if (rv <= 0) {
            fprintf(stderr, "[%d] ", twamp_test[active_sessions].testport);
            perror("Error sending RequestTWSession message");
            close(twamp_test[active_sessions].testfd);
            free(twamp_test);
            close(servfd);
            exit(EXIT_FAILURE);
        }

        /* See the Server's response */
        AcceptSession acc;
        memset(&acc, 0, sizeof(acc));
        rv = recv(servfd, &acc, sizeof(acc), 0);
        if (rv <= 0) {
            fprintf(stderr, "[%d] ", twamp_test[active_sessions].testport);
            perror("Error receiving Accept Session");
            close(twamp_test[active_sessions].testfd);
            free(twamp_test);
            close(servfd);
            exit(EXIT_FAILURE);
        }
        /* See the Server response to this RequestTWSession message */
        if (acc.Accept != kOK) {
            close(twamp_test[active_sessions].testfd);
            continue;
        }
        twamp_test[active_sessions].port = acc.Port;
        active_sessions++;

    }

    if (active_sessions) {
        printf("Sending StartSessions for all active ports ...\n");
        /* If there are any accepted Test-Sessions then send
         * the StartSessions message */
        rv = send_start_sessions(servfd);
        if (rv <= 0) {
            perror("Error sending StartSessions");
            /* Close all TWAMP-Test sockets */
            for (i = 0; i < active_sessions; i++)
                close(twamp_test[i].testfd);
            free(twamp_test);
            close(servfd);
            exit(EXIT_FAILURE);
        }
    }

    /* For each accepted TWAMP-Test session send test_sessions_msg
     * TWAMP-Test packets */
    for (i = 0; i < active_sessions; i++) {
        uint32_t j;
        for (j = 0; j < test_sessions_msg; j++) {
            SenderUPacket pack;
            memset(&pack, 0, sizeof(pack));
            pack.seq_number = htonl(i * test_sessions_msg + j);
            pack.time = get_timestamp();
            pack.error_estimate = 0x100;    // Multiplier = 1.

            printf("Sending TWAMP-Test message %d for port %d...\n", j + 1, ntohs(twamp_test[i].port));
            serv_addr.sin_port = twamp_test[i].port;
            rv = sendto(twamp_test[i].testfd, &pack, sizeof(pack), 0,
                        (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            if (rv <= 0) {
                perror("Error sending test packet");
                continue;
            }

            socklen_t len = sizeof(serv_addr);
            ReflectorUPacket pack_reflect;
            memset(&pack_reflect, 0, sizeof(pack_reflect));
            rv = recvfrom(twamp_test[i].testfd, &pack_reflect, sizeof(pack_reflect), 0,
                          (struct sockaddr *)&serv_addr, &len);
            if (rv <= 0) {
                perror("Error receiving test reply");
                continue;
            }
            printf("Received TWAMP-Test message response %d for port %d.\n", j + 1, ntohs(twamp_test[i].port));
            /* Print the round-trip metrics */
            print_metrics(j + 1, ntohs(twamp_test[i].port), &pack_reflect);
        }
    }

    /* After all TWAMP-Test packets were sent, send a StopSessions
     * packet and finish */
    if (active_sessions) {
        printf("Sending StopSessions for all active ports ...\n");
        rv = send_stop_session(servfd, kOK, 1);
        if (rv <= 0) {
            perror("Error sending stop session");
            /* Close all TWAMP-Test sockets */
            for (i = 0; i < active_sessions; i++)
                close(twamp_test[i].testfd);
            free(twamp_test);
            close(servfd);
            exit(EXIT_FAILURE);
        }
    }
    /* Close all TWAMP-Test sockets */
    for (i = 0; i < active_sessions; i++)
        close(twamp_test[i].testfd);
    free(twamp_test);
    close(servfd);
    return 0;
}
