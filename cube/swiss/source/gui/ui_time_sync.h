#ifndef UI_TIME_SYNC_H
#define UI_TIME_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Setting the console's clock from an NTP server (Settings › Network): the
 * SNTP packets (RFC 4330) and the time zone's offset, with no console calls,
 * so the host tests run them. time_sync.c does the network and the clock. */

#define UI_TIME_SYNC_PACKET 48

/* A client request, version 4, carrying `cookie` as its transmit time; the
 * server's reply echoes it, which is how UITimeSync_Reply knows the reply is
 * this request's. */
void UITimeSync_Request(uint8_t packet[UI_TIME_SYNC_PACKET], uint64_t cookie);

/* The server's transmit time, in microseconds since 1970 UTC, from its reply
 * to the request carrying `cookie`. False for anything else: a short packet,
 * not a server's reply, an unsynchronised server, a Kiss-o'-Death, a reply to
 * another request, no time, or a time before 2000, where the console's
 * clock starts. */
bool UITimeSync_Reply(const uint8_t *packet, size_t length, uint64_t cookie,
	int64_t *utcMicroseconds);

/* Seconds east of UTC that a location name (America/Denver) or POSIX TZ
 * string `zone` gives at `utcSeconds`,
 * such as "MST7MDT,M3.2.0,M11.1.0" (US Mountain) or "IST-5:30" (India).
 * Daylight time needs both of its rules in the Mm.w.d form. False when `zone`
 * isn't one of these. Does not change the process environment or clock. */
bool UITimeSync_ZoneOffset(const char *zone, int64_t utcSeconds,
	int32_t *eastSeconds);

/* With no Time Zone, the console keeps its own: the offset its clock already
 * has from UTC, snapped to the nearest offset a real time zone uses. A drift
 * keeps the zone while it is nearer than any other: under half an hour for
 * US Mountain, whose neighbours are an hour away, but 7 minutes for Nepal,
 * 15 from India. A clock more than 14 and a half hours out has no zone to
 * keep, and gets UTC. */
int32_t UITimeSync_KeptOffset(int64_t consoleSeconds, int64_t utcSeconds);

#endif
