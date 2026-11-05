#include "ui_home_text.h"
#include "ui_lang.h"

static float readableMax(float maxScale)
{
	return maxScale >= UI_HOME_TEXT_SCALE_FLOOR ? maxScale :
		UI_HOME_TEXT_SCALE_FLOOR;
}

static bool fitsAtFloor(const char *text, int maxWidth,
	uiHomeTextMeasureFn measure)
{
	int width;

	if(text == NULL || measure == NULL || maxWidth <= 0) {
		return false;
	}
	width = measure(text);
	return width <= 0 || (float)width * UI_HOME_TEXT_SCALE_FLOOR <=
		(float)maxWidth;
}

float UIHomeText_FitScale(const char *text, int maxWidth, float maxScale,
	uiHomeTextMeasureFn measure)
{
	float limit = readableMax(maxScale);
	float scale;
	int width;

	if(text == NULL || measure == NULL || maxWidth <= 0) {
		return UI_HOME_TEXT_SCALE_FLOOR;
	}
	width = measure(text);
	if(width <= 0) {
		return limit;
	}
	scale = (float)maxWidth / (float)width;
	if(scale > limit) {
		scale = limit;
	}
	return scale < UI_HOME_TEXT_SCALE_FLOOR ?
		UI_HOME_TEXT_SCALE_FLOOR : scale;
}

float UIHomeText_CopyFitted(char *destination, size_t capacity,
	const char *source, int maxWidth, float maxScale,
	uiHomeTextMeasureFn measure, bool *ellipsized)
{
	bool truncated = false;
	float scale;
	size_t length = 0u;
	size_t prefixLength;

	if(ellipsized != NULL) {
		*ellipsized = false;
	}
	if(destination == NULL || capacity == 0u) {
		return UI_HOME_TEXT_SCALE_FLOOR;
	}
	destination[0] = '\0';
	if(source == NULL || measure == NULL || maxWidth <= 0) {
		return UI_HOME_TEXT_SCALE_FLOOR;
	}

	while(length + 1u < capacity && source[length] != '\0') {
		destination[length] = source[length];
		++length;
	}
	destination[length] = '\0';
	truncated = source[length] != '\0';
	if(truncated && length > 0u) {
		/* The ellipsis never follows half a Cyrillic letter. */
		length = UILang_Head(source, length - 1u) + 1u;
		destination[length - 1u] = (char)UI_HOME_TEXT_ELLIPSIS_BYTE;
		destination[length] = '\0';
	}
	if(ellipsized != NULL) {
		*ellipsized = truncated;
	}

	scale = UIHomeText_FitScale(destination, maxWidth, maxScale, measure);
	if(fitsAtFloor(destination, maxWidth, measure)) {
		return scale;
	}
	if(capacity < 2u || length == 0u) {
		destination[0] = '\0';
		if(ellipsized != NULL) {
			*ellipsized = true;
		}
		return UI_HOME_TEXT_SCALE_FLOOR;
	}

	/* One or more source bytes must be omitted. If capacity already forced an
	 * ellipsis, preserve its prefix; otherwise replace the final source byte. */
	prefixLength = length - 1u;
	for(;;) {
		prefixLength = UILang_Head(source, prefixLength);
		destination[prefixLength] = (char)UI_HOME_TEXT_ELLIPSIS_BYTE;
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
	return UI_HOME_TEXT_SCALE_FLOOR;
}
