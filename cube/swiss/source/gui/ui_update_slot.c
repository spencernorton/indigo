#include <string.h>
#include "ui_update_slot.h"

int UIUpdate_FirstSlot(const ui_update_slots_t *slots)
{
	return slots->confirmed[1] > slots->confirmed[0] ? 1 : 0;
}

int UIUpdate_InstallSlot(const ui_update_slots_t *slots, int runningSlot,
	const ui_update_manifest_t *release, bool automatic)
{
	int best = UIUpdate_FirstSlot(slots);
	if(automatic && (!strcmp(slots->tried[0], release->commit) ||
		!strcmp(slots->tried[1], release->commit) ||
		slots->confirmed[best] >= release->buildTime))
		return -1;
	if(runningSlot == 0 || runningSlot == 1)
		return 1 - runningSlot;
	return slots->confirmed[best] == 0 ? 0 : 1 - best;
}
