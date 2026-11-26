#ifndef UI_MENU_SFX_H
#define UI_MENU_SFX_H

#include <stdint.h>

#define UI_MENU_SFX_RATE 32000

enum {
	UI_MENU_BLIP_SAMPLES = 1600,
	UI_MENU_SELECT_SAMPLES = 5760
};

void UIMenuSFX_Blip(int16_t *pcm);
void UIMenuSFX_Select(int16_t *pcm);

#endif
