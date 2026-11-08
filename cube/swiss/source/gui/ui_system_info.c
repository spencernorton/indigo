#include <stdio.h>
#include <string.h>

#include "ui_system_info.h"
#include "ui_lang.h"

static const uiSystemPageDesc_t PAGES[UI_SYSTEM_PAGE_COUNT] = {
	{ "OVERVIEW", "Live console, source, output, and build summary." },
	{ "CONSOLE", "Hardware identity and low-level revision details." },
	{ "CONNECTIONS", "Storage and peripherals captured on page entry." },
	{ "INPUT / OUTPUT", "Controller sockets and active video state." },
	{ "ABOUT INDIGO", "Version, toolchain, source, and support identity." },
	{ "CREDITS", "People who made Swiss and Indigo possible." },
};

/* Short names: "WEDNESDAY  SEPTEMBER 30, 2026" is far wider than the
 * Overview's date line, which cut it short on a console. */
static const char *const WEEKDAYS[7] = {
	"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static const char *const MONTHS[12] = {
	"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
	"JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

static int clampPage(int page)
{
	if(page < 0) {
		return 0;
	}
	if(page >= UI_SYSTEM_PAGE_COUNT) {
		return UI_SYSTEM_PAGE_COUNT - 1;
	}
	return page;
}

const uiSystemPageDesc_t *UISystem_PageDesc(int page)
{
	return &PAGES[clampPage(page)];
}

void UISystem_ComputeLayout(int page, uiSystemLayout_t *out)
{
	if(out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	out->page = clampPage(page);
	out->panel = (uiSystemRect_t) {44, 72, 552, 322};
	out->rail = (uiSystemRect_t) {48, 408, 544, 24};
	out->leftCard = (uiSystemRect_t) {52, 136, 258, 242};
	out->rightCard = (uiSystemRect_t) {330, 136, 258, 242};
	out->wideCard = (uiSystemRect_t) {52, 136, 536, 248};
	out->titleX = 48;
	out->titleY = 94;
	out->subtitleX = 48;
	out->subtitleY = 116;
	out->progressX = 592;
	out->progressY = 94;
}

bool UISystem_RectIsSafe(const uiSystemRect_t *rect, int borderExpansion)
{
	int x0;
	int y0;
	int x1;
	int y1;

	if(rect == NULL || rect->w <= 0 || rect->h <= 0 || borderExpansion < 0) {
		return false;
	}
	x0 = (int)rect->x - borderExpansion;
	y0 = (int)rect->y - borderExpansion;
	x1 = (int)rect->x + (int)rect->w + borderExpansion;
	y1 = (int)rect->y + (int)rect->h + borderExpansion;
	return x0 >= UI_SYSTEM_SAFE_X0 && y0 >= UI_SYSTEM_SAFE_Y0 &&
		x1 <= UI_SYSTEM_SAFE_X1 && y1 <= UI_SYSTEM_SAFE_Y1;
}

static float readableMaximum(float maximum)
{
	return maximum < UI_SYSTEM_TEXT_SCALE_FLOOR ?
		UI_SYSTEM_TEXT_SCALE_FLOOR : maximum;
}

static bool fitsAtFloor(const char *text, int maxWidth,
	uiSystemTextMeasureFn measure)
{
	int width;

	if(text == NULL || measure == NULL || maxWidth <= 0) {
		return false;
	}
	width = measure(text);
	return width <= 0 ||
		(float)width * UI_SYSTEM_TEXT_SCALE_FLOOR <= (float)maxWidth;
}

float UISystem_CopyFitted(char *destination, size_t capacity,
	const char *source, int maxWidth, float maxScale,
	uiSystemTextMeasureFn measure, bool *ellipsized)
{
	float maximum = readableMaximum(maxScale);
	float scale;
	size_t length = 0u;
	size_t prefixLength;
	bool truncated = false;
	int width;

	if(ellipsized != NULL) {
		*ellipsized = false;
	}
	if(destination == NULL || capacity == 0u) {
		return UI_SYSTEM_TEXT_SCALE_FLOOR;
	}
	destination[0] = '\0';
	if(source == NULL || measure == NULL || maxWidth <= 0) {
		return UI_SYSTEM_TEXT_SCALE_FLOOR;
	}
	while(length + 1u < capacity && source[length] != '\0') {
		destination[length] = source[length];
		++length;
	}
	destination[length] = '\0';
	truncated = source[length] != '\0';
	if(truncated && length > 0u) {
		destination[length - 1u] = (char)UI_SYSTEM_ELLIPSIS_BYTE;
	}
	if(ellipsized != NULL) {
		*ellipsized = truncated;
	}
	if(length == 0u && truncated) {
		return UI_SYSTEM_TEXT_SCALE_FLOOR;
	}

	width = measure(destination);
	if(width <= 0) {
		return maximum;
	}
	scale = (float)maxWidth / (float)width;
	if(scale > maximum) {
		scale = maximum;
	}
	if(fitsAtFloor(destination, maxWidth, measure)) {
		return scale < UI_SYSTEM_TEXT_SCALE_FLOOR ?
			UI_SYSTEM_TEXT_SCALE_FLOOR : scale;
	}
	if(capacity < 2u || length == 0u) {
		destination[0] = '\0';
		if(ellipsized != NULL) {
			*ellipsized = true;
		}
		return UI_SYSTEM_TEXT_SCALE_FLOOR;
	}

	prefixLength = length - 1u;
	for(;;) {
		destination[prefixLength] = (char)UI_SYSTEM_ELLIPSIS_BYTE;
		destination[prefixLength + 1u] = '\0';
		if(fitsAtFloor(destination, maxWidth, measure)) {
			break;
		}
		if(prefixLength == 0u) {
			destination[0] = '\0';
			break;
		}
		--prefixLength;
	}
	if(ellipsized != NULL) {
		*ellipsized = true;
	}
	return UI_SYSTEM_TEXT_SCALE_FLOOR;
}

static bool copyUnavailable(char *out, size_t capacity, const char *message)
{
	if(out == NULL || capacity == 0u) {
		return false;
	}
	(void)snprintf(out, capacity, "%s", message);
	return false;
}

bool UISystem_FormatClock(char *out, size_t capacity, int hour, int minute,
	bool available)
{
	if(!available || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
		return copyUnavailable(out, capacity, "TIME UNAVAILABLE");
	}
	if(out == NULL || capacity == 0u) {
		return false;
	}
	(void)snprintf(out, capacity, "%02d:%02d", hour, minute);
	return true;
}

bool UISystem_FormatDate(char *out, size_t capacity, int weekday, int month,
	int day, int year, bool available)
{
	if(!available || weekday < 0 || weekday >= 7 || month < 0 || month >= 12 ||
		day < 1 || day > 31 || year < 2000 || year > 9999) {
		return copyUnavailable(out, capacity, "DATE UNAVAILABLE");
	}
	if(out == NULL || capacity == 0u) {
		return false;
	}
	(void)snprintf(out, capacity, "%s  %s %d, %d", WEEKDAYS[weekday],
		MONTHS[month], day, year);
	return true;
}

bool UISystem_FormatTemperature(char *out, size_t capacity,
	int degreesCelsius, bool available)
{
	if(!available || degreesCelsius < -40 || degreesCelsius > 125) {
		return copyUnavailable(out, capacity, "UNAVAILABLE");
	}
	if(out == NULL || capacity == 0u) {
		return false;
	}
	(void)snprintf(out, capacity, "%d\260C", degreesCelsius);
	return true;
}

bool UISystem_FormatCalibration(char *out, size_t capacity, int offsetCelsius)
{
	if(offsetCelsius < -80 || offsetCelsius > 80) {
		return copyUnavailable(out, capacity, "INVALID OFFSET");
	}
	if(out == NULL || capacity == 0u) {
		return false;
	}
	(void)snprintf(out, capacity, UI_T("%+d\260C OFFSET"), offsetCelsius);
	return true;
}

int UISystem_SmoothTemperature(uiSystemTemperature_t *state, int reading)
{
	float drift;

	if(reading < 0 || !state->primed) {
		state->average = (float)reading;
		state->shown = reading;
		state->primed = reading >= 0;
		return reading;
	}
	state->average += ((float)reading - state->average) / 8.0f;
	drift = state->average - (float)state->shown;
	if(drift >= 1.0f || drift <= -1.0f) {
		state->shown = (int)(state->average + 0.5f);
	}
	return state->shown;
}

void UISystem_FormatPageStatus(char *out, size_t capacity, int page)
{
	if(out == NULL || capacity == 0u) {
		return;
	}
	page = clampPage(page);
	(void)snprintf(out, capacity, UI_T("L/R  PAGE %d OF %d    B  BACK"),
		page + 1, UI_SYSTEM_PAGE_COUNT);
}

const char *UISystem_SourceHealth(bool configured, bool available)
{
	if(!configured) {
		return "NOT MOUNTED";
	}
	return available ? "READY" : "UNAVAILABLE";
}

const char *UISystem_RegionName(uiSystemRegion_t region)
{
	switch(region) {
		case UI_SYSTEM_REGION_NTSC_J:
			return "NTSC-J / JAPAN";
		case UI_SYSTEM_REGION_NTSC_U:
			return "NTSC-U / AMERICAS";
		case UI_SYSTEM_REGION_PAL:
			return "PAL / EUROPE";
		case UI_SYSTEM_REGION_MPAL:
			return "MPAL / BRAZIL";
		case UI_SYSTEM_REGION_UNKNOWN:
		default:
			return "REGION UNKNOWN";
	}
}
