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

int send_stop_session(int socket, int accept, int sessions)
{
	StopSessions stop;
	memset(&stop, 0, sizeof(stop));
	stop.Type = kStopSessions;
	stop.Accept = accept;
	stop.SessionsNo = sessions;
	return send(socket, &stop, sizeof(stop), 0);
}

int send_start_sessions(int socket)
{
	StartSessions start;
	memset(&start, 0, sizeof(start));
	start.Type = kStartSessions;
	return send(socket, &start, sizeof(start), 0);
}

char * get_accept_str(int code)
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
	if (argc < 2)
		return 1;

	struct sockaddr_in serv_addr;
	struct hostent *server;

	int servfd = socket(AF_INET, SOCK_STREAM, 0);
	if (servfd < 0) {
		perror("ERROR opening socket");
		exit(EXIT_FAILURE);
	}

	server = gethostbyname(argv[1]);
	if (server == NULL) {
		perror("ERROR, no such host");
		exit(EXIT_FAILURE);
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
	serv_addr.sin_port = htons(SERVER_PORT);

	printf("Connecting to %s...\n", argv[1]);
	if (connect(servfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR connecting");
		exit(EXIT_FAILURE);
	}

	ServerGreeting greet;
	int rv = recv(servfd, &greet, sizeof(greet), 0);
	if (rv <= 0) {
		perror("ERROR receiving Server Greeting");
		exit(EXIT_FAILURE);
	}

	if (greet.Modes == 0) {
		close(servfd);
		fprintf(stderr, "The server does not support any usable Mode\n");
		exit(EXIT_FAILURE);
	}

	printf("Mode: %d\n", greet.Modes);

	SetUpResponse resp;
	memset(&resp, 0, sizeof(resp));

	resp.Mode = greet.Modes & kModeUnauthenticated;
	rv = send(servfd, &resp, sizeof(resp), 0);
	printf("RV: %d\n", rv);

	ServerStart start;
	rv = recv(servfd, &start, sizeof(start), 0);
	if (rv <= 0) {
		perror("ERROR Receiving Server Start");
		exit(EXIT_FAILURE);
	}

	if (start.Accept != kOK) {
		fprintf(stderr, "Received \n");
	}

	if (start.Accept == 0) {
		printf("Accept\n");
	} else {
		printf("Not accepted\n");
		return 0;
	}

	// Setup test socket

	int testfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (testfd < 0)
		perror("ERROR opening socket");

	struct sockaddr_in local_addr;
	memset(&local_addr, 0, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = INADDR_ANY;
	int testport = 20000 + rand() % 1000;
	while (1) {
		testport = 20000 + rand() % 1000;
		local_addr.sin_port = ntohs(testport);
		if (!bind
			(testfd, (struct sockaddr *)&local_addr, sizeof(struct sockaddr)))
			break;
	}

	RequestSession req;
	memset(&req, 0, sizeof(req));

	req.Type = kRequestTWSession;
	req.IPVN = 4;
	req.SenderPort = ntohs(testport);
	req.ReceiverPort = ntohs(testport);
	req.PaddingLength = 27;		// As defined in RFC 6038#4.2 // TODO: correct?
	TWAMPTimestamp timestamp = get_timestamp();
	timestamp.integer = htonl(ntohl(timestamp.integer) + 10);	// TODO: 10 seconds?
	req.StartTime = timestamp;
	struct timeval timeout;
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;
	timeval_to_timestamp(&timeout, &req.Timeout);

	rv = send(servfd, &req, sizeof(req), 0);
	// TODO check

	AcceptSession acc;
	rv = recv(servfd, &acc, sizeof(acc), 0);
	// TODO check

	rv = send_start_sessions(servfd);
	// TODO check

	UPacket pack;
	memset(&pack, 0, sizeof(pack));

	pack.seq_number = 0;
	pack.time = get_timestamp();
	pack.error_estimate = 1;	// TODO:Multiplyer = 1.
	pack.sender_ttl = 255;

	serv_addr.sin_port = acc.Port;
	rv = sendto(testfd, &pack, sizeof(pack), 0,
				(struct sockaddr *)&serv_addr, sizeof(serv_addr));

	int len;
	rv = recvfrom(testfd, &pack, sizeof(pack), 0,
				  (struct sockaddr *)&serv_addr, (socklen_t *) & len);

	rv = send_stop_session(servfd, kOK, 1);

	return 0;
}
