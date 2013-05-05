/*
 * Name: Emma Mirică
 * Project: TWAMP Protocol
 * Class: OSS
 * Email: emma.mirica@cti.pub.ro
 *
 * Source: server.c
 * Note: contains the TWAMP server implementation
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

#include "twamp.h"

#define MAX_CLIENTS 10
#define MAX_SESSIONS_PER_CLIENT 10

typedef enum {
	kOffline = 0,
	kConnected,
	kConfigured,
	kTesting
} ClientStatus;

struct active_session {
	int socket;
	RequestSession req;
};

struct client_info {
	ClientStatus status;
	int socket;
	struct sockaddr_in addr;
	int sess_no;
	struct active_session sessions[MAX_SESSIONS_PER_CLIENT];
};

int fd_max = 0;

int send_greeting(struct client_info *client)
{
	int socket = client->socket;
	ServerGreeting greet;
	memset(&greet, 0, sizeof(greet));
	greet.Modes = kModeUnauthenticated;
	int i;
	for (i = 0; i < 16; i++)
		greet.Challenge[i] = rand() % 16;
	for (i = 0; i < 16; i++)
		greet.Salt[i] = rand() % 16;
	greet.Count = (1 << 12);	// TODO: should it be more?
	int rv = send(socket, &greet, sizeof(greet), 0);
	if (rv < 0)
		perror("Failed to send GREET message");
	else {
		printf("Sent GREET message to %s. Result %d\n",
			   inet_ntoa(client->addr.sin_addr), rv);
	}
	return rv;
}

int receive_greet_response(struct client_info *client)
{
	int socket = client->socket;
	SetUpResponse resp;
	memset(&resp, 0, sizeof(resp));
	int rv = recv(socket, &resp, sizeof(resp) * 2, 0);
	if (rv < 0)
		perror("Failed to receive response");
	else {
		printf("Received GreetResponse message from %s. Result %d\n",
			   inet_ntoa(client->addr.sin_addr), rv);
		printf("Mode %d\n", resp.Mode);
	}
	return rv;
}

int send_start_serv(struct client_info *client, TWAMPTimestamp StartTime)
{
	int socket = client->socket;
	ServerStart msg;
	memset(&msg, 0, sizeof(msg));
	msg.Accept = kOK;
	msg.StartTime = StartTime;
	return send(socket, &msg, sizeof(msg), 0);
}

int send_accept_session(struct client_info *client, RequestSession * req,
						int *used_sockets)
{
	AcceptSession acc;
	memset(&acc, 0, sizeof(acc));

	if ((*used_sockets < 64) && (client->sess_no < MAX_SESSIONS_PER_CLIENT)) {
		int testfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (testfd < 0)
			perror("ERROR opening socket");

		struct sockaddr_in local_addr;
		memset(&local_addr, 0, sizeof(local_addr));
		local_addr.sin_family = AF_INET;
		local_addr.sin_addr.s_addr = INADDR_ANY;
		local_addr.sin_port = req->ReceiverPort;

		// TODO: check N times then send failure
		while (bind
			   (testfd, (struct sockaddr *)&local_addr,
				sizeof(struct sockaddr)) < 0)
			local_addr.sin_port = 20000 + rand() % 1000;

		req->ReceiverPort = local_addr.sin_port;
		acc.Accept = kOK;
		acc.Port = req->ReceiverPort;

		client->sessions[client->sess_no].socket = testfd;
		client->sessions[client->sess_no].req = *req;
		client->sess_no++;
	} else {
		acc.Accept = kTemporaryResourceLimitation;
	}

	int rv = send(client->socket, &acc, sizeof(acc), 0);
	return rv;
}

int receive_request_session(struct client_info *client, RequestSession * req,
							int *used_sockets)
{
	printf("Received REQ SESS\n");
	// TODO check
	int rv = send_accept_session(client, req, used_sockets);
	if (rv < 0)
		perror("ERROR in send accept");
	return rv;
}

int send_start_ack(struct client_info *client)
{
	StartACK ack;
	memset(&ack, 0, sizeof(ack));
	ack.Accept = kOK;
	return send(client->socket, &ack, sizeof(ack), 0);
}

int receive_start_sessions(struct client_info *client, StartSessions * req,
						   fd_set * read_fds)
{
	int rv = send_start_ack(client);
	int i;
	for (i = 0; i < client->sess_no; i++) {
		FD_SET(client->sessions[i].socket, read_fds);
		if (fd_max < client->sessions[i].socket)
			fd_max = client->sessions[i].socket;
	}
	return rv;
}

int receive_stop_sessions(struct client_info *client, StopSessions * req,
						  fd_set * read_fds)
{
	client->status = kConfigured;
	return 0;
}

int receive_test_message(struct client_info *client, int session_index)
{
	printf("RECEIVED test message\n");
	UPacket pack;
	memset(&pack, 0, sizeof(pack));
	struct sockaddr addr;
	int len;
	int rv =
		recvfrom(client->sessions[session_index].socket, &pack, sizeof(pack), 0,
				 &addr, (socklen_t *) & len);
	// TODO: check rv?
	pack.sender_seq_number = pack.seq_number;
	static uint32_t seq_nr = 0;
	pack.seq_number = seq_nr++;
	pack.sender_error_estimate = pack.error_estimate;
	pack.sender_time = pack.time;
	pack.receive_time = get_timestamp();
	pack.sender_ttl = 255;		// TODO: compute TTL with recvmsg

	rv = sendto(client->sessions[session_index].socket, &pack, sizeof(pack), 0,
				&addr, sizeof(addr));
	return rv;
}

void cleanup_client(struct client_info *client, fd_set * read_fds,
					int *used_sockets)
{
	printf("CLEANUP client\n");
	FD_CLR(client->socket, read_fds);
	close(client->socket);
	(*used_sockets)--;
	int i;
	for (i = 0; i < client->sess_no; i++) {
		FD_CLR(client->sessions[i].socket, read_fds);
		close(client->sessions[i].socket);
		(*used_sockets)--;
	}
	memset(client, 0, sizeof(struct client_info));
	client->status = kOffline;
}

int find_client(struct client_info *clients, int max_clients, int socketno)
{
	int i;
	for (i = 0; i < max_clients; i++)
		if (clients[i].status != kOffline && clients[i].socket == socketno)
			return i;
	return -1;
}

int find_empty_client(struct client_info *clients, int max_clients)
{
	int i;
	for (i = 0; i < max_clients; i++)
		if (clients[i].status == kOffline)
			return i;
	return -1;
}

int main(int argc, char *argv[])
{
	// TODO: check args

	TWAMPTimestamp StartTime = get_timestamp();
	int used_sockets = 0;

	int sockfd;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		perror("ERROR opening socket");

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(SERVER_PORT);

	used_sockets++;
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr))
		< 0)
		perror("ERROR on binding");

	listen(sockfd, MAX_CLIENTS);

	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(sockfd, &read_fds);
	fd_max = sockfd;

	struct client_info clients[MAX_CLIENTS];

	memset(clients, 0, MAX_CLIENTS * sizeof(struct client_info));

	unsigned int newsockfd;
	struct sockaddr_in client_addr;
	fd_set tmp_fds;
	FD_ZERO(&tmp_fds);
	struct timeval timeout;

	int rv;
	while (1) {
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		tmp_fds = read_fds;
		if (select(fd_max + 1, &tmp_fds, NULL, NULL, &timeout) < 0)
			perror("ERROR in select");

		if (FD_ISSET(sockfd, &tmp_fds)) {
			uint32_t client_len = sizeof(client_addr);
			if ((newsockfd = accept(sockfd,
									(struct sockaddr *)&client_addr,
									&client_len)) < 0) {
				perror("ERROR in accept");
			} else {
				int pos = find_empty_client(clients, MAX_CLIENTS);

				if (pos != -1) {
					clients[pos].status = kConnected;
					clients[pos].socket = newsockfd;
					clients[pos].addr = client_addr;
					clients[pos].sess_no = 0;
					used_sockets++;
					FD_SET(newsockfd, &read_fds);
					if (newsockfd > fd_max)
						fd_max = newsockfd;
				} else {
					;			// TODO send NAK
				}

				rv = send_greeting(&clients[pos]);
				if (rv <= 0) {
					clients[pos].status = kOffline;
					cleanup_client(&clients[pos], &read_fds, &used_sockets);
				}

			}
		}

		uint8_t buffer[4096];
		int i, j;
		for (i = 0; i < MAX_CLIENTS; i++)
			if (clients[i].status != kOffline)
				if (FD_ISSET(clients[i].socket, &tmp_fds)) {
					switch (clients[i].status) {
					case kConnected:
						rv = receive_greet_response(&clients[i]);

						if (rv <= 0) {
							cleanup_client(&clients[i], &read_fds,
										   &used_sockets);
							break;
						}

						rv = send_start_serv(&clients[i], StartTime);

						if (rv <= 0)
							cleanup_client(&clients[i], &read_fds,
										   &used_sockets);
						else
							clients[i].status = kConfigured;

						break;
					case kConfigured:
						memset(buffer, 0, 4096);
						rv = recv(clients[i].socket, buffer, 4096, 0);
						if (rv <= 0) {
							cleanup_client(&clients[i], &read_fds,
										   &used_sockets);
							break;
						}
						switch (buffer[0]) {
						case kStartSessions:
							rv = receive_start_sessions(&clients[i],
														(StartSessions *)
														buffer, &read_fds);
							clients[i].status = kTesting;
							break;
						case kRequestTWSession:
							rv = receive_request_session(&clients[i],
														 (RequestSession *)
														 buffer, &used_sockets);
							break;
						default:
							break;
						}

						if (rv <= 0)
							cleanup_client(&clients[i], &read_fds,
										   &used_sockets);

						break;
					case kTesting:
						memset(buffer, 0, 4096);
						rv = recv(clients[i].socket, buffer, 4096, 0);
						if (rv <= 0) {
							cleanup_client(&clients[i], &read_fds,
										   &used_sockets);
							break;
						}
						if (buffer[0] == kStopSessions) {
							rv = receive_stop_sessions(&clients[i],
													   (StopSessions *) buffer,
													   &read_fds);
							// TODO: check rv?
						}
						break;
					default:
						break;
					}
				}

		for (i = 0; i < MAX_CLIENTS; i++)
			if (clients[i].status == kTesting)
				for (j = 0; j < clients[i].sess_no; i++)
					if (FD_ISSET(clients[i].sessions[j].socket, &tmp_fds)) {
						rv = receive_test_message(&clients[i], j);
						// TODO: check rv?
					}
	}

	return 0;
}
