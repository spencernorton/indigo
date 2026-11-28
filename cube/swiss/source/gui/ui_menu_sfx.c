#include "ui_menu_sfx.h"

#include <math.h>

static void synthesize(int16_t *pcm, unsigned count, double firstHz, double firstWeight,
	double secondHz, double secondWeight, double decay)
{
	const double radiansPerHz = 2.0 * 3.14159265358979323846 / UI_MENU_SFX_RATE;
	const double firstStep = 2.0 * cos(radiansPerHz * firstHz);
	const double secondStep = 2.0 * cos(radiansPerHz * secondHz);
	const double falloff = exp(-1.0 / (UI_MENU_SFX_RATE * decay));
	const unsigned attack = UI_MENU_SFX_RATE * 4 / 1000;
	double firstPrevious = -sin(radiansPerHz * firstHz);
	double secondPrevious = -sin(radiansPerHz * secondHz);
	double first = 0.0, second = 0.0, envelope = 1.0;

	for(unsigned i = 0; i < count; ++i) {
		const double gain = i < attack ? envelope * ((double)i / attack) : envelope;
		/* The weights sum to one and the peak gain is 0.30. */
		pcm[i] = (int16_t)(0.30 * 32767.0 * gain * (firstWeight * first + secondWeight * second));

		/* sin(x + step) = 2*cos(step)*sin(x) - sin(x - step). */
		double firstNext = firstStep * first - firstPrevious;
		double secondNext = secondStep * second - secondPrevious;
		firstPrevious = first;
		secondPrevious = second;
		first = firstNext;
		second = secondNext;
		envelope *= falloff;
	}
}

void UIMenuSFX_Blip(int16_t *pcm)
{
	synthesize(pcm, UI_MENU_BLIP_SAMPLES, 880.0, 0.85, 1760.0, 0.15, 0.03);
}

void UIMenuSFX_Select(int16_t *pcm)
{
	synthesize(pcm, UI_MENU_SELECT_SAMPLES, 659.25, 0.6, 988.0, 0.4, 0.09);
}
