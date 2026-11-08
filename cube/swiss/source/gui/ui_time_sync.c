#include <string.h>

#include "ui_time_sync.h"
#include "tzdata.h"

#define NTP_TO_UNIX INT64_C(2208988800)	/* 1900 to 1970, in seconds */
#define UNIX_2000 INT64_C(946684800)	/* where the console's clock starts */
#define DAY 86400
#define HOUR 3600

typedef struct {
	int month, week, weekday;	/* Mm.w.d: week 5 is the last */
	int32_t seconds;	/* local time of day it changes at */
} zoneRule_t;

static uint64_t readBig64(const uint8_t *p)
{
	uint64_t value = 0;
	for(int i = 0; i < 8; i++) {
		value = value << 8 | p[i];
	}
	return value;
}

void UITimeSync_Request(uint8_t packet[UI_TIME_SYNC_PACKET], uint64_t cookie)
{
	memset(packet, 0, UI_TIME_SYNC_PACKET);
	packet[0] = 0x23;	/* no leap warning, version 4, client */
	for(int i = 0; i < 8; i++) {
		packet[40 + i] = (uint8_t)(cookie >> (56 - 8 * i));
	}
}

bool UITimeSync_Reply(const uint8_t *packet, size_t length, uint64_t cookie,
	int64_t *utcMicroseconds)
{
	if(packet == NULL || length < UI_TIME_SYNC_PACKET) {
		return false;
	}
	/* Leap 3 is a server with no time; stratum 0 a Kiss-o'-Death. */
	if((packet[0] >> 6) == 3 || (packet[0] & 7) != 4 ||
		packet[1] == 0 || packet[1] > 15 || readBig64(packet + 24) != cookie) {
		return false;
	}
	uint64_t transmit = readBig64(packet + 40);
	if(transmit == 0) {
		return false;
	}
	/* RFC 4330 section 3: seconds with the top bit clear are from 2036 on. */
	uint32_t seconds = (uint32_t)(transmit >> 32);
	int64_t unixSeconds = (int64_t)seconds - NTP_TO_UNIX +
		((seconds & 0x80000000u) ? 0 : INT64_C(1) << 32);
	/* The console's clock counts from 2000: a server whose own clock was
	 * never set (it says 1970) has no time to give it. */
	if(unixSeconds < UNIX_2000) {
		return false;
	}
	*utcMicroseconds = unixSeconds * 1000000 +
		(int64_t)(((transmit & 0xffffffffu) * 1000000u) >> 32);
	return true;
}

static int64_t floorDiv(int64_t a, int64_t b)
{
	return a / b - (a % b < 0);
}

/* Days since 1970-01-01, and back (Howard Hinnant's civil calendar). */
static int64_t daysFromCivil(int64_t year, int month, int day)
{
	year -= month <= 2;
	int64_t era = floorDiv(year, 400);
	int64_t yearOfEra = year - era * 400;
	int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
	return era * 146097 + yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 +
		dayOfYear - 719468;
}

static int64_t yearFromDays(int64_t days)
{
	days += 719468;
	int64_t era = floorDiv(days, 146097);
	int64_t dayOfEra = days - era * 146097;
	int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
		dayOfEra / 146096) / 365;
	int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
	return yearOfEra + era * 400 + ((5 * dayOfYear + 2) / 153 >= 10);
}

