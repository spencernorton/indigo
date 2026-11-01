#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_time_sync.h"
#include "tzdata.h"

static unsigned long checks;

#define CHECK(condition) do { 	checks++; 	if(!(condition)) { 		fprintf(stderr, "check failed at %s:%d: %s\n", 			__FILE__, __LINE__, #condition); 		exit(1); 	} } while(0)

/* Offsets from the IANA database (Python's zoneinfo) for each zone's POSIX
 * string: both sides of every change in 2026 and 2037, and three fixed times. */
static const struct {
	const char *zone;
	int64_t utc;
	int32_t east;
} ZONES[] = {
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1772960399), -25200},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1772960400), -21600},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1793519999), -21600},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1793520000), -25200},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(2120115599), -25200},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(2120115600), -21600},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(2140675199), -21600},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(2140675200), -25200},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1792432800), -21600},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1798763400), -25200},
	{"MST7MDT,M3.2.0,M11.1.0", INT64_C(1798722000), -25200},
	{"MST7", INT64_C(1792432800), -25200},
	{"MST7", INT64_C(1798763400), -25200},
	{"MST7", INT64_C(1798722000), -25200},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1774745999), 0},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1774746000), 3600},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1792889999), 3600},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1792890000), 0},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(2121901199), 0},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(2121901200), 3600},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(2140045199), 3600},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(2140045200), 0},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1792432800), 3600},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1798763400), 0},
	{"GMT0BST,M3.5.0/1,M10.5.0", INT64_C(1798722000), 0},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1774745999), 3600},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1774746000), 7200},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1792889999), 7200},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1792890000), 3600},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(2121901199), 3600},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(2121901200), 7200},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(2140045199), 7200},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(2140045200), 3600},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1792432800), 7200},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1798763400), 3600},
	{"CET-1CEST,M3.5.0,M10.5.0/3", INT64_C(1798722000), 3600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1775318399), 39600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1775318400), 36000},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1791043199), 36000},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1791043200), 39600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(2122473599), 39600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(2122473600), 36000},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(2138198399), 36000},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(2138198400), 39600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1792432800), 39600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1798763400), 39600},
	{"AEST-10AEDT,M10.1.0,M4.1.0/3", INT64_C(1798722000), 39600},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1775311199), 46800},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1775311200), 43200},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1790431199), 43200},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1790431200), 46800},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(2122466399), 46800},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(2122466400), 43200},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(2137586399), 43200},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(2137586400), 46800},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1792432800), 46800},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1798763400), 46800},
	{"NZST-12NZDT,M9.5.0,M4.1.0/3", INT64_C(1798722000), 46800},
	{"IST-5:30", INT64_C(1792432800), 19800},
	{"IST-5:30", INT64_C(1798763400), 19800},
	{"IST-5:30", INT64_C(1798722000), 19800},
	{"<-03>3", INT64_C(1792432800), -10800},
	{"<-03>3", INT64_C(1798763400), -10800},
	{"<-03>3", INT64_C(1798722000), -10800},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1774745999), -7200},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1774746000), -3600},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1792889999), -3600},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1792890000), -7200},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(2121901199), -7200},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(2121901200), -3600},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(2140045199), -3600},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(2140045200), -7200},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1792432800), -3600},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1798763400), -7200},
	{"<-02>2<-01>,M3.5.0/-1,M10.5.0/0", INT64_C(1798722000), -7200},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1774569599), 7200},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1774569600), 10800},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1792882799), 10800},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1792882800), 7200},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(2121724799), 7200},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(2121724800), 10800},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(2140037999), 10800},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(2140038000), 7200},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1792432800), 10800},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1798763400), 7200},
	{"IST-2IDT,M3.4.4/26,M10.5.0", INT64_C(1798722000), 7200},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1775320199), 37800},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1775320200), 34200},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1791044999), 34200},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1791045000), 37800},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(2122475399), 37800},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(2122475400), 34200},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(2138200199), 34200},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(2138200200), 37800},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1792432800), 37800},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1798763400), 37800},
	{"ACST-9:30ACDT,M10.1.0,M4.1.0/3", INT64_C(1798722000), 37800},
	{"<+0545>-5:45", INT64_C(1792432800), 20700},
	{"<+0545>-5:45", INT64_C(1798763400), 20700},
	{"<+0545>-5:45", INT64_C(1798722000), 20700},
};

