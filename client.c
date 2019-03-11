/*
 * Name: Emma Mirică
 * Project: TWAMP Protocol
 * Class: OSS
 * Email: emma.mirica@cti.pub.ro
 * Contributions: stephanDB
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
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <errno.h>
#include "twamp.h"

#define PORTBASE_SEND    30000
#define PORTBASE_RECV    20000
#define TEST_SESSIONS    1
#define TEST_MESSAGES    1
#define TIMEOUT          10     /* SECONDS - Timeout for TWAMP test packet
                                   Loss Threshold - in TWAMP-C Request Session */
#define STTIME           0      /* SECONDS - Time to start the Test session
                                   in TWAMP-C Request Session */
#define WAIT             1      /* SECONDS - Waiting time before Test session */

struct twamp_test_info {
    int testfd;
    uint16_t testport;
    uint16_t port;
    uint16_t serveroct;         /* in network order */
};

const uint8_t One = 1;
static enum Mode authmode = kModeUnauthenticated;
static long port_send = PORTBASE_SEND;
static long port_recv = PORTBASE_RECV;
static uint16_t test_sessions_no = TEST_SESSIONS;
static uint32_t test_sessions_msg = TEST_MESSAGES;
static uint64_t interv_msg = 0;
static uint8_t dscp_snd = 0;
static uint16_t payload_len = 160;
static uint8_t mbz_offset = 0;  /* Offset for padding in Symmetrical and DSCP/ECN Modes */
static uint16_t active_sessions = 0;
static uint16_t otbr = 0;       /* Octets To Be Reflected */
static int socket_family = AF_INET;

static enum Mode workmode = kModeUnauthenticated;

static uint8_t snd_tos = 0;     /* IP TOS=0 value in TWAMP */

/* The function that prints the help for this program */
static void usage(char *progname)
{
    fprintf(stderr, "Usage: %s [options]\n", progname);
    fprintf(stderr, "\nWhere \"options\" are:\n");
    fprintf(stderr,
            "   -s  server      The TWAMP server IP [Mandatory]\n"
            "   -a  authmode    Default Unauthenticated\n"
            "   -p  port_sender The miminum Test port sender (>1023)\n"
            "   -P  port_recv   The minimum Test port receiver (>1023)\n"
            "   -n  test_sess   The number of Test sessions\n"
            "   -m  no_test_msg The number of Test packets per Test session\n"
            "   -l  payload_len The length of Test packets in bytes (>40 <1473)\n"
            "   -t  snd_tos     The TOS value for Test packets (<256)\n"
            "   -d  snd_tos     The DSCP value for Test packets (<64)\n"
            "   -o  otbr        2 Octets to be reflected value in Reflected Mode (<65536)\n"
            "   -i  interval    The interval between Test packets [ms] (<10000)\n"
            "   -6              Use IPv6 if this option is defined. Otherwise, Ipv4 will be used.\n"
            "   -h              Prints this help message and exits\n");
    return;
}

