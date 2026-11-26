#include "ui_menu_sfx.h"

#include <assert.h>
#include <math.h>

/* The original 32 kHz effects, evaluated independently at each sample. */
static void check(const int16_t *pcm, unsigned count, double a, double weightA,
	double b, double weightB, double decay)
{
	const double tau = 2.0 * 3.14159265358979323846;
	for(unsigned n = 0; n < count; ++n) {
		double envelope = exp(-(double)n / (32000.0 * decay));
		if(n < 128) envelope *= (double)n / 128.0;
		double wave = weightA * sin(tau * a * n / 32000.0)
			+ weightB * sin(tau * b * n / 32000.0);
		assert(pcm[n] == (int16_t)(0.30 * 32767.0 * envelope * wave));
	}
}

int main(void)
{
	int16_t blip[UI_MENU_BLIP_SAMPLES], select[UI_MENU_SELECT_SAMPLES];
	assert(UI_MENU_SFX_RATE == 32000);
	assert(UI_MENU_BLIP_SAMPLES == 1600 && UI_MENU_SELECT_SAMPLES == 5760);
	UIMenuSFX_Blip(blip);
	UIMenuSFX_Select(select);
	check(blip, 1600, 880.0, 0.85, 1760.0, 0.15, 0.03);
	check(select, 5760, 659.25, 0.60, 988.0, 0.40, 0.09);
	return 0;
}
