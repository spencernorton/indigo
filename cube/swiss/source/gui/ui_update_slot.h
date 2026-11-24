#ifndef UI_UPDATE_SLOT_H
#define UI_UPDATE_SLOT_H

#include "ui_update.h"

/* Zero timestamps denote empty or unverifiable records. */
typedef struct {
	int64_t confirmed[2];
	char tried[2][41];
} ui_update_slots_t;

int UIUpdate_FirstSlot(const ui_update_slots_t *slots);
int UIUpdate_InstallSlot(const ui_update_slots_t *slots, int runningSlot,
	const ui_update_manifest_t *release, bool automatic);

#endif
