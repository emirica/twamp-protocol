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

void timeval_to_timestamp(const struct timeval *tv, TWAMPTimestamp *ts)
{
    if (!tv || !ts)
        return;

    /* Unix time to NTP */
    ts->integer = tv->tv_sec + 2208988800uL;
    ts->fractional = (uint32_t) ( (double)tv->tv_usec * ( (double)(1uLL<<32)\
                                / (double)1e6) );

    ts->integer = htonl(ts->integer);
    ts->fractional = htonl(ts->fractional);
}

void timestamp_to_timeval(const TWAMPTimestamp *ts, struct timeval *tv)
{
    if (!tv || !ts)
        return;
    
    TWAMPTimestamp ts_host_ord;

    ts_host_ord.integer = ntohl(ts->integer);
    ts_host_ord.fractional = ntohl(ts->fractional);

    /* NTP to Unix time */
    tv->tv_sec = ts_host_ord.integer - 2208988800uL;
    tv->tv_usec = (uint32_t) (double)ts_host_ord.fractional * (double)1e6\
                             / (double)(1uLL<<32);
}

TWAMPTimestamp get_timestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    TWAMPTimestamp ts;
    timeval_to_timestamp(&tv, &ts);
    return ts;
}

static uint64_t get_usec(const TWAMPTimestamp *ts)
{
    struct timeval tv;
    timestamp_to_timeval(ts, &tv);

    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int get_actual_shutdown(const struct timeval *tv, const struct timeval *ts, const TWAMPTimestamp *t)
{
    /* If ts is 0 then no StopSessions message was received */
    if ((ts->tv_sec * 1000000 + ts->tv_usec) == 0)
        return 1;
    /* Else compute time difference */
    uint64_t current = tv->tv_sec * 1000000 + tv->tv_usec;
    uint64_t shutdown = ts->tv_sec * 1000000 + ts->tv_usec;
    uint64_t timeout = get_usec(t);

    /* This should be ok, as no difference is computed */
    if (current > shutdown + timeout)
        return 1;
    return 0;
}

/* This will check if the time difference is negative.
 * Most likely the time isn't synchronized on client and server */
static uint64_t get_time_difference(const TWAMPTimestamp *tv, const TWAMPTimestamp *ts)
{
    uint64_t tv_usec = get_usec(tv);
    uint64_t ts_usec = get_usec(ts);

    if (tv_usec < ts_usec) {
        fprintf(stderr, "Clocks aren't synchronized\n");
        return 0;
    }

    return tv_usec - ts_usec;
}

void print_metrics(int j, int port, const ReflectorUPacket *pack) {

    /* Get Time of the received TWAMP-Test response message */
    TWAMPTimestamp recv_resp_time = get_timestamp();

    /* Print different metrics */

    /* Compute round-trip */
    fprintf(stderr, "Round-trip time for TWAMP-Test packet %d for port %hd is %" PRIu64 " [usec]\n",
            j, port, get_time_difference(&recv_resp_time, &pack->sender_time));
    fprintf(stderr, "Receive time - Send time for TWAMP-Test"
            " packet %d for port %d is %" PRIu64 " [usec]\n", j, port,
            get_time_difference(&pack->receive_time, &pack->sender_time));
    fprintf(stderr, "Reflect time - Send time for TWAMP-Test"
            " packet %d for port %d is %" PRIu64 " [usec]\n", j, port,
            get_time_difference(&pack->time, &pack->sender_time));

}