/* The parse_options will check the command line arguments */
static int parse_options(struct hostent **server, int argc, char *argv[])
{
    char *server_host;
    if (argc < 2) {
        return 1;
    }
    int opt;

    while ((opt = getopt(argc, argv, "s:a:p:P:n:m:l:t:d:i:o:h:6")) != -1) {
        switch (opt) {
        case 's':
            /* Get the Server's IP */
            server_host = optarg;
            break;
        case 'a':
            authmode = strtol(optarg, NULL, 10);
            /* For now it only supports unauthenticated mode */
            if (authmode < 0 || authmode > 511)
                return 1;
            break;
        case 'p':
            port_send = strtol(optarg, NULL, 10);
            /* The port must be a valid one */
            if (port_send < 1024 || port_send > 65535)
                return 1;
            break;
        case 'P':
            port_recv = strtol(optarg, NULL, 10);
            /* The port must be a valid one */
            if (port_recv < 1024 || port_recv > 65535)
                return 1;
            break;
        case 'n':
            errno = 0;
            long sessions = strtol(optarg, NULL, 10);
            /* Test sessions number must be a valid one */
            if ((errno == ERANGE)
                || (sessions >= INT_MAX)
                || (sessions <= 0)) {
                perror("strtol");
                return 1;
            }
            test_sessions_no = (uint16_t) sessions;
            break;
        case 'm':
            errno = 0;
            long msgs = strtol(optarg, NULL, 10);
            /* Test messages per session must be a valid one */
            if ((errno == ERANGE)
                || (msgs >= LONG_MAX)
                || (msgs <= 0)) {
                perror("client");
                return 1;
            }
            test_sessions_msg = (uint32_t) msgs;
            break;
        case 'l':
            payload_len = strtol(optarg, NULL, 10);
            /* The length value must be a valid one */
            if (payload_len < 41 || payload_len > TST_PKT_SIZE)
                return 1;
            break;
        case 't':
            snd_tos = strtol(optarg, NULL, 10);
            /* The TOS value must be a valid one (no congestion on ECN */
            snd_tos = snd_tos - (((snd_tos & 0x2) >> 1) & (snd_tos & 0x1));
            break;
        case 'd':
            dscp_snd = strtol(optarg, NULL, 10);
            /* The DSCP value must be a valid one */
            if (dscp_snd > 63)
                return 1;
            snd_tos = dscp_snd << 2;
            //printf("TOS value is: %d\n", ip_hdr_snd.tos);
            break;
        case 'i':
            interv_msg = strtol(optarg, NULL, 10);
            /* The interval must be a valid one */
            if (interv_msg > 10000)
                return 1;
            break;
        case 'o':
            otbr = strtol(optarg, NULL, 10);
            /* The octets to be reflected value must be a valid one */
            authmode = authmode | kModeReflectOctets;
            break;
        case '6':
            socket_family = AF_INET6;
            break;
        case 'h':
        default:
            return 1;
        }
    }

    if(socket_family == AF_INET6) {
        *server = gethostbyname2(server_host, AF_INET6);
    } else {
        *server = gethostbyname(server_host);
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
    stop.SessionsNo = htonl(sessions);
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
        return "Some aspect of the request is not supported.";
    case kPermanentResourceLimitation:
        return
            "Cannot perform the request due to permanent resource limitations.";
    case kTemporaryResourceLimitation:
        return
            "Cannot perform the request due to temporary resource limitations.";
    default:
        return "Undefined failure";
    }
}

