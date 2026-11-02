#ifndef TIME_SYNC_H
#define TIME_SYNC_H
#include <stdbool.h>
#include <stdint.h>

/* The NTP server when NTP Server IP is empty: time.cloudflare.com's address,
 * since the console has no DNS. */
#define TIME_SYNC_DEFAULT_SERVER "162.159.200.123"

/* Settings › Network's Sync Clock: from the network thread, once the network
 * is up, sets the console's clock from the NTP server, on a thread of its
 * own so wait_network() doesn't wait for an unanswered server (3 s). */
void time_sync_network(void);

/* Until Sync Clock is done: init_network() waits, so a network device never
 * has the clock stepped under its timeouts. */
void time_sync_wait(void);

/* A game's launch, before it times anything or hands the game the clock:
 * Sync Clock no longer steps it this session (a step under way finishes). */
void time_sync_close(void);

/* The offset actually applied to the console local clock, when known. */
bool time_sync_offset(int32_t *eastSeconds);

#endif