/* A name: three or more letters, or anything in <> such as <+0530>. */
static const char *zoneName(const char *p)
{
	const char *start = p;
	if(*p == '<') {
		while(*p != '\0' && *p != '>') {
			p++;
		}
		return *p == '>' && p - start >= 4 ? p + 1 : NULL;
	}
	while((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
		p++;
	}
	return p - start >= 3 ? p : NULL;
}

/* [+-]hh[:mm[:ss]], in seconds. */
static const char *zoneSeconds(const char *p, int32_t maxHours, int32_t *seconds)
{
	int32_t sign = 1, part[3] = {0, 0, 0};
	if(*p == '+' || *p == '-') {
		sign = *p++ == '-' ? -1 : 1;
	}
	for(int i = 0; i < 3; i++) {
		if(i > 0) {
			if(*p != ':') {
				break;
			}
			p++;
		}
		if(*p < '0' || *p > '9') {
			return NULL;
		}
		for(int digits = 0; *p >= '0' && *p <= '9'; p++) {
			if(++digits > 3) {
				return NULL;
			}
			part[i] = part[i] * 10 + (*p - '0');
		}
	}
	if(part[0] > maxHours || part[1] > 59 || part[2] > 59) {
		return NULL;
	}
	*seconds = sign * (part[0] * HOUR + part[1] * 60 + part[2]);
	return p;
}

static const char *zoneDigit(const char *p, int low, int high, int *value)
{
	if(*p < '0' || *p > '9') {
		return NULL;
	}
	*value = *p++ - '0';
	if(*p >= '0' && *p <= '9') {
		*value = *value * 10 + (*p++ - '0');
	}
	return *value >= low && *value <= high ? p : NULL;
}

/* ,Mm.w.d[/time] */
static const char *zoneRule(const char *p, zoneRule_t *rule)
{
	rule->seconds = 2 * HOUR;
	if(*p++ != ',' || *p++ != 'M' ||
		(p = zoneDigit(p, 1, 12, &rule->month)) == NULL || *p++ != '.' ||
		(p = zoneDigit(p, 1, 5, &rule->week)) == NULL || *p++ != '.' ||
		(p = zoneDigit(p, 0, 6, &rule->weekday)) == NULL) {
		return NULL;
	}
	return *p == '/' ? zoneSeconds(p + 1, 167, &rule->seconds) : p;
}

/* The day, in days since 1970, a rule falls on in `year`. */
static int64_t ruleDay(int64_t year, const zoneRule_t *rule)
{
	int64_t first = daysFromCivil(year, rule->month, 1);
	int64_t next = rule->month == 12 ? daysFromCivil(year + 1, 1, 1) :
		daysFromCivil(year, rule->month + 1, 1);
	int64_t firstWeekday = (first % 7 + 11) % 7;	/* 1970-01-01 was a Thursday */
	int64_t day = first + (rule->weekday - firstWeekday + 7) % 7 + 7 * (rule->week - 1);
	while(day >= next) {
		day -= 7;
	}
	return day;
}

bool UITimeSync_ZoneOffset(const char *zone, int64_t utcSeconds,
	int32_t *eastSeconds)
{
	/* Resolve upstream's location names without changing libc's process-wide
	 * TZ: Indigo and the IPL keep a local clock, already shifted by this offset. */
	if(zone == NULL || eastSeconds == NULL) return false;
	if(strchr(zone, '/') != NULL && strchr(zone, ',') == NULL) {
		/* IANA 2026f cancels Havana's November 2026 fallback, keeping -04
		 * until 2027-11-07 05:00 UTC. A recurring POSIX tail cannot express
		 * this temporary interval. Explicit POSIX settings keep their rules. */
		if(!strcmp(zone, "America/Havana") &&
			utcSeconds >= INT64_C(1793509200) && utcSeconds < INT64_C(1825563600)) {
			*eastSeconds = -4 * HOUR;
			return true;
		}
		zone = get_timezone(zone);
		if(zone == NULL) return false;
	}
	int32_t standardWest, daylightWest;
	zoneRule_t start, end;
	const char *p = zone != NULL ? zoneName(zone) : NULL;

	if(p == NULL || (p = zoneSeconds(p, 24, &standardWest)) == NULL) {
		return false;
	}
	if(*p == '\0') {
		*eastSeconds = -standardWest;
		return true;
	}
	daylightWest = standardWest - HOUR;
	if((p = zoneName(p)) == NULL ||
		(*p != ',' && (p = zoneSeconds(p, 24, &daylightWest)) == NULL) ||
		(p = zoneRule(p, &start)) == NULL || (p = zoneRule(p, &end)) == NULL ||
		*p != '\0') {
		return false;
	}
	/* Daylight time starts at standard local time and ends at daylight
	 * local time; south of the equator it spans the new year. */
	int64_t year = yearFromDays(floorDiv(utcSeconds - standardWest, DAY));
	int64_t on = ruleDay(year, &start) * DAY + start.seconds + standardWest;
	int64_t off = ruleDay(year, &end) * DAY + end.seconds + daylightWest;
	bool daylight = on < off ? utcSeconds >= on && utcSeconds < off :
		utcSeconds >= on || utcSeconds < off;
	*eastSeconds = -(daylight ? daylightWest : standardWest);
	return true;
}

bool UITimeSync_KeepsOffset(const char *zone)
{
	return zone[0] == '\0' || strcmp(zone, "Etc/Unknown") == 0;
}

/* Every offset a time zone uses, standard or daylight, in minutes east of UTC
 * (the IANA database, 2026). Snapping to these rather than to any quarter hour
 * lets a clock that drifted half an hour or less keep a whole-hour zone. */
static const int16_t zoneOffsets[] = {
	-720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180,
	-150, -120, -60, 0, 60, 120, 180, 210, 240, 270, 300, 330, 345, 360, 390,
	420, 480, 525, 540, 570, 600, 630, 660, 720, 765, 780, 825, 840,
};

int32_t UITimeSync_KeptOffset(int64_t consoleSeconds, int64_t utcSeconds)
{
	int64_t east = consoleSeconds - utcSeconds, best = 0, nearest = INT64_MAX;

	if(east < -14 * HOUR - 1800 || east > 14 * HOUR + 1800) {
		return 0;
	}
	for(size_t i = 0; i < sizeof(zoneOffsets) / sizeof(zoneOffsets[0]); i++) {
		int64_t offset = zoneOffsets[i] * INT64_C(60);
		int64_t distance = east > offset ? east - offset : offset - east;
		if(distance < nearest) {
			nearest = distance;
			best = offset;
		}
	}
	return (int32_t)best;
}