static void checkZones(void)
{
	for(size_t i = 0; i < sizeof(ZONES) / sizeof(ZONES[0]); i++) {
		int32_t east = 99;
		CHECK(UITimeSync_ZoneOffset(ZONES[i].zone, ZONES[i].utc, &east));
		if(east != ZONES[i].east) {
			fprintf(stderr, "%s at %lld: %d, not %d\n", ZONES[i].zone,
				(long long)ZONES[i].utc, (int)east, (int)ZONES[i].east);
		}
		CHECK(east == ZONES[i].east);
	}
}

static unsigned checkLocationTree(const struct TZData *tree, const char *prefix)
{
	unsigned leaves = 0;
	for(int i = 0; i < tree->count; i++) {
		const struct TZData *entry = &tree->timezones[i];
		char name[64];
		int n = snprintf(name, sizeof(name), "%s%s", prefix, entry->name);
		CHECK(n > 0 && (size_t)n < sizeof(name));
		if(entry->count) leaves += checkLocationTree(entry, name);
		else if(entry->timezone) {
			CHECK(!strcmp(get_timezone(name), entry->timezone));
			const int64_t dates[] = {INT64_C(1768478400), INT64_C(1784116800)};
			for(unsigned j = 0; j < sizeof(dates) / sizeof(dates[0]); j++) {
				int32_t named, posix;
				CHECK(UITimeSync_ZoneOffset(name, dates[j], &named));
				CHECK(UITimeSync_ZoneOffset(entry->timezone, dates[j], &posix));
				CHECK(named == posix);
			}
			leaves++;
		}
	}
	return leaves;
}

static void checkLocations(void)
{
	CHECK(setenv("TZ", "UTC0", 1) == 0);
	static const struct { const char *name; int64_t utc; int32_t east; } known[] = {
		{"America/Denver", INT64_C(1772960399), -25200},
		{"America/Denver", INT64_C(1772960400), -21600},
		{"America/Denver", INT64_C(1793519999), -21600},
		{"America/Denver", INT64_C(1793520000), -25200},
		{"Europe/London", INT64_C(1774745999), 0},
		{"Europe/London", INT64_C(1774746000), 3600},
		{"Asia/Kolkata", INT64_C(1792432800), 19800},
		{"Asia/Kathmandu", INT64_C(1792432800), 20700},
		{"Australia/Lord_Howe", INT64_C(1792432800), 39600},
		{"Etc/GMT+12", INT64_C(1792432800), -43200},
		{"Etc/UTC", INT64_C(1792432800), 0},
		{"America/Havana", INT64_C(1793509199), -14400},
		{"America/Havana", INT64_C(1793509200), -14400},
		{"America/Havana", INT64_C(1800014400), -14400},
		{"America/Havana", INT64_C(1825563599), -14400},
		{"America/Havana", INT64_C(1825563600), -18000},
		{"America/Havana", INT64_C(1825563601), -18000},
	};
	for(unsigned i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
		int32_t east = 99;
		CHECK(UITimeSync_ZoneOffset(known[i].name, known[i].utc, &east));
		CHECK(east == known[i].east);
	}
	CHECK(checkLocationTree(&TZData, "") > 400);
	CHECK(!UITimeSync_ZoneOffset("America/Denver", 0, NULL));
	CHECK(getenv("TZ") != NULL && !strcmp(getenv("TZ"), "UTC0"));
	/* An explicit POSIX rule is the caller's choice, including in this interval. */
	int32_t east;
	CHECK(UITimeSync_ZoneOffset("CST5CDT,M3.2.0/0,M11.1.0/1", INT64_C(1800014400), &east));
	CHECK(east == -18000);
}