int main(int argc, char *argv[])
{
    char *progname = argv[0];
    srand(time(NULL));
    if (strrchr(progname, '/') != NULL) {
        progname = strrchr(progname, '/') + 1;
    }

    struct sockaddr_in serv_addr;
    struct sockaddr_in6 serv_addr6;
    //struct hostent *server = NULL;
    struct hostent *server;

    /* Sanity check */
#if 1
    if (getuid() == 0) {
        fprintf(stderr, "%s should not be run as root\n", progname);
        exit(EXIT_FAILURE);
    }
#endif

    /* Check client options */
    if (parse_options(&server, argc, argv)) {
        usage(progname);
        exit(EXIT_FAILURE);
    }
    if (server == NULL) {
        perror("Error, no such host");
        exit(EXIT_FAILURE);
    }

    /* Create server socket connection for the TWAMP-Control session */
    int servfd = socket(socket_family, SOCK_STREAM, 0);
    if (servfd < 0) {
        perror("Error opening socket");
        exit(EXIT_FAILURE);
    }

    char str_serv[INET6_ADDRSTRLEN]; /* String for Server IP address */
    if(socket_family == AF_INET6) {
        memset(&serv_addr6, 0, sizeof(serv_addr6));
        serv_addr6.sin6_family = AF_INET6;
        memcpy(&serv_addr6.sin6_addr, server->h_addr, server->h_length);
        serv_addr6.sin6_port = htons(SERVER_PORT);

        inet_ntop(AF_INET6, &(serv_addr6.sin6_addr), str_serv, INET6_ADDRSTRLEN);
        printf("Connecting to Server %s...\n", str_serv);
        if (connect(servfd, (struct sockaddr *)&serv_addr6, sizeof(serv_addr6)) < 0) {
            perror("Error connecting");
            exit(EXIT_FAILURE);
        }

    } else {
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        serv_addr.sin_port = htons(SERVER_PORT);


        inet_ntop(AF_INET, &(serv_addr.sin_addr), str_serv, INET_ADDRSTRLEN);
        printf("Connecting to Server %s...\n", str_serv);
        if (connect(servfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Error connecting");
            exit(EXIT_FAILURE);
        }
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

    printf("Received ServerGreeting message with mode %d\n",
           ntohl(greet.Modes));

    /* Abort with Mode 0 */
    if ((ntohl(greet.Modes) & 0x000F) == 0) {
        close(servfd);
        fprintf(stderr, "The server does not support any usable Mode\n");
        exit(EXIT_FAILURE);
    }

    /* Compute SetUpResponse */

    SetUpResponse resp;
    memset(&resp, 0, sizeof(resp));
    workmode = ntohl(greet.Modes) & authmode;
    resp.Mode = htonl(workmode);
    printf("Sending SetUpResponse with mode %d\n", workmode);
    rv = send(servfd, &resp, sizeof(resp), 0);
    if (rv <= 0) {
        close(servfd);
        perror("Error sending Greeting Response");
        exit(EXIT_FAILURE);
    }

    /* Set Timeout to exit in case server does not respond to mode 0 */
    if (workmode == 0) {
        //close(servfd);
        fprintf(stderr,
                "The client and server do not support any usable Mode.\n");
        //exit(EXIT_FAILURE);
        int result;

        /* Set Timeout */
        struct timeval timeout = { 5, 0 };  //set timeout for 5 seconds

        /* Set receive UDP message timeout value */
#ifdef SO_RCVTIMEO
        result = setsockopt(servfd, SOL_SOCKET, SO_RCVTIMEO,
                            (char *)&timeout, sizeof(struct timeval));
        if (result != 0) {
            fprintf(stderr,
                    "[PROBLEM] Cannot set the timeout value for reception.\n");
        }
#else
        fprintf(stderr,
                "No way to set the timeout value for incoming packets on that platform.\n");
#endif
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

    /* Check Server host IP address */
    char str_host[INET6_ADDRSTRLEN]; /* String for Client IP address */

    struct sockaddr_in host_addr;
    struct sockaddr_in6 host_addr6;

    if(socket_family == AF_INET6) {
        socklen_t addr_size = sizeof(host_addr6);
        getsockname(servfd, (struct sockaddr *)&host_addr, &addr_size);
        inet_ntop(AF_INET6, &(host_addr6.sin6_addr), str_host, sizeof(str_host));
    } else {
        socklen_t addr_size = sizeof(host_addr);
        getsockname(servfd, (struct sockaddr *)&host_addr, &addr_size);
        inet_ntop(AF_INET, &(host_addr.sin_addr), str_host, sizeof(str_host));
    }

    printf("Received ServerStart at Client %s\n", str_host);

    /* After the TWAMP-Control connection has been established, the
     * Control-Client will negociate and set up some TWAMP-Test sessions */

    struct twamp_test_info *twamp_test =
        malloc(test_sessions_no * sizeof(struct twamp_test_info));
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

        int check_time = CHECK_TIMES;
        if(socket_family == AF_INET6) {
            struct sockaddr_in6 local_addr6;
            memset(&local_addr6, 0, sizeof(local_addr6));
            local_addr6.sin6_family = AF_INET6;
            local_addr6.sin6_addr = in6addr_any;

            /* Try to bind on an available port */
            while (check_time--) {
                twamp_test[active_sessions].testport = port_send + rand() % 1000;
                local_addr6.sin6_port = htons(twamp_test[active_sessions].testport);
                if (!bind
                    (twamp_test[active_sessions].testfd,
                     (struct sockaddr *)&local_addr6, sizeof(struct sockaddr)))
                    break;
            }
        } else {
            struct sockaddr_in local_addr;
            memset(&local_addr, 0, sizeof(local_addr));
            local_addr.sin_family = AF_INET;
            local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            /* Try to bind on an available port */
            while (check_time--) {
                twamp_test[active_sessions].testport = port_send + rand() % 1000;
                local_addr.sin_port = htons(twamp_test[active_sessions].testport);
                if (!bind
                    (twamp_test[active_sessions].testfd,
                     (struct sockaddr *)&local_addr, sizeof(struct sockaddr)))
                    break;
            }
        }
        if (check_time < 0) {
            fprintf(stderr,
                    "Couldn't find a port to bind to for session %d\n  %s",
                    i + 1, strerror(errno));
            continue;
        }

        /* Set socket options */
        set_socket_option(twamp_test[active_sessions].testfd, HDR_TTL);
        set_socket_tos(twamp_test[active_sessions].testfd, snd_tos);

        RequestSession req;
        memset(&req, 0, sizeof(req));
        req.Type = kRequestTWSession;
        req.IPVN = 4;
        req.SenderPort = htons(twamp_test[active_sessions].testport);
        req.ReceiverPort = htons(port_recv + rand() % 1000);
        if(socket_family == AF_INET) {
            req.SenderAddress = host_addr.sin_addr.s_addr;
            //req.SenderAddress = 0;
            req.ReceiverAddress = serv_addr.sin_addr.s_addr;
        }
        if ((workmode & KModeSymmetrical) == KModeSymmetrical) {
            mbz_offset = 27;
            if ((workmode & kModeDSCPECN) == kModeDSCPECN) {
                mbz_offset = 28;
            }
        }
        req.PaddingLength = htonl(payload_len - 14 - mbz_offset);   // As defined in RFC 6038#4.2
        TWAMPTimestamp timestamp = get_timestamp();
        timestamp.integer = htonl(ntohl(timestamp.integer) + STTIME);   // 0s for start time
        req.StartTime = timestamp;
        req.Timeout.integer = htonl(TIMEOUT);
        req.Timeout.fractional = htonl(0);
        req.TypePDescriptor = htonl((snd_tos & 0xFC) << 22);
        printf("Sending RequestTWSession for Receiver port %d...\n",
               ntohs(req.ReceiverPort));

        if ((workmode & kModeReflectOctets) == kModeReflectOctets) {
            req.OctetsToBeReflected = htons(otbr);
            req.PadLenghtToReflect = htons(2);
        }

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
            perror("Error receiving Accept-Session");
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
        twamp_test[active_sessions].port = ntohs(acc.Port);
        printf("Received Accept-Session for Receiver port %d...\n",
               twamp_test[active_sessions].port);

        /* SID */
        uint32_t sid_addr;      /* in network order */
        TWAMPTimestamp sid_time;    /* in network order */
        uint32_t sid_rand;      /* in network order */
        memcpy(&sid_addr, &acc.SID, 4);
        memcpy(&sid_time, &acc.SID[4], 8);
        memcpy(&sid_rand, &acc.SID[12], 4);

        fprintf(stderr, "SID: 0x%04X.%04X.%04X.%04X \n", ntohl(sid_addr),
                ntohl(sid_time.integer),
                ntohl(sid_time.fractional), ntohl(sid_rand));

        twamp_test[active_sessions].serveroct = (acc.ServerOctets);

        fprintf(stderr,
                "#Session \t%d, Sender  \t%s:%d, Receiver \t%s:%d, Mode: %d\n",
                active_sessions, str_host, twamp_test[active_sessions].testport,
                str_serv, twamp_test[active_sessions].port, workmode);
        if ((workmode & kModeReflectOctets) == kModeReflectOctets) {
            fprintf(stderr,
                    "Octets to be reflected: %u, Reflected octets: %d,"
                    " Server Octets: %d\n", otbr, ntohs(acc.ReflectedOctets),
                    ntohs(twamp_test[active_sessions].serveroct));
        }
        active_sessions++;

    }

    fprintf(stderr, "Nb of Packets \t%u, Packet length \t%d, DSCP \t%d,"
            " TOS \t%d\n", test_sessions_msg, payload_len,
            (snd_tos & 0xFC) >> 2, snd_tos);
    if (active_sessions) {
        printf("Sending Start-Sessions for all active Sender ports...\n");

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
        sleep(WAIT);
    }

    /* For each accepted TWAMP-Test session send test_sessions_msg
     * TWAMP-Test packets */
    for (i = 0; i < active_sessions; i++) {
        uint32_t j;

        /* Title for printing results */
        fprintf(stderr,
                "\tTime\t, Snd#\t, Rcv#\t, SndPt\t, RcvPt\t,  Sync\t, FW TTL,"
                " SW TTL, SndTOS, FW_TOS, SW_TOS, NwRTD\t, IntD\t, FWD\t,"
                " SWD  [ms]\n");
        uint32_t lost_msg = 0;
        uint32_t fw_lost_msg = 0;
        uint32_t sw_lost_msg = 0;
        uint32_t rt_msg = 0;
        uint32_t rcv_sn = 0;
        uint32_t snd_sn = 0;
        uint32_t index = 0;
        uint64_t rtd = LOSTTIME * 1000000;

        for (j = 0; j < test_sessions_msg; j++) {
            SenderUPacket pack;
            memset(&pack, 0, sizeof(pack));
            index = 0 * test_sessions_msg + j;
            pack.seq_number = htonl(index);
            pack.time = get_timestamp();
            pack.error_estimate = htons(0x8001);    // Sync = 1, Multiplier = 1.

            memcpy(&pack.padding[mbz_offset], &twamp_test[i].serveroct, 2);

            if(socket_family == AF_INET6) {
                serv_addr6.sin6_port = htons(twamp_test[i].port);
                rv = sendto(twamp_test[i].testfd, &pack, payload_len, 0,
                            (struct sockaddr *)&serv_addr6, sizeof(serv_addr6));
            } else {
                serv_addr.sin_port = htons(twamp_test[i].port);
                rv = sendto(twamp_test[i].testfd, &pack, payload_len, 0,
                            (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            }

            if (rv <= 0) {
                perror("Error sending test packet");
                continue;
            }

            ReflectorUPacket pack_reflect;
            memset(&pack_reflect, 0, sizeof(pack_reflect));

            /* New for recvmsg */

            struct msghdr *message = malloc(sizeof(struct msghdr));
            struct cmsghdr *c_msg;
            char *control_buffer = malloc(TST_PKT_SIZE);
            int control_length = TST_PKT_SIZE;

            memset(message, 0, sizeof(*message));
            message->msg_iov = malloc(sizeof(struct iovec));
            message->msg_iov->iov_base = &pack_reflect;
            message->msg_iov->iov_len = TST_PKT_SIZE;
            message->msg_iovlen = 1;
            /* Message control does not exist on every system. For instance, HP Tru64
             * does not have it */
#ifndef NO_MESSAGE_CONTROL
            message->msg_control = control_buffer;
            message->msg_controllen = control_length;
#endif

            int rv = recvmsg(twamp_test[i].testfd, message, 0);

            /* Get Time of the received TWAMP-Test response message */
            TWAMPTimestamp rcv_resp_time = get_timestamp();

            if (rv <= 0) {
                uint64_t t_sender_usec = get_usec(&pack.time);
                fprintf(stderr,
                        "%.0f\t, %3d\t, %3c\t, %d\t, %d\t, %3c\t, %3c\t, %3c\t, %3c\t, %3c\t,"
                        " %3c\t, %3c\t, %3c\t, %3c\t, %3c\n",
                        (double)t_sender_usec * 1e-3, (int)(index), '-',
                        twamp_test[i].testport, twamp_test[i].port, '-', '-',
                        '-', '-', '-', '-', '-', '-', '-', '-');
                rtd = LOSTTIME * 1000000;
                lost_msg++;
                /* print loss results */
                if (((j + 1) % 10) == 0 && lost_msg) {
                    printf("RT Lost packets: %u/%u,  RT Loss Ratio: %3.2f%%\n",
                           lost_msg, (int)(j + 1),
                           (float)100 * lost_msg / (j + 1));
                    if (rt_msg) {
                        printf
                            ("FW Lost packets: %u/%u,  FW Loss Ratio: %3.2f%%\n",
                             fw_lost_msg, rt_msg,
                             (float)100 * fw_lost_msg / rt_msg);
                        printf
                            ("SW Lost packets: %u/%u,  SW Loss Ratio: %3.2f%%\n",
                             sw_lost_msg, (int)rt_msg - fw_lost_msg,
                             (float)100 * sw_lost_msg / (rt_msg - fw_lost_msg));
                    }
                }
                continue;
            } else {

                /* Get IPTTL value */

                uint8_t sw_ttl = 0;
                uint8_t sw_tos = 0;

#ifndef NO_MESSAGE_CONTROL
                for (c_msg = CMSG_FIRSTHDR(message); c_msg;
                     c_msg = (CMSG_NXTHDR(message, c_msg))) {
                    if ((c_msg->cmsg_level == IPPROTO_IP
                         && c_msg->cmsg_type == IP_TTL)
                        || (c_msg->cmsg_level == IPPROTO_IPV6
                            && c_msg->cmsg_type == IPV6_HOPLIMIT)) {
                        sw_ttl = *(int *)CMSG_DATA(c_msg);

                    } else if (c_msg->cmsg_level == IPPROTO_IP
                               && c_msg->cmsg_type == IP_TOS) {
                        sw_tos = *(int *)CMSG_DATA(c_msg);

                    } else {
                        fprintf(stderr,
                                "\tWarning, unexpected data of level %i and type %i\n",
                                c_msg->cmsg_level, c_msg->cmsg_type);
                    }
                }
#else
                fprintf(stdout,
                        "No message control on that platform, so no way to find IP options\n");
#endif

                /* Indicators for one-way losses */
                if (rt_msg == 0) {
                    rt_msg = 1;
                } else {
                    fw_lost_msg =
                        fw_lost_msg + index - snd_sn -
                        ntohl(pack_reflect.seq_number)
                        + rcv_sn;
                    sw_lost_msg =
                        sw_lost_msg + ntohl(pack_reflect.seq_number) - rcv_sn -
                        1;
                    rt_msg = rt_msg + index - snd_sn;
                }
                /* Print the latency metrics */
                rtd =
                    print_metrics(twamp_test[i].testport, twamp_test[i].port,
                                  snd_tos, sw_ttl, sw_tos, &rcv_resp_time,
                                  &pack_reflect, workmode);

            }

            /* Indicators for one-way losses */
            snd_sn = index;
            rcv_sn = ntohl(pack_reflect.seq_number);

            /* Print loss results */
            if ((((j + 1) % 10) == 0) && ((j + 1) < test_sessions_msg)) {
                printf("RT Lost packets: %u/%u,  RT Loss Ratio: %3.2f%%\n",
                       lost_msg, (int)(j + 1), (float)100 * lost_msg / (j + 1));
                if (rt_msg && lost_msg) {
                    printf("FW Lost packets: %u/%u,  FW Loss Ratio: %3.2f%%\n",
                           fw_lost_msg, rt_msg,
                           (float)100 * fw_lost_msg / rt_msg);
                    printf("SW Lost packets: %u/%u,  SW Loss Ratio: %3.2f%%\n",
                           sw_lost_msg, (int)rt_msg - fw_lost_msg,
                           (float)100 * sw_lost_msg / (rt_msg - fw_lost_msg));
                }
            }

            /* Interval sleep between packets */

            if (rtd < interv_msg * 1000) {
                usleep(interv_msg * 1000 - rtd);
            };

        }
        /* Print final loss results */
        printf("RT Lost packets: %u/%u,  RT Loss Ratio: %3.2f%%\n",
               lost_msg, test_sessions_msg,
               (float)100 * lost_msg / test_sessions_msg);
        if (rt_msg && lost_msg) {
            printf("FW Lost packets: %u/%u,  FW Loss Ratio: %3.2f%%\n",
                   fw_lost_msg, rt_msg, (float)100 * fw_lost_msg / rt_msg);
            printf("SW Lost packets: %u/%u,  SW Loss Ratio: %3.2f%%\n",
                   sw_lost_msg, (int)rt_msg - fw_lost_msg,
                   (float)100 * sw_lost_msg / (rt_msg - fw_lost_msg));
        }

    }

    /* After all TWAMP-Test packets were sent, send a StopSessions
     * packet and finish */
    if (active_sessions) {

        printf("Sending Stop-Sessions for all active ports...\n");
        rv = send_stop_session(servfd, kOK, active_sessions);
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
