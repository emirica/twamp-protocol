/*
 * Name: Emma Mirică
 * Project: TWAMP Protocol
 * Class: OSS
 * Email: emma.mirica@cti.pub.ro
 *
 * Source: timestamp.c
 * Note: contains helpful functions to get the timestamp
 * in the required TWAMP format.
 *
 */
#include "twamp.h"
#include <inttypes.h>
#include <sys/time.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>

void timeval_to_timestamp(struct timeval *tv, TWAMPTimestamp * ts)
{
    if (!tv || !ts)
        return;
    ts->integer = htonl(tv->tv_sec);
    ts->fractional = htonl(tv->tv_usec);
}

void timestamp_to_timeval(TWAMPTimestamp * ts, struct timeval *tv)
{
    if (!tv || !ts)
        return;

    tv->tv_sec = ntohl(ts->integer);
    tv->tv_usec = ntohl(ts->fractional);
}

TWAMPTimestamp get_timestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    TWAMPTimestamp ts;
    timeval_to_timestamp(&tv, &ts);
    return ts;
}

static uint64_t get_usec(TWAMPTimestamp ts)
{
    struct timeval tv;
    timestamp_to_timeval(&ts, &tv);

    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int get_actual_shutdown(struct timeval tv, struct timeval ts, TWAMPTimestamp t)
{
    /* If ts is 0 then no StopSessions message was received */
    if ((ts.tv_sec * 1000000 + ts.tv_usec) == 0)
        return 1;
    /* Else compute time difference */
    uint64_t current = tv.tv_sec * 1000000 + tv.tv_usec;
    uint64_t shutdown = ts.tv_sec * 1000000 + ts.tv_usec;
    uint64_t timeout = get_usec(t);

    /* This should be ok, as no difference is computed */
    if (current > shutdown + timeout)
        return 1;
    return 0;
}

/* This will check if the time difference is negative.
 * Most likely the time isn't synchronized on client and server */
static uint64_t get_time_difference(struct timeval *tv, struct timeval *ts)
{
    uint64_t tv_usec = tv->tv_sec * 1000000 + tv->tv_usec;
    uint64_t ts_usec = ts->tv_sec * 1000000 + ts->tv_usec;

    if (tv_usec < ts_usec) {
        fprintf(stderr, "Clocks aren't synchronized\n");
        return 0;
    }

    return tv_usec - ts_usec;
}

void print_metrics(int j, int port, ReflectorUPacket pack) {
    struct timeval recv_resp_time, send_time, recv_time, reflect_time;

    /* Get Time of the received TWAMP-Test response message */
    gettimeofday(&recv_resp_time, NULL);

    /* Get the timestamps from the reflected message */
    timestamp_to_timeval(&pack.sender_time, &send_time);
    timestamp_to_timeval(&pack.receive_time, &recv_time);
    timestamp_to_timeval(&pack.time, &reflect_time);

    /* Print different metrics */

    /* Compute round-trip */
    fprintf(stderr, "Round-trip time for TWAMP-Test packet %d for port %hd is %" PRIu64 " [usec]\n",
            j, port, get_time_difference(&recv_resp_time, &send_time));
    fprintf(stderr, "Receive time - Send time for TWAMP-Test"
            " packet %d for port %d is %" PRIu64 " [usec]\n", j, port,
            get_time_difference(&recv_time, &send_time));
    fprintf(stderr, "Reflect time - Send time for TWAMP-Test"
            " packet %d for port %d is %" PRIu64 " [usec]\n", j, port,
            get_time_difference(&reflect_time, &send_time));

}