static void checkBadZones(void)
{
	static const char *const bad[] = {
		"", "MST", "M7", "<>7", "<+05-5", "MST25", "MST7:60", "MST7x",
		"EST5EDT", "MST7MDT,M3.2.0", "MST7MDT,M13.2.0,M11.1.0",
		"MST7MDT,M3.6.0,M11.1.0", "MST7MDT,M3.2.7,M11.1.0",
		"MST7MDT,J60,M11.1.0", "MST7MDT,M3.2.0,M11.1.0,", "MST7MDT,M3.2.0/168,M11.1.0",
		"MST7MDT,M3.2.0,M11.1.0 ", "America", "America/", "America/No_Such_City",
		"America/Denver/Extra", "Etc/Unknown", "/", "//", "../America/Denver",
	};
	int32_t east = 99;

	CHECK(!UITimeSync_ZoneOffset(NULL, 0, &east));
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(!UITimeSync_ZoneOffset(bad[i], INT64_C(1792432800), &east));
	}
	CHECK(east == 99);
	CHECK(UITimeSync_ZoneOffset("UTC0", INT64_C(1792432800), &east) && east == 0);
	CHECK(UITimeSync_ZoneOffset("XYZ-14", 0, &east) && east == 14 * 3600);
}

static void put64(uint8_t *p, uint64_t value)
{
	for(int i = 0; i < 8; i++) {
		p[i] = (uint8_t)(value >> (56 - 8 * i));
	}
}

/* What a server sends back for `request`, transmitting at `transmit`. */
static void reply(uint8_t out[UI_TIME_SYNC_PACKET],
	const uint8_t request[UI_TIME_SYNC_PACKET], uint64_t transmit)
{
	memset(out, 0, UI_TIME_SYNC_PACKET);
	out[0] = 0x24;	/* no leap warning, version 4, server */
	out[1] = 2;
	memcpy(out + 24, request + 40, 8);
	put64(out + 40, transmit);
}

static void checkPackets(void)
{
	const uint64_t cookie = UINT64_C(0x0123456789abcdef);
	uint8_t request[UI_TIME_SYNC_PACKET], answer[UI_TIME_SYNC_PACKET];
	int64_t utc = 0;

	UITimeSync_Request(request, cookie);
	CHECK(request[0] == 0x23);
	for(int i = 1; i < 40; i++) {
		CHECK(request[i] == 0);
	}
	CHECK(request[40] == 0x01 && request[47] == 0xef);

	/* 2026-10-19 18:00:00.5 UTC */
	reply(answer, request, (UINT64_C(1792432800) + UINT64_C(2208988800)) << 32 | 0x80000000u);
	CHECK(UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	CHECK(utc == INT64_C(1792432800500000));
	CHECK(!UITimeSync_Reply(answer, sizeof(answer) - 1, cookie, &utc));
	CHECK(!UITimeSync_Reply(NULL, sizeof(answer), cookie, &utc));
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie + 1, &utc));

	/* NTP's seconds wrap in 2036: 2040-01-01 (Unix 2208988800) is era 1. */
	reply(answer, request, (UINT64_C(2208988800) + UINT64_C(2208988800) - UINT64_C(4294967296)) << 32);
	CHECK(UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	CHECK(utc == INT64_C(2208988800) * 1000000);

	reply(answer, request, UINT64_C(0xe000000000000000));
	answer[0] = 0xe4;	/* leap 3: the server has no time */
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	answer[0] = 0x23;	/* a client's request, not a reply */
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	answer[0] = 0x24;
	answer[1] = 0;	/* Kiss-o'-Death */
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	answer[1] = 16;
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	answer[1] = 1;
	CHECK(UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	put64(answer + 40, 0);
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));

	/* A server whose own clock was never set says 1970; the console's
	 * clock starts at 2000, which it can take. */
	reply(answer, request, UINT64_C(2208988800) << 32);
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	reply(answer, request, (UINT64_C(946684799) + UINT64_C(2208988800)) << 32);
	CHECK(!UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	reply(answer, request, (UINT64_C(946684800) + UINT64_C(2208988800)) << 32);
	CHECK(UITimeSync_Reply(answer, sizeof(answer), cookie, &utc));
	CHECK(utc == INT64_C(946684800) * 1000000);
}

