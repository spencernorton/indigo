#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_update.h"

int main(void)
{
	const uint32_t boundary = 2u * 1024u * 1024u;
	assert(!UIUpdate_RenewTransfer(true, 0, 0));
	assert(!UIUpdate_RenewTransfer(true, 0, boundary - 1u));
	assert(UIUpdate_RenewTransfer(true, boundary - 1u, boundary));
	assert(!UIUpdate_RenewTransfer(true, boundary, boundary + 32768u));
	assert(UIUpdate_RenewTransfer(true, 2u * boundary - 17u, 2u * boundary + 12u));
	assert(!UIUpdate_RenewTransfer(false, 0, UI_UPDATE_DOL_MAX));

	/* The published Melee manual at 10 KiB/s takes longer than ten
	 * minutes. Each new window still requires actual card progress. */
	const uint32_t manual = 23963328u;
	uint32_t written = 0, seconds = 0, renewed = 0, longest = 0;
	while(written < manual) {
		uint32_t count = manual - written < 32768u ? manual - written : 32768u;
		uint32_t previous = written;
		written += count;
		seconds = written / 10240u;
		if(seconds - renewed > longest)
			longest = seconds - renewed;
		if(UIUpdate_RenewTransfer(true, previous, written))
			renewed = seconds;
	}
	assert(seconds > 600u && renewed > 600u && longest < 600u);
	puts("test_ui_transfer: firmware deadline and sustained game content passed");
	return 0;
}
