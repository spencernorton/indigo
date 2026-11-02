/* Fuzz what the clock sync reads: the Time Zone from global.ini, and the NTP
 * server's reply off the network. The bytes are copied to exactly their size
 * (the zone with its terminating NUL), so a read past either is a finding. A
 * zone that parses gives an offset a real zone can have, at any time; a
 * reply that is accepted names a time NTP's two eras can carry, 1968 to 2104. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ui_time_sync.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const int64_t when[] = {
		INT64_C(-2208988800), 0, INT64_C(1792432800),
		INT64_C(1793509200), INT64_C(1800014400), INT64_C(1825563600),
		INT64_C(4294967295),
		INT64_C(6370000000),
	};
	char *zone = malloc(size + 1);
	uint8_t *reply = malloc(size ? size : 1);
	int64_t utc;

	if(!zone || !reply)
		abort();
	memcpy(zone, data, size);
	zone[size] = '\0';
	for(size_t i = 0; i < sizeof(when) / sizeof(when[0]); i++) {
		int32_t east;
		if(UITimeSync_ZoneOffset(zone, when[i], &east) &&
			(east < -25 * 3600 || east > 25 * 3600))
			abort();
	}
	memcpy(reply, data, size);
	if(size >= 8 && UITimeSync_Reply(reply, size, 0, &utc) &&
		(utc < INT64_C(-61505152) * 1000000 || utc >= INT64_C(4233462144) * 1000000))
		abort();
	free(zone);
	free(reply);
	return 0;
}