static void checkKeptOffset(void)
{
	const int64_t utc = INT64_C(1792432800);

	CHECK(UITimeSync_KeptOffset(utc, utc) == 0);
	CHECK(UITimeSync_KeptOffset(utc - 6 * 3600 + 437, utc) == -6 * 3600);
	CHECK(UITimeSync_KeptOffset(utc - 6 * 3600 - 449, utc) == -6 * 3600);
	/* A Denver console 8 minutes fast (seen on a real one) keeps -6:00, not
	 * -5:45; a quarter-hour rounding would have picked a zone nobody uses. */
	CHECK(UITimeSync_KeptOffset(utc - 6 * 3600 + 8 * 60, utc) == -6 * 3600);
	CHECK(UITimeSync_KeptOffset(utc - 6 * 3600 - 451, utc) == -6 * 3600);
	CHECK(UITimeSync_KeptOffset(utc - 6 * 3600 + 29 * 60, utc) == -6 * 3600);
	CHECK(UITimeSync_KeptOffset(utc - 6 * 3600 - 29 * 60, utc) == -6 * 3600);
	CHECK(UITimeSync_KeptOffset(utc + 25 * 60, utc) == 0);
	/* Half- and quarter-hour zones are kept when the clock is close to them. */
	CHECK(UITimeSync_KeptOffset(utc + 5 * 3600 + 1800 + 30, utc) == 5 * 3600 + 1800);
	CHECK(UITimeSync_KeptOffset(utc + 5 * 3600 + 2700 - 300, utc) == 5 * 3600 + 2700);
	CHECK(UITimeSync_KeptOffset(utc - 3 * 3600 - 1800 + 200, utc) == -3 * 3600 - 1800);
	CHECK(UITimeSync_KeptOffset(utc + 9 * 3600 + 1800 - 400, utc) == 9 * 3600 + 1800);
	/* A drift that leaves another zone nearer takes it: a half-hour zone beside
	 * a whole hour, or India for a Nepal clock 8 minutes slow. */
	CHECK(UITimeSync_KeptOffset(utc + 3 * 3600 + 1700, utc) == 3 * 3600 + 1800);
	CHECK(UITimeSync_KeptOffset(utc + 5 * 3600 + 2700 - 480, utc) == 5 * 3600 + 1800);
	CHECK(UITimeSync_KeptOffset(utc + 14 * 3600, utc) == 14 * 3600);
	CHECK(UITimeSync_KeptOffset(utc + 14 * 3600 + 1700, utc) == 14 * 3600);
	CHECK(UITimeSync_KeptOffset(utc - 14 * 3600, utc) == -12 * 3600);
	CHECK(UITimeSync_KeptOffset(utc + 15 * 3600, utc) == 0);
	CHECK(UITimeSync_KeptOffset(utc - 15 * 3600, utc) == 0);
	/* A console whose clock went back to 2000 has no zone to keep. */
	CHECK(UITimeSync_KeptOffset(INT64_C(946684800), utc) == 0);
}

int main(void)
{
	checkZones();
	checkLocations();
	checkBadZones();
	checkPackets();
	checkKeptOffset();
	printf("test_ui_time_sync: %lu checks passed\n", checks);
	return 0;
}
