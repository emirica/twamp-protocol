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

static enum Mode authmode = kModeUnauthenticated;
static int port_send = PORTBASE_SEND;
static int port_recv = PORTBASE_RECV;
static int test_sessions_no = TEST_SESSIONS;
static int test_sessions_msg = TEST_MESSAGES;

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
            if ((port_send > 0 && port_send < 1024) || port_send > 65536 || port_send < 0)
                return 1;
            break;
        case 'P':
            port_recv = atoi(optarg);
            /* The port must be a valid one */
            if ((port_recv > 0 && port_recv < 1024) || port_recv > 65536 || port_recv < 0)
                return 1;
            break;
        case 'n':
            test_sessions_no = atoi(optarg);
            break;
        case 'm':
            test_sessions_msg = atoi(optarg);
            break;
        case 'h':
        default:
            return 1;
        }
    }

    return 0;
}

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

    printf("Connecting to server...\n\n");
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
    printf("Received ServerGreeting.\n\n");

    /* Compute SetUpResponse */
    printf("Sending SetUpResponse...\n\n");
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
    printf("Received ServerStart.\n\n");

    /* After the TWAMP-Control connection has been established, the
     * Control-Client will negociate and set up some TWAMP-Test sessions */

    /* Setup test socket */
    int testfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (testfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;

    int testport = 20000 + rand() % 1000;
    while (1) {
        testport = 20000 + rand() % 1000;
        local_addr.sin_port = ntohs(testport);
        if (!bind(testfd, (struct sockaddr *)&local_addr,
                  sizeof(struct sockaddr)))
            break;
    }


    printf("Sending RequestSession...\n\n");
    RequestSession req;
    memset(&req, 0, sizeof(req));

    req.Type = kRequestTWSession;
    req.IPVN = 4;
    req.SenderPort = ntohs(testport);
    req.ReceiverPort = ntohs(testport);
    req.PaddingLength = 27;     // As defined in RFC 6038#4.2 // TODO: correct?
    TWAMPTimestamp timestamp = get_timestamp();
    timestamp.integer = htonl(ntohl(timestamp.integer) + 10);   // TODO: 10 seconds?
    req.StartTime = timestamp;
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    timeval_to_timestamp(&timeout, &req.Timeout);

    rv = send(servfd, &req, sizeof(req), 0);
    if (rv <= 0) {
        perror("Error sending Session request message");
        exit(EXIT_FAILURE);
    }

    AcceptSession acc;
    rv = recv(servfd, &acc, sizeof(acc), 0);
    if (rv <= 0) {
        perror("Error receiving Accept Session");
        exit(EXIT_FAILURE);
    }

    rv = send_start_sessions(servfd);
    if (rv <= 0) {
        perror("Error sending Start Sessions");
        exit(EXIT_FAILURE);
    }

    UPacket pack;
    memset(&pack, 0, sizeof(pack));

    pack.seq_number = 0;
    pack.time = get_timestamp();
    pack.error_estimate = 1;    // TODO:Multiplyer = 1.
    pack.sender_ttl = 255;

    serv_addr.sin_port = acc.Port;
    rv = sendto(testfd, &pack, sizeof(pack), 0,
                (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (rv <= 0) {
        perror("Error sending test packet");
        exit(EXIT_FAILURE);
    }

    socklen_t len;
    rv = recvfrom(testfd, &pack, sizeof(pack), 0,
                  (struct sockaddr *)&serv_addr, &len);
    if (rv <= 0) {
        perror("Error receiving test reply");
        exit(EXIT_FAILURE);
    }

    rv = send_stop_session(servfd, kOK, 1);
    if (rv <= 0) {
        perror("Error sending stop session");
        exit(EXIT_FAILURE);
    }

    return 0;
}
