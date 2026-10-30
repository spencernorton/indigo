#include <time.h>
#include <unistd.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <ogc/irq.h>
#include <ogc/lwp.h>
#include <string.h>

#include "swiss.h"
#include "time_sync.h"
#include "ui_time_sync.h"
#include "util.h"

#define TIME_SYNC_TRIES 3	/* a second each: the first can go before ARP has an answer */

/* What the thread asks, copied on the network thread: Settings waits for
 * that thread, so it can't change them underneath. */
static struct in_addr timeSyncServer;
static char timeSyncZone[sizeof(swissSettings.timeZone)];
static volatile enum { TIME_SYNC_IDLE, TIME_SYNC_ASKING, TIME_SYNC_STEPPING } timeSyncState;
static volatile bool timeSyncClosed;
static int32_t timeSyncOffset;
static bool timeSyncOffsetKnown;

/* One SNTP request, a second's wait for the reply, three tries. The reply is
 * the server's time; half the round trip and the time since bring it to now. */
static bool time_sync_ask(struct in_addr server, int64_t *utcMicroseconds)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET, .sin_port = htons(123), .sin_addr = server,
	};
	s32 fd = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	bool answered = false;

	if(fd < 0) {
		return false;
	}
	if(net_connect(fd, (struct sockaddr *)&address, sizeof(address)) >= 0) {
		for(int i = 0; i < TIME_SYNC_TRIES && !answered; i++) {
			u8 packet[UI_TIME_SYNC_PACKET];
			u64 sent = gettime();
			UITimeSync_Request(packet, sent);
			if(net_send(fd, packet, sizeof(packet), 0) < 0) {
				break;
			}
			while(!answered) {
				struct timeval wait = {1, 0};
				fd_set readable;
				FD_ZERO(&readable);
				FD_SET(fd, &readable);
				if(net_select(fd + 1, &readable, NULL, NULL, &wait) <= 0) {
					break;
				}
				s32 length = net_recv(fd, packet, sizeof(packet), 0);
				if(length < 0) {
					break;
				}
				u64 received = gettime();
				answered = UITimeSync_Reply(packet, (size_t)length, sent, utcMicroseconds);
				if(answered) {
					*utcMicroseconds += (int64_t)(ticks_to_microsecs(diff_ticks(sent, received)) / 2 +
						ticks_to_microsecs(diff_ticks(received, gettime())));
				}
			}
		}
	}
	net_close(fd);
	return answered;
}

/* The console keeps local time with no zone, and so does the IPL: the time base
 * while it runs, and after a restart the RTC's seconds plus the SRAM's counter
 * bias. Both move, so games and the IPL read the new time too. */
static void time_sync_set(void)
{
	int64_t utc;
	int32_t east;
	u32 rtc;

	if(!time_sync_ask(timeSyncServer, &utc)) {
		print_debug("Sync Clock: no answer from the NTP server\n");
		return;
	}
	int64_t utcSeconds = utc / 1000000;
	if(timeSyncZone[0] == '\0') {
		east = UITimeSync_KeptOffset(time(NULL), utcSeconds);
	}
	else if(!UITimeSync_ZoneOffset(timeSyncZone, utcSeconds, &east)) {
		print_debug("Sync Clock: Time Zone %s isn't a POSIX TZ string\n", timeSyncZone);
		return;
	}
	/* A game's launch that began meanwhile keeps the clock as it is. */
	u32 level = IRQ_Disable();
	bool open = !timeSyncClosed;
	if(open) timeSyncState = TIME_SYNC_STEPPING;
	IRQ_Restore(level);
	if(!open) {
		print_debug("Sync Clock: a game started first, so the clock stays\n");
		return;
	}
	int64_t local = utc + (int64_t)east * 1000000;
	struct timespec now = {
		.tv_sec = (time_t)(local / 1000000), .tv_nsec = (long)(local % 1000000) * 1000,
	};
	struct timespec was;
	clock_gettime(CLOCK_REALTIME, &was);
	if(clock_settime(CLOCK_REALTIME, &now) == 0) {
		level = IRQ_Disable();
		timeSyncOffset = east;
		timeSyncOffsetKnown = true;
		IRQ_Restore(level);
	}
	print_debug("Sync Clock: moved the clock %lld ms\n", (long long)(now.tv_sec - was.tv_sec) * 1000 +
		(now.tv_nsec - was.tv_nsec) / 1000000);
	/* __SYS_GetRTC gives up while another device holds EXI channel 0 (an
	 * adapter sending, a card mid-transfer). Try for a moment longer: the
	 * counter bias is what keeps this time over a power-off. */
	bool haveRtc = false;
	for(int i = 0; i < 50 && !(haveRtc = __SYS_GetRTC(&rtc)); i++) {
		usleep(2000);
	}
	if(haveRtc) {
		SYS_SetCounterBias((u32)(now.tv_sec - DIFF_SEC_1970_2000) - rtc);
		while(!__SYS_SyncSram());
	}
}

static void *time_sync_thread(void *unused)
{
	(void)unused;
	LWP_DetachThread(LWP_THREAD_NULL);
	time_sync_set();
	timeSyncState = TIME_SYNC_IDLE;
	return NULL;
}

void time_sync_network(void)
{
	lwp_t thread;

	if(!swissSettings.ntpSync || !inet_aton(swissSettings.ntpHostIp[0] ?
		swissSettings.ntpHostIp : TIME_SYNC_DEFAULT_SERVER, &timeSyncServer)) {
		return;
	}
	memcpy(timeSyncZone, swissSettings.timeZone, sizeof(timeSyncZone));
	timeSyncZone[sizeof(timeSyncZone) - 1] = '\0';
	timeSyncState = TIME_SYNC_ASKING;
	if(LWP_CreateThread(&thread, time_sync_thread, NULL, NULL, 0, LWP_PRIO_LOWEST) != 0) {
		timeSyncState = TIME_SYNC_IDLE;
	}
}

void time_sync_wait(void)
{
	while(timeSyncState != TIME_SYNC_IDLE) {
		usleep(10000);
	}
}

void time_sync_close(void)
{
	timeSyncClosed = true;
	while(timeSyncState == TIME_SYNC_STEPPING) {
		usleep(1000);
	}
}

bool time_sync_offset(int32_t *eastSeconds)
{
	if(eastSeconds == NULL) return false;
	u32 level = IRQ_Disable();
	bool known = timeSyncOffsetKnown;
	if(known) *eastSeconds = timeSyncOffset;
	IRQ_Restore(level);
	return known;
}
