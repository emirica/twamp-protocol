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
