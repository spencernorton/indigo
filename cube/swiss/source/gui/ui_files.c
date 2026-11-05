#include "ui_files.h"
#include "ui_lang.h"
#include "ui_motion.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static float stageClamp(float value);

/* ------------------------------------------------------------------------
 * Small text helpers.
 * --------------------------------------------------------------------- */

/* Copies length bytes of source (or fewer, to fit), always terminated. */
static void copyRange(char *out, size_t capacity, const char *source, size_t length)
{
	if(out == NULL || capacity == 0u) return;
	if(source == NULL) length = 0u;
	if(length > capacity - 1u) length = capacity - 1u;
	if(length > 0u) memmove(out, source, length);
	out[length] = '\0';
}

static void copyText(char *out, size_t capacity, const char *source)
{
	copyRange(out, capacity, source, source != NULL ? strlen(source) : 0u);
}

static void format(char *out, size_t capacity, const char *pattern, ...)
{
	va_list args;

	if(out == NULL || capacity == 0u) return;
	va_start(args, pattern);
	/* Every pattern here is drawn: Menu Language has its own. */
	vsnprintf(out, capacity, UILang_Text(pattern), args);
	va_end(args);
}

static int lowerAscii(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* strcasecmp as newlib's C locale has it: ASCII only. */
static int compareNoCase(const char *a, const char *b)
{
	const unsigned char *x = (const unsigned char *)a;
	const unsigned char *y = (const unsigned char *)b;

	while(*x != '\0' && lowerAscii(*x) == lowerAscii(*y)) {
		++x;
		++y;
	}
	return lowerAscii(*x) - lowerAscii(*y);
}

static bool endsWithNoCase(const char *text, const char *end)
{
	size_t textLength = strlen(text), endLength = strlen(end);

	return textLength >= endLength && compareNoCase(text + textLength - endLength, end) == 0;
}

static bool continuationByte(char c)
{
	return ((unsigned char)c & 0xC0u) == 0x80u;
}

/* A path's last part: after its last '/', or its ':' (getRelativeName). */
static const char *leafOf(const char *path)
{
	const char *slash = strrchr(path, '/');

	if(slash == NULL) slash = strchr(path, ':');
	return slash != NULL ? slash + 1 : path;
}

/* A path below its device's root ("sd:/games/x" gives "games/x"), and its
 * length with trailing slashes left off. */
static const char *belowRoot(const char *path, size_t *length)
{
	const char *slash = strchr(path, '/');
	const char *below = slash != NULL ? slash + 1 : path + strlen(path);
	size_t n = strlen(below);

	while(n > 0u && below[n - 1u] == '/') --n;
	*length = n;
	return below;
}

/* A folder path's length with trailing slashes left off: "sd:/" and "sd:"
 * are both the root. */
static size_t folderLength(const char *path)
{
	size_t n = strlen(path);

	while(n > 0u && path[n - 1u] == '/') --n;
	return n;
}

/* ------------------------------------------------------------------------
 * Layout.
 * --------------------------------------------------------------------- */

static int roundToInt(float value)
{
	return (int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

static uiFilesRect_t rect(int x0, int y0, int x1, int y1)
{
	uiFilesRect_t r;

	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	return r;
}

void UIFiles_Layout(float stageLeft, float stageRight, uiFilesLayout_t *out)
{
	int left = roundToInt(stageLeft) + UI_FILES_MARGIN;
	int right = roundToInt(stageRight) - UI_FILES_MARGIN;
	int p;

	if(out == NULL) return;
	memset(out, 0, sizeof(*out));
	out->pane[UI_FILES_LEFT] = rect(left, UI_FILES_PANE_TOP, UI_FILES_INNER_LEFT,
		UI_FILES_PANE_BOTTOM);
	out->pane[UI_FILES_RIGHT] = rect(UI_FILES_INNER_RIGHT, UI_FILES_PANE_TOP, right,
		UI_FILES_PANE_BOTTOM);
	for(p = 0; p < UI_FILES_PANES; ++p) {
		const uiFilesRect_t *pane = &out->pane[p];
		int mid = (pane->x0 + pane->x1) / 2;

		out->mid[p] = mid;
		out->button[p] = rect(mid - UI_FILES_BUTTON_WIDTH / 2, UI_FILES_BUTTON_Y,
			mid + UI_FILES_BUTTON_WIDTH / 2, UI_FILES_BUTTON_Y + UI_FILES_BUTTON_HEIGHT);
		out->track[p] = rect(pane->x1 - 9, UI_FILES_TRACK_TOP, pane->x1 - 7,
			UI_FILES_TRACK_BOTTOM);
	}
	out->info = rect(left, UI_FILES_INFO_TOP, right, UI_FILES_INFO_BOTTOM);
	out->picture = rect(left + 16, 381, left + 16 + 96, 381 + 32);
	out->infoTextX = left + 128;
	out->sizeBox = rect(out->infoTextX, 397, out->infoTextX, 421);
	out->hintLeft = left;
	out->hintRight = right;
}

uiFilesRect_t UIFiles_RowRect(const uiFilesLayout_t *layout, int pane, int row)
{
	const uiFilesRect_t *box = &layout->pane[pane == UI_FILES_RIGHT];
	int top = UI_FILES_ROW_TOP + UI_FILES_ROW_PITCH * row;

	return rect(box->x0 + 4, top, box->x1 - 4, top + UI_FILES_ROW_HEIGHT);
}

int UIFiles_CubeX(const uiFilesLayout_t *layout, int pane)
{
	return layout->pane[pane == UI_FILES_RIGHT].x0 + 21;
}

int UIFiles_NameX(const uiFilesLayout_t *layout, int pane)
{
	return layout->pane[pane == UI_FILES_RIGHT].x0 + 40;
}

int UIFiles_MetaRight(const uiFilesLayout_t *layout, int pane, bool track)
{
	return layout->pane[pane == UI_FILES_RIGHT].x1 - 6 - (track ? 8 : 0);
}

int UIFiles_NameWidth(const uiFilesLayout_t *layout, int pane, int metaWidth,
	bool track)
{
	int right = UIFiles_MetaRight(layout, pane, track) - (metaWidth > 0 ? metaWidth + 8 : 0);

	return right - UIFiles_NameX(layout, pane);
}

/* ------------------------------------------------------------------------
 * The two panes' focus.
 * --------------------------------------------------------------------- */

/* The window keeps a row of margin round the focus, moving only as far as
 * that needs, and stays inside the list. */
static void follow(uiFilesPaneState_t *pane)
{
	int last = pane->count > UI_FILES_ROWS ? pane->count - UI_FILES_ROWS : 0;

	if(pane->focus < pane->first + 1) pane->first = pane->focus - 1;
	if(pane->focus > pane->first + UI_FILES_ROWS - 2) pane->first = pane->focus - (UI_FILES_ROWS - 2);
	if(pane->first > last) pane->first = last;
	if(pane->first < 0) pane->first = 0;
}

void UIFiles_Init(uiFilesState_t *state)
{
	if(state == NULL) return;
	memset(state, 0, sizeof(*state));
	state->active = UI_FILES_LEFT;
}

void UIFiles_SetPane(uiFilesState_t *state, int pane, int count, int focus)
{
	uiFilesPaneState_t *p;

	if(state == NULL || pane < 0 || pane >= UI_FILES_PANES) return;
	p = &state->pane[pane];
	p->count = count > 0 ? count : 0;
	p->focus = focus < 0 ? 0 : focus;
	if(p->focus >= p->count) p->focus = p->count > 0 ? p->count - 1 : 0;
	follow(p);
}

bool UIFiles_Input(uiFilesState_t *state, uiFilesInput_t input)
{
	uiFilesPaneState_t *p;
	int focus;

	if(state == NULL) return false;
	if(input == UI_FILES_INPUT_LEFT || input == UI_FILES_INPUT_RIGHT) {
		int to = input == UI_FILES_INPUT_LEFT ? UI_FILES_LEFT : UI_FILES_RIGHT;

		if(state->active == to) return false;
		state->active = to;
		return true;
	}
	p = &state->pane[state->active == UI_FILES_RIGHT];
	focus = p->focus;
	switch(input) {
	case UI_FILES_INPUT_UP:
		if(p->count > 0) focus = (focus + p->count - 1) % p->count;
		break;
	case UI_FILES_INPUT_DOWN:
		if(p->count > 0) focus = (focus + 1) % p->count;
		break;
	case UI_FILES_INPUT_PAGE_UP:
		focus = focus > UI_FILES_ROWS ? focus - UI_FILES_ROWS : 0;
		break;
	case UI_FILES_INPUT_PAGE_DOWN:
		if(p->count > 0) {
			focus += UI_FILES_ROWS;
			if(focus > p->count - 1) focus = p->count - 1;
		}
		break;
	default:
		return false;
	}
	if(focus == p->focus) return false;
	p->focus = focus;
	follow(p);
	return true;
}

void UIFiles_LeftView(const uiFilesState_t *state, int *start, int *end)
{
	const uiFilesPaneState_t *left = &state->pane[UI_FILES_LEFT];
	int last = left->first + UI_FILES_ROWS;

	*start = left->first;
	*end = last < left->count ? last : left->count;
}

/* ------------------------------------------------------------------------
 * Rows.
 * --------------------------------------------------------------------- */

static bool endsWithAny(const char *name, const char *const *extensions)
{
	for(; *extensions != NULL; ++extensions) {
		if(endsWithNoCase(name, *extensions)) return true;
	}
	return false;
}

uiFilesKind_t UIFiles_Kind(const char *name, int fileType)
{
	static const char *const disc[] = {".gcm", ".iso", ".tgc", ".fdi", NULL};
	static const char *const compressed[] = {".gcz", ".rvz", NULL};
	static const char *const program[] = {".dol", ".dol+cli", ".elf", ".bin", NULL};
	static const char *const firmware[] = {".fpkg", ".fzn", NULL};
	static const char *const music[] = {".mp3", NULL};
	static const char *const picture[] = {".png", ".jpg", ".jpeg", ".bmp", ".gif", NULL};
	static const char *const text[] = {".txt", ".ini", ".cfg", ".log", ".md", ".cli", NULL};

	if(fileType == UI_FILES_TYPE_PARENT) return UI_FILES_KIND_PARENT;
	if(fileType == UI_FILES_TYPE_DIR) return UI_FILES_KIND_FOLDER;
	if(name == NULL) return UI_FILES_KIND_OTHER;
	if(endsWithAny(name, disc)) return UI_FILES_KIND_DISC;
	if(endsWithAny(name, compressed)) return UI_FILES_KIND_DISC_COMPRESSED;
	if(endsWithAny(name, program)) return UI_FILES_KIND_PROGRAM;
	if(endsWithAny(name, firmware)) return UI_FILES_KIND_FIRMWARE;
	if(endsWithAny(name, music)) return UI_FILES_KIND_MUSIC;
	if(endsWithAny(name, picture)) return UI_FILES_KIND_PICTURE;
	if(endsWithAny(name, text)) return UI_FILES_KIND_TEXT;
	return UI_FILES_KIND_OTHER;
}

bool UIFiles_Loads(uiFilesKind_t kind)
{
	return kind == UI_FILES_KIND_PROGRAM_FOLDER || kind == UI_FILES_KIND_DISC ||
		kind == UI_FILES_KIND_PROGRAM || kind == UI_FILES_KIND_FIRMWARE ||
		kind == UI_FILES_KIND_MUSIC;
}

bool UIFiles_IsProgramFolder(const char *entryName, int fileType,
	const char *curDirName, bool flattened)
{
	const char *slash;
	size_t dirLength;

	if(flattened || fileType != UI_FILES_TYPE_FILE || entryName == NULL || curDirName == NULL) {
		return false;
	}
	/* Exactly what the rewrite makes: one folder below the open one, and
	 * the .dol inside it. Anything else with a slash (a memory card save's
	 * name may hold one) is a plain file. */
	dirLength = folderLength(curDirName);
	if(strncmp(entryName, curDirName, dirLength) != 0 || entryName[dirLength] != '/') {
		return false;
	}
	slash = strchr(entryName + dirLength + 1u, '/');
	return slash != NULL && slash > entryName + dirLength + 1u &&
		strchr(slash + 1, '/') == NULL && strlen(slash + 1) > 4u &&
		endsWithNoCase(slash + 1, ".dol");
}

void UIFiles_ProgramFolderPath(char *out, size_t capacity, const char *entryName,
	const char *curDirName)
{
	size_t dirLength, length;
	const char *slash;

	if(out == NULL || capacity == 0u) return;
	out[0] = '\0';
	if(entryName == NULL) return;
	dirLength = curDirName != NULL ? folderLength(curDirName) : 0u;
	if(curDirName != NULL && strncmp(entryName, curDirName, dirLength) == 0 &&
		entryName[dirLength] == '/') {
		slash = strchr(entryName + dirLength + 1u, '/');
		length = slash != NULL ? (size_t)(slash - entryName) : strlen(entryName);
	}
	else {
		slash = strrchr(entryName, '/');
		length = slash != NULL ? (size_t)(slash - entryName) : strlen(entryName);
	}
	copyRange(out, capacity, entryName, length);
}

void UIFiles_RowName(char *out, size_t capacity, const char *entryName,
	uiFilesKind_t kind, const char *curDirName, const char *deviceName)
{
	if(out == NULL || capacity == 0u) return;
	out[0] = '\0';
	if(kind == UI_FILES_KIND_PARENT) {
		size_t length;
		const char *below = belowRoot(curDirName != NULL ? curDirName : "", &length);
		size_t parent = length;

		if(length == 0u) {
			copyText(out, capacity, "Other storage");
			return;
		}
		while(parent > 0u && below[parent - 1u] != '/') --parent;
		if(parent == 0u) {
			format(out, capacity, "Up to %s", deviceName != NULL ? deviceName : UILang_Text("the top"));
		}
		else {
			size_t start = parent - 1u;

			while(start > 0u && below[start - 1u] != '/') --start;
			format(out, capacity, "Up to %.*s", (int)(parent - 1u - start), below + start);
		}
		return;
	}
	if(entryName == NULL) return;
	if(kind == UI_FILES_KIND_PROGRAM_FOLDER) {
		char folder[256];

		UIFiles_ProgramFolderPath(folder, sizeof(folder), entryName, curDirName);
		copyText(out, capacity, leafOf(folder));
		return;
	}
	copyText(out, capacity, leafOf(entryName));
}

/* util.c's formatBytes, without libogc. */
static const struct {
	const char *unit[2];
	double value[2];
} units[] = {
	{{"YiB", "YB"}, {0x1p80, 1e24}},
	{{"ZiB", "ZB"}, {0x1p70, 1e21}},
	{{"EiB", "EB"}, {0x1p60, 1e18}},
	{{"PiB", "PB"}, {0x1p50, 1e15}},
	{{"TiB", "TB"}, {0x1p40, 1e12}},
	{{"GiB", "GB"}, {0x1p30, 1e9}},
	{{"MiB", "MB"}, {0x1p20, 1e6}},
	{{"KiB", "kB"}, {0x1p10, 1e3}},
	{{NULL, NULL}, {0.0, 0.0}}
};

/* The unit formatBytes picks for count, or -1 for bytes. */
static int unitFor(uint64_t count, bool metric)
{
	int m = metric ? 1 : 0, i;

	for(i = 0; units[i].unit[m] != NULL; ++i) {
		if((double)count >= units[i].value[m] - units[i + 1].value[m] / 2) return i;
	}
	return -1;
}

void UIFiles_SizeText(char *out, size_t capacity, uint64_t bytes,
	uint32_t blockSize, bool metric)
{
	int i;

	if(blockSize != 0u) {
		uint64_t blocks = bytes / blockSize + (bytes % blockSize != 0u ? 1u : 0u);

		format(out, capacity, "%" PRIu64 " %s", blocks, UILang_Text(blocks == 1u ? "block" : "blocks"));
		return;
	}
	i = unitFor(bytes, metric);
	if(i < 0) {
		format(out, capacity, "%" PRIu64 " bytes", bytes);
		return;
	}
	format(out, capacity, "%.3g %s", (double)bytes / units[i].value[metric ? 1 : 0],
		units[i].unit[metric ? 1 : 0]);
}

void UIFiles_PartitionText(char *out, size_t capacity, int partition, int iso)
{
	format(out, capacity, "Partition %d, ISO %d", partition, iso);
}

void UIFiles_RowMeta(char *out, size_t capacity, uiFilesKind_t kind,
	const char *sizeText)
{
	if(kind == UI_FILES_KIND_PARENT || kind == UI_FILES_KIND_FOLDER ||
		kind == UI_FILES_KIND_PROGRAM_FOLDER) {
		copyText(out, capacity, "");
	}
	else if(kind == UI_FILES_KIND_DISC_COMPRESSED) {
		format(out, capacity, "Can't start \267 %s", sizeText != NULL ? sizeText : "");
	}
	else {
		copyText(out, capacity, sizeText);
	}
}

/* Two amounts in the larger one's unit, to its three figures: "0.80 GB" and
 * "1.35 GB". */
static void amountPair(char *a, char *b, size_t capacity, uint64_t x, uint64_t y,
	bool metric)
{
	uint64_t larger = x > y ? x : y;
	int i = unitFor(larger, metric), m = metric ? 1 : 0;
	double unit, scaled;
	int decimals;

	if(i < 0) {
		format(a, capacity, "%" PRIu64 " bytes", x);
		format(b, capacity, "%" PRIu64 " bytes", y);
		return;
	}
	unit = units[i].value[m];
	scaled = (double)larger / unit;
	decimals = scaled < 9.995 ? 2 : scaled < 99.95 ? 1 : 0;
	format(a, capacity, "%.*f %s", decimals, (double)x / unit, units[i].unit[m]);
	format(b, capacity, "%.*f %s", decimals, (double)y / unit, units[i].unit[m]);
}

/* ------------------------------------------------------------------------
 * Fitting.
 * --------------------------------------------------------------------- */

static bool fits(const char *text, int maxWidth, float scale, uiFilesMeasureFn measure)
{
	return (float)measure(text) * scale <= (float)maxWidth;
}

/* head's first length bytes (spaces at its end left off), the ellipsis and
 * tail, into out. False when out can't hold them. */
static bool joinCut(char *out, size_t capacity, const char *head, size_t length,
	const char *tail, size_t tailLength)
{
	while(length > 0u && head[length - 1u] == ' ') --length;
	if(length + 1u + tailLength + 1u > capacity) return false;
	memmove(out, head, length);
	out[length] = UI_FILES_ELLIPSIS;
	memmove(out + length + 1u, tail, tailLength);
	out[length + 1u + tailLength] = '\0';
	return true;
}

/* The characters a head shows: its UTF-8 sequences, trailing spaces not
 * counted (joinCut drops them). */
static size_t shownCharacters(const char *head, size_t length)
{
	size_t count = 0u;

	while(length > 0u && head[length - 1u] == ' ') --length;
	while(length > 0u) {
		if(!continuationByte(head[--length])) ++count;
	}
	return count;
}

/* The longest head that fits before the ellipsis and tail, cutting only
 * between UTF-8 sequences. With a tail the head keeps at least
 * UI_FILES_NAME_MIN_HEAD characters (or all it has), so a row never shows
 * only a tail. */
static bool cutMiddle(char *out, size_t capacity, const char *name, size_t headLength,
	const char *tail, size_t tailLength, int maxWidth, float scale,
	uiFilesMeasureFn measure)
{
	size_t length = headLength, least = 0u;

	if(tailLength > 0u) {
		least = shownCharacters(name, headLength);
		if(least > UI_FILES_NAME_MIN_HEAD) least = UI_FILES_NAME_MIN_HEAD;
	}
	while(1) {
		while(length > 0u && continuationByte(name[length])) --length;
		if(shownCharacters(name, length) < least) return false;
		if(joinCut(out, capacity, name, length, tail, tailLength) &&
			fits(out, maxWidth, scale, measure)) {
			return true;
		}
		if(length == 0u) return false;
		--length;
	}
}

/* Where a name's kept tail starts: its extension, and a "(Disc N)" right
 * before it when withDisc. The name's length when it has neither. */
static size_t tailStart(const char *name, size_t length, bool withDisc)
{
	const char *dot = strrchr(name, '.');
	size_t ext = dot != NULL && dot != name ? (size_t)(dot - name) : length;
	size_t open, i;

	/* "Version 2.1 final" has no extension worth keeping. */
	if(length - ext > 9u) ext = length;
	if(!withDisc || ext < 9u || name[ext - 1u] != ')') return ext;
	for(open = ext - 1u; open > 0u && name[open - 1u] != '('; --open) {}
	if(open == 0u) return ext;
	--open;
	/* "(Disc " then one to three characters. */
	if(strncmp(name + open, "(Disc ", 6u) != 0 || ext - open < 8u || ext - open > 10u) return ext;
	for(i = open + 6u; i < ext - 1u; ++i) {
		if(name[i] == ' ' || name[i] == '(') return ext;
	}
	return open;
}

float UIFiles_FitName(char *out, size_t capacity, const char *name, int maxWidth,
	uiFilesMeasureFn measure)
{
	size_t length;
	int width, pass;

	if(out == NULL || capacity == 0u) return UI_FILES_NAME_MIN_SCALE;
	out[0] = '\0';
	if(name == NULL || measure == NULL || maxWidth <= 0) return UI_FILES_NAME_MIN_SCALE;
	length = strlen(name);
	if(length < capacity) {
		copyText(out, capacity, name);
		width = measure(out);
		if(width <= 0 || (float)width * UI_FILES_NAME_SCALE <= (float)maxWidth) {
			return UI_FILES_NAME_SCALE;
		}
		if((float)width * UI_FILES_NAME_MIN_SCALE <= (float)maxWidth) {
			return (float)maxWidth / (float)width;
		}
	}
	/* Cut in the middle: keep "(Disc N)" and the extension, then only the
	 * extension, then nothing but the start. */
	for(pass = 0; pass < 3; ++pass) {
		size_t start = pass == 2 ? length : tailStart(name, length, pass == 0);

		if(cutMiddle(out, capacity, name, start, name + start, length - start, maxWidth,
			UI_FILES_NAME_MIN_SCALE, measure)) {
			return UI_FILES_NAME_MIN_SCALE;
		}
	}
	out[0] = '\0';
	return UI_FILES_NAME_MIN_SCALE;
}

float UIFiles_FitDevice(char *out, size_t capacity, const char *name,
	const uiFilesLayout_t *layout, int pane, bool source, int freeWidth,
	int freeWord, uiFilesMeasureFn measure)
{
	const uiFilesRect_t *box;
	int room, width;

	if(out == NULL || capacity == 0u) return UI_FILES_DEVICE_SCALE;
	out[0] = '\0';
	if(name == NULL || layout == NULL || measure == NULL || pane < 0 ||
		pane >= UI_FILES_PANES) return UI_FILES_DEVICE_SCALE;
	box = &layout->pane[pane];
	room = box->x1 - box->x0 - 2 - (source ? 10 + UI_FILES_CHIP_W : 0) -
		(freeWidth > 0 ? freeWidth + 8 + (freeWord > 0 ? freeWord + 8 : 0) : 0);
	copyText(out, capacity, name);
	width = measure(out);
	if(width <= 0 || (float)width * UI_FILES_DEVICE_SCALE <= (float)room) {
		return UI_FILES_DEVICE_SCALE;
	}
	if((float)width * UI_FILES_DEVICE_MIN_SCALE <= (float)room) {
		return (float)room / (float)width;
	}
	if(!cutMiddle(out, capacity, name, strlen(name), "", 0u, room,
		UI_FILES_DEVICE_MIN_SCALE, measure)) {
		out[0] = '\0';
	}
	return UI_FILES_DEVICE_MIN_SCALE;
}

void UIFiles_FitPath(char *out, size_t capacity, const char *path, int maxWidth,
	float scale, uiFilesMeasureFn measure)
{
	char shown[1024];
	size_t length, start;
	const char *below;

	if(out == NULL || capacity == 0u) return;
	out[0] = '\0';
	if(path == NULL || measure == NULL || maxWidth <= 0 || !(scale > 0.0f)) return;
	below = belowRoot(path, &length);
	shown[0] = '/';
	copyRange(shown + 1, sizeof(shown) - 1u, below, length);
	length = strlen(shown);
	copyText(out, capacity, shown);
	if(length < capacity && fits(out, maxWidth, scale, measure)) return;
	/* At a folder first ("\205/Collection/GameCube"), then anywhere. */
	for(start = 1u; start < length; ++start) {
		if(shown[start] == '/' && joinCut(out, capacity, "", 0u, shown + start, length - start) &&
			fits(out, maxWidth, scale, measure)) {
			return;
		}
	}
	for(start = 1u; start < length; ++start) {
		if(!continuationByte(shown[start]) &&
			joinCut(out, capacity, "", 0u, shown + start, length - start) &&
			fits(out, maxWidth, scale, measure)) {
			return;
		}
	}
	if(!joinCut(out, capacity, "", 0u, "", 0u) || !fits(out, maxWidth, scale, measure)) out[0] = '\0';
}

/* ------------------------------------------------------------------------
 * Hints.
 * --------------------------------------------------------------------- */

#define OTHER_SIDE "\213  \233  Other side"

void UIFiles_Hints(uiFilesHintMode_t mode, int pane, uiFilesKind_t kind,
	bool loads, bool fileManagement, bool autoloadOn,
	char left[UI_FILES_HINT_CAPACITY], char right[UI_FILES_HINT_CAPACITY])
{
	const char *z = fileManagement ? "Z  Actions   " : "";
	bool folder = kind == UI_FILES_KIND_FOLDER ||
		(kind == UI_FILES_KIND_PROGRAM_FOLDER && pane == UI_FILES_RIGHT);

	switch(mode) {
	case UI_FILES_HINTS_BOX:
		copyText(left, UI_FILES_HINT_CAPACITY, "A  Choose");
		copyText(right, UI_FILES_HINT_CAPACITY, "B  Back");
		return;
	case UI_FILES_HINTS_QUESTION:
		copyText(left, UI_FILES_HINT_CAPACITY, "A  Choose");
		copyText(right, UI_FILES_HINT_CAPACITY, "B  Cancel");
		return;
	case UI_FILES_HINTS_DELETE:
		copyText(left, UI_FILES_HINT_CAPACITY, "L+A  Delete");
		copyText(right, UI_FILES_HINT_CAPACITY, "B  Cancel");
		return;
	case UI_FILES_HINTS_MESSAGE:
		copyText(left, UI_FILES_HINT_CAPACITY, "A  OK");
		copyText(right, UI_FILES_HINT_CAPACITY, "");
		return;
	case UI_FILES_HINTS_STORAGE:
		copyText(left, UI_FILES_HINT_CAPACITY, "A  Choose storage   " OTHER_SIDE);
		copyText(right, UI_FILES_HINT_CAPACITY, "B  Home");
		return;
	default:
		break;
	}
	copyText(right, UI_FILES_HINT_CAPACITY, "X  Up   B  Home");
	if(kind == UI_FILES_KIND_PARENT) {
		/* Autoload comes on with File Management, and goes off always. */
		format(left, UI_FILES_HINT_CAPACITY, "A  Open   %s" OTHER_SIDE,
			autoloadOn ? "Z  Autoload off   " : fileManagement ? "Z  Autoload   " : "");
	}
	else if(folder) {
		format(left, UI_FILES_HINT_CAPACITY, "A  Open   %s" OTHER_SIDE, z);
	}
	else if(loads && pane == UI_FILES_LEFT) {
		format(left, UI_FILES_HINT_CAPACITY, "%s   %s" OTHER_SIDE,
			kind == UI_FILES_KIND_DISC ? "A  Details" : "A  Start", z);
	}
	else if(loads) {
		format(left, UI_FILES_HINT_CAPACITY, "%sY  Swap sides   " OTHER_SIDE, z);
	}
	else {
		format(left, UI_FILES_HINT_CAPACITY, "%s" OTHER_SIDE,
			fileManagement ? "A  Actions   " : "");
	}
}

/* ------------------------------------------------------------------------
 * Storage.
 * --------------------------------------------------------------------- */

static const char *nameOf(const uiFilesDevice_t *device)
{
	return device->name != NULL && device->name[0] != '\0' ? device->name : UILang_Text("This storage");
}

bool UIFiles_StorageClash(const uiFilesDevice_t *choice,
	const uiFilesDevice_t *other, char *reason, size_t capacity)
{
	if(reason != NULL && capacity > 0u) reason[0] = '\0';
	if(choice == NULL || other == NULL || choice->handler == NULL ||
		other->handler == NULL || choice->handler == other->handler) {
		return false;
	}
	if(choice->network && other->network) {
		copyText(reason, capacity, "The network adapter is in use on the other side.");
		return true;
	}
	if((choice->location & other->location) != 0u) {
		format(reason, capacity, "%s is open on the other side.", nameOf(other));
		return true;
	}
	return false;
}

bool UIFiles_FreeKnown(bool haveInfo, uint64_t totalSpace, bool network)
{
	return haveInfo && totalSpace != 0u && !network;
}

bool UIFiles_CanSwap(uiFilesMount_t mount, bool readOk)
{
	return mount == UI_FILES_SHARED || (mount == UI_FILES_OWN && readOk);
}

bool UIFiles_RightOnConfig(bool fileManagement, bool haveConfig,
	bool configDetected, bool configIsSource)
{
	return fileManagement && haveConfig && configDetected && !configIsSource;
}

void UIFiles_NotReady(char out[UI_FILES_MESSAGE_LINES][UI_FILES_MESSAGE_TEXT],
	const char *name, const char *status, const char *folder)
{
	format(out[0], UI_FILES_MESSAGE_TEXT, "%s isn't ready.",
		name != NULL && name[0] != '\0' ? name : UILang_Text("This storage"));
	if(folder != NULL) {
		format(out[1], UI_FILES_MESSAGE_TEXT, "Couldn't read %s.",
			folder[0] != '\0' ? folder : UILang_Text("its top folder"));
	}
	else if(status != NULL && status[0] != '\0') {
		format(out[1], UI_FILES_MESSAGE_TEXT, "(%s)", status);
	}
	else {
		out[1][0] = '\0';
	}
	copyText(out[2], UI_FILES_MESSAGE_TEXT, "Press R to choose storage.");
}

/* ------------------------------------------------------------------------
 * Actions.
 * --------------------------------------------------------------------- */

/* Copy's and Move's room on the other side. False greys the item. */
static bool fit(const uiFilesSide_t *other, const uiFilesEntry_t *entry,
	uiFilesAvailability_t *out, int action)
{
	char room[32], needed[32];
	const char *name = nameOf(&other->device);

	if(!other->freeKnown) {
		format(out->line[action], UI_FILES_TEXT_CAPACITY, "Free space on %s is unknown.", name);
		out->warn[action] = true;
		return true;
	}
	if(entry->needed <= other->freeBytes) {
		UIFiles_SizeText(needed, sizeof(needed), entry->needed, 0u, other->device.metric);
		format(out->line[action], UI_FILES_TEXT_CAPACITY, "Needs %s. It fits.", needed);
		return true;
	}
	if(entry->exists && entry->needed - other->freeBytes <= entry->existingSize) {
		copyText(out->line[action], UI_FILES_TEXT_CAPACITY,
			"It fits only by replacing the copy already there.");
		out->warn[action] = true;
		out->replaceOnly[action] = true;
		return true;
	}
	amountPair(room, needed, sizeof(room), other->freeBytes, entry->needed, other->device.metric);
	format(out->line[action], UI_FILES_TEXT_CAPACITY, "%s has %s free; this needs %s.",
		name, room, needed);
	return false;
}

/* One folder however its path is spelled: FAT ignores case, and a trailing
 * slash changes nothing. Two that differ only by case on a share that
 * doesn't ignore it are taken as one, which only refuses a copy there. */
static bool sameFolderPath(const char *a, const char *b)
{
	size_t length = folderLength(a), i;

	if(length != folderLength(b)) return false;
	for(i = 0u; i < length; i++) {
		if(lowerAscii((unsigned char)a[i]) != lowerAscii((unsigned char)b[i])) return false;
	}
	return true;
}

void UIFiles_Availability(const uiFilesSide_t *source, const uiFilesSide_t *other,
	const uiFilesEntry_t *entry, uiFilesAvailability_t *out)
{
	static const char *const labels[UI_FILES_ACTIONS] = {"Copy", "Move", "Rename", "Hide", "Delete"};
	static const char letters[UI_FILES_ACTIONS] = {'X', 'Y', 'R', 'L', 'Z'};
	const uiFilesDevice_t *from = &source->device, *to = &other->device;
	bool file = entry->isFile && !entry->programFolder;
	bool sameDevice = from->handler == to->handler;
	bool sameFolder = sameDevice && source->folder != NULL && other->folder != NULL &&
		sameFolderPath(source->folder, other->folder);
	bool ready = (other->mount == UI_FILES_SHARED || other->mount == UI_FILES_OWN) && other->readOk;
	bool renames = sameDevice && from->canWrite && from->canRename;
	char letter = entry->pane == UI_FILES_LEFT ? 'R' : 'L';
	int a;

	memset(out, 0, sizeof(*out));
	for(a = 0; a < UI_FILES_ACTIONS; ++a) {
		copyText(out->label[a], sizeof(out->label[a]), labels[a]);
		out->letter[a] = letters[a];
	}
	if(entry->hidden) copyText(out->label[UI_FILES_ACTION_HIDE], sizeof(out->label[0]), "Unhide");

	/* Copy and Move: the checks in the order a player would fix them. */
	for(a = UI_FILES_ACTION_COPY; a <= UI_FILES_ACTION_MOVE; ++a) {
		char *line = out->line[a];
		bool move = a == UI_FILES_ACTION_MOVE;

		if(!file) {
			copyText(line, UI_FILES_TEXT_CAPACITY, move ?
				"Folders can't be moved yet." : "Folders can't be copied yet.");
		}
		else if(!ready) {
			format(line, UI_FILES_TEXT_CAPACITY, "%s isn't ready. Choose storage with %c.",
				nameOf(to), letter);
		}
		else if(sameFolder) {
			copyText(line, UI_FILES_TEXT_CAPACITY, "It's already in this folder.");
		}
		else if(to->card) {
			/* A card's own write goes by the save inside the file, not
			 * its name: saves go card to card in Memory Cards. */
			copyText(line, UI_FILES_TEXT_CAPACITY, "Use Memory Cards to copy saves.");
		}
		else if(move && from->card) {
			/* Memory Cards moves a save: it reads the copy back and keeps
			 * a game's no-move saves where they are. */
			copyText(line, UI_FILES_TEXT_CAPACITY, "Use Memory Cards to move saves.");
		}
		else if(entry->existsFolder) {
			copyText(line, UI_FILES_TEXT_CAPACITY, "A folder there has the same name.");
		}
		else if(move && !(from->canWrite && (renames || from->canDelete))) {
			format(line, UI_FILES_TEXT_CAPACITY, "%s is read-only, so it can't be moved off it.",
				nameOf(from));
		}
		else if(!to->canWrite) {
			format(line, UI_FILES_TEXT_CAPACITY, "%s is read-only.", nameOf(to));
		}
		else if(move && renames) {
			/* Swiss renames it across: nothing is written. */
			out->enabled[a] = true;
		}
		else {
			out->enabled[a] = fit(other, entry, out, a);
		}
		if(!out->enabled[a]) out->warn[a] = true;
	}

	out->enabled[UI_FILES_ACTION_RENAME] = from->canWrite && from->canRename;
	out->enabled[UI_FILES_ACTION_HIDE] = from->canWrite && from->canHide;
	out->enabled[UI_FILES_ACTION_DELETE] = from->canWrite && from->canDelete;
	if(!out->enabled[UI_FILES_ACTION_RENAME]) {
		format(out->line[UI_FILES_ACTION_RENAME], UI_FILES_TEXT_CAPACITY,
			"%s can't rename files.", nameOf(from));
	}
	if(!out->enabled[UI_FILES_ACTION_HIDE]) {
		format(out->line[UI_FILES_ACTION_HIDE], UI_FILES_TEXT_CAPACITY,
			"%s can't hide files.", nameOf(from));
	}
	if(!out->enabled[UI_FILES_ACTION_DELETE]) {
		format(out->line[UI_FILES_ACTION_DELETE], UI_FILES_TEXT_CAPACITY,
			"%s can't delete files.", nameOf(from));
	}
	for(a = UI_FILES_ACTION_RENAME; a < UI_FILES_ACTIONS; ++a) out->warn[a] = !out->enabled[a];
}

int UIFiles_FirstEnabled(const bool *enabled, int count)
{
	int i;

	for(i = 0; i < count; ++i) {
		if(enabled[i]) return i;
	}
	return 0;
}

void UIFiles_ExistsChoices(bool fitsBoth, uiFilesChoices_t *out)
{
	memset(out, 0, sizeof(*out));
	copyText(out->item[0], sizeof(out->item[0]), "Keep both");
	copyText(out->item[1], sizeof(out->item[1]), "Replace it");
	copyText(out->item[2], sizeof(out->item[2]), "Cancel");
	out->dim = fitsBoth ? 0u : 1u;
	out->focus = fitsBoth ? 0 : 1;
	out->reason = fitsBoth ? "" : "There isn't room for both.";
}

void UIFiles_MenuBox(const uiFilesLayout_t *layout, const uiFilesRect_t *row,
	int pane, int items, bool titled, int width, uiFilesBox_t *out)
{
	int title = titled ? UI_FILES_MENU_TITLE + 4 : 0;
	int top = row->y0;

	int widest = layout->pane[pane == UI_FILES_RIGHT].x1 - layout->pane[pane == UI_FILES_RIGHT].x0 - 12;

	out->width = width > UI_FILES_MENU_WIDTH ? width : UI_FILES_MENU_WIDTH;
	if(out->width > widest) out->width = widest;
	out->height = 16 + UI_FILES_MENU_PITCH * (items > 0 ? items : 0);
	out->x = layout->pane[pane == UI_FILES_RIGHT].x1 - 6 - out->width;
	if(top < UI_FILES_BOX_TOP) top = UI_FILES_BOX_TOP;
	if(top + title + out->height > UI_FILES_BOX_BOTTOM) {
		top = UI_FILES_BOX_BOTTOM - title - out->height;
	}
	out->titleY = top;
	out->y = top + title;
}

#define MENU_OPEN 0.08f
#define MENU_CLOSE 0.10f

void UIFiles_MenuMotion(float since, bool open, int mode, float *alpha, float *scale)
{
	float a;

	if(mode == UI_MOTION_OFF) {
		a = open ? 1.0f : 0.0f;
	}
	else if(open) {
		a = stageClamp(since / MENU_OPEN);
	}
	else {
		a = 1.0f - stageClamp(since / MENU_CLOSE);
	}
	if(alpha != NULL) *alpha = a;
	if(scale != NULL) {
		*scale = !open || mode == UI_MOTION_OFF ? 1.0f :
			(mode == UI_MOTION_REDUCED ? 0.97f : 0.92f) +
			(mode == UI_MOTION_REDUCED ? 0.03f : 0.08f) * a;
	}
}

static bool sameDevice(const uiFilesDevice_t *a, const uiFilesDevice_t *b)
{
	return a != NULL && b != NULL && a->handler != NULL && a->handler == b->handler;
}

void UIFiles_StorageMenu(int pane, const uiFilesDevice_t *devices, int count,
	const uiFilesDevice_t *current, const uiFilesDevice_t *other,
	const char *otherPath, uiFilesStorageMenu_t *out)
{
	const char *here = UILang_Text(pane == UI_FILES_RIGHT ? "right" : "left");
	const char *there = UILang_Text(pane == UI_FILES_RIGHT ? "left" : "right");
	int i, focus = -1;

	if(out == NULL) return;
	memset(out, 0, sizeof(*out));
	copyText(out->box.title, sizeof(out->box.title),
		pane == UI_FILES_RIGHT ? "Right storage" : "Left storage");
	out->box.pane = (uint8_t)(pane == UI_FILES_RIGHT);
	out->box.open = 1u;
	if(devices == NULL || count < 0) count = 0;
	if(count > UI_FILES_STORAGE_DEVICES) count = UI_FILES_STORAGE_DEVICES;
	for(i = 0; i < count; i++) {
		const uiFilesDevice_t *device = &devices[i];

		copyText(out->box.item[i], sizeof(out->box.item[i]), nameOf(device));
		if(sameDevice(device, current)) {
			format(out->line[i], UI_FILES_TEXT_CAPACITY, pane == UI_FILES_RIGHT ?
				"Shown on the right now." : "Shown on the left now. Games start from it.");
			focus = i;
		}
		else if(sameDevice(device, other)) {
			format(out->line[i], UI_FILES_TEXT_CAPACITY,
				"Also open on the %s, in %s. Both sides can show it, each in its own folder.",
				there, otherPath != NULL && otherPath[0] != '\0' ? otherPath : "/");
		}
		else if(UIFiles_StorageClash(device, other, out->reason[i], UI_FILES_TEXT_CAPACITY)) {
			out->box.dim = (uint16_t)(out->box.dim | (1u << i));
			format(out->line[i], UI_FILES_TEXT_CAPACITY,
				"Can't open on the %s while %s is open on the %s.", here, nameOf(other),
				there);
		}
		else {
			copyText(out->line[i], UI_FILES_TEXT_CAPACITY, pane == UI_FILES_RIGHT ?
				"Opens on the right." : "Becomes the Source: games start from it.");
		}
	}
	out->devices = count;
	copyText(out->box.item[count], sizeof(out->box.item[count]), "Other devices\205");
	copyText(out->line[count], UI_FILES_TEXT_CAPACITY, pane == UI_FILES_RIGHT ?
		"Every storage that can be written to." :
		"Every storage, with each one's settings.");
	out->box.count = (uint8_t)(count + 1);
	out->warn = out->box.dim;
	if(focus < 0) {
		for(focus = 0; focus < count && (((unsigned)out->box.dim >> (unsigned)focus) & 1u); focus++) {
		}
	}
	out->box.focus = (uint8_t)focus;
}

/* fileComparator without the Game Disc's own first place (a disc is never
 * a destination). */
static int compareEntries(const char *aName, int aType, const char *bName, int bType)
{
	if(aType != bType) return bType - aType;
	return compareNoCase(aName, bName);
}

int UIFiles_LandingIndex(const void *context, int count, uiFilesEntryAtFn at,
	const char *name, int fileType)
{
	int low = 0, high = count > 0 ? count : 0;

	if(at == NULL || name == NULL) return 0;
	while(low < high) {
		int mid = low + (high - low) / 2;
		const char *midName = "";
		int midType = 0;

		at(context, mid, &midName, &midType);
		if(compareEntries(midName != NULL ? midName : "", midType, name, fileType) < 0) {
			low = mid + 1;
		}
		else {
			high = mid;
		}
	}
	return low;
}

void UIFiles_FocusAfter(uiFilesAction_t action, bool done, int focus, int count,
	bool showHidden, bool wasHidden, uiFilesFocusAfter_t *out)
{
	/* The row that takes a removed entry's place: the next, or the one
	 * before at the end; -1 when none is left. */
	int neighbour = focus + 1 < count ? focus + 1 : focus - 1;

	out->sourceIndex = focus;
	out->otherToNew = false;
	out->flash = false;
	if(!done) return;
	switch(action) {
	case UI_FILES_ACTION_COPY:
		out->otherToNew = true;
		out->flash = true;
		break;
	case UI_FILES_ACTION_MOVE:
		out->sourceIndex = neighbour;
		out->otherToNew = true;
		break;
	case UI_FILES_ACTION_RENAME:
		out->sourceIndex = UI_FILES_FOCUS_NEW_NAME;
		break;
	case UI_FILES_ACTION_HIDE:
		if(!wasHidden && !showHidden) out->sourceIndex = neighbour;
		break;
	case UI_FILES_ACTION_DELETE:
		out->sourceIndex = neighbour;
		break;
	default:
		break;
	}
}

void UIFiles_ActionLine(uiFilesAction_t action, bool hidden, const char *here,
	const char *there, const char *folder, char *out, size_t capacity)
{
	const char *place = folder != NULL && folder[0] != '\0' ? folder : "/";

	if(here == NULL || here[0] == '\0') here = UILang_Text("This storage");
	if(there == NULL || there[0] == '\0') there = UILang_Text("This storage");
	switch(action) {
	case UI_FILES_ACTION_COPY:
		format(out, capacity, "Copy puts a copy in %s  \233  %s. It stays here.", there, place);
		break;
	case UI_FILES_ACTION_MOVE:
		format(out, capacity, "Move puts it in %s  \233  %s and takes it off %s.", there, place,
			here);
		break;
	case UI_FILES_ACTION_RENAME:
		copyText(out, capacity, "Rename gives it a new name.");
		break;
	case UI_FILES_ACTION_HIDE:
		copyText(out, capacity, hidden ? "Unhide shows it again." :
			"Hide keeps it out of sight until Show hidden files is on.");
		break;
	default:
		format(out, capacity, "Delete takes it off %s for good.", here);
		break;
	}
}

void UIFiles_Result(uiFilesResult_t result, bool move, const char *name, const char *here,
	const char *there, const char *folder, int code, bool removed, bool replaced,
	char out[2][UI_FILES_TEXT_CAPACITY])
{
	const char *place = folder != NULL && folder[0] != '\0' ? folder : there;
	char left[UI_FILES_TEXT_CAPACITY];

	out[0][0] = out[1][0] = '\0';
	format(left, sizeof(left), replaced ? "Part of it is left in %s; the old file is gone." :
		"Part of it is left in %s.", place);
	switch(result) {
	case UI_FILES_RESULT_DONE:
		copyText(out[0], UI_FILES_TEXT_CAPACITY, move ? "Finished moving." : "Finished copying.");
		break;
	case UI_FILES_RESULT_STOPPED:
		copyText(out[0], UI_FILES_TEXT_CAPACITY, move ? "Moving was stopped." :
			"Copying was stopped.");
		if(removed && replaced) {
			copyText(out[1], UI_FILES_TEXT_CAPACITY,
				"The unfinished copy and the file it replaced are gone.");
		}
		else if(removed) {
			format(out[1], UI_FILES_TEXT_CAPACITY, "The unfinished copy was removed from %s.",
				there);
		}
		else {
			copyText(out[1], UI_FILES_TEXT_CAPACITY, left);
		}
		break;
	case UI_FILES_RESULT_WRITE_FAILED:
	case UI_FILES_RESULT_READ_FAILED:
		if(result == UI_FILES_RESULT_WRITE_FAILED) {
			format(out[0], UI_FILES_TEXT_CAPACITY, "Couldn't write to %s. (%d)", there, code);
		}
		else {
			format(out[0], UI_FILES_TEXT_CAPACITY, "Couldn't read %s. (%d)", name, code);
		}
		format(out[1], UI_FILES_TEXT_CAPACITY, "%s%s",
			removed && replaced ? "The unfinished copy and the file it replaced are gone." :
			removed ? "The unfinished copy was removed." : left,
			move ? " Nothing was moved." : "");
		break;
	default:
		format(out[0], UI_FILES_TEXT_CAPACITY, "Copied, but couldn't take it off %s.", here);
		copyText(out[1], UI_FILES_TEXT_CAPACITY, "It is in both places now.");
		break;
	}
}

void UIFiles_InsertGhost(uiFilesPaneSnapshot_t *pane, int landing,
	const uiFilesRowSnapshot_t *ghost)
{
	int at = landing - pane->first, i;

	if(at < 0) at = 0;
	if(at > pane->rows) at = pane->rows;
	if(at >= UI_FILES_ROWS) at = UI_FILES_ROWS - 1;
	for(i = (pane->rows < UI_FILES_ROWS ? pane->rows : UI_FILES_ROWS - 1); i > at; --i) {
		pane->row[i] = pane->row[i - 1];
	}
	pane->row[at] = *ghost;
	pane->row[at].flags = (uint8_t)((ghost->flags & ~UI_FILES_ROW_FOCUS) | UI_FILES_ROW_GHOST);
	if(pane->rows < UI_FILES_ROWS) pane->rows++;
	if(pane->focusRow >= at) {
		pane->focusRow = (int16_t)(pane->focusRow + 1 < UI_FILES_ROWS ? pane->focusRow + 1 : -1);
	}
	pane->count++;
}

float UIFiles_Flash(float since, int mode)
{
	float pulses = mode == UI_MOTION_FULL ? 2.0f : mode == UI_MOTION_REDUCED ? 1.0f : 0.0f;
	float t = since / 0.3f;

	if(!(t >= 0.0f) || t >= pulses) return 0.0f;
	t -= (float)(int)t;
	return 1.0f - (t > 0.5f ? 2.0f * t - 1.0f : 1.0f - 2.0f * t);
}

/* ------------------------------------------------------------------------
 * Questions.
 * --------------------------------------------------------------------- */

/* "MOVE" as an item: "Move". */
static void itemCase(char *out, size_t capacity, const char *word, size_t length)
{
	size_t i;

	copyRange(out, capacity, word, length);
	for(i = 1u; out[i - 1u] != '\0' && out[i] != '\0'; ++i) out[i] = (char)lowerAscii(out[i]);
}

bool UIFiles_ParseQuestion(const char *text, uiFilesQuestion_t *out)
{
	const char *firstBreak, *lastBreak, *hint, *verb, *gap, *cancel;
	size_t cancelLength, i;

	if(out == NULL) return false;
	memset(out, 0, sizeof(*out));
	if(text == NULL) return false;
	firstBreak = strchr(text, '\n');
	lastBreak = strrchr(text, '\n');
	if(firstBreak == NULL) return false;
	hint = lastBreak + 1;
	if(strncmp(hint, "A  ", 3u) != 0) return false;
	verb = hint + 3;
	gap = strstr(verb, "   ");
	if(gap == NULL || gap == verb) return false;
	for(cancel = gap; *cancel == ' '; ++cancel) {}
	if(strncmp(cancel, "B  ", 3u) != 0) return false;
	cancel += 3;
	cancelLength = strlen(cancel);
	while(cancelLength > 0u && cancel[cancelLength - 1u] == ' ') --cancelLength;
	if(cancelLength == 0u) return false;

	copyRange(out->title, sizeof(out->title), text, (size_t)(firstBreak - text));
	if(firstBreak != lastBreak) {
		copyRange(out->detail, sizeof(out->detail), firstBreak + 1,
			(size_t)(lastBreak - firstBreak - 1));
		for(i = 0u; out->detail[i] != '\0'; ++i) {
			if(out->detail[i] == '\n') out->detail[i] = ' ';
		}
	}
	itemCase(out->verb, sizeof(out->verb), verb, (size_t)(gap - verb));
	itemCase(out->cancel, sizeof(out->cancel), cancel, cancelLength);
	return true;
}

/* ------------------------------------------------------------------------
 * The page coming and going: Memory Cards' timings.
 * --------------------------------------------------------------------- */
#define STAGE_HANDOVER 0.3f
#define STAGE_PAPER_IN 0.12f
#define STAGE_PAPER_TIME 0.33f
#define STAGE_CHROME_IN 0.30f
#define STAGE_CHROME_TIME 0.25f
#define STAGE_FADE 0.25f
#define STAGE_LEAVE 0.45f
#define STAGE_LEAVE_REDUCED 0.2f
#define STAGE_LEAVE_CHROME 0.12f
#define STAGE_RETURN_START 0.25f

static float stageClamp(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

static float stageSmooth(float value)
{
	value = stageClamp(value);
	return value * value * (3.0f - 2.0f * value);
}

void UIFiles_Stage(float seconds, float leave, int mode, uiFilesStage_t *out)
{
	if(out == NULL) return;
	if(mode == UI_MOTION_OFF) {
		out->paper = out->chrome = leave >= 0.0f ? 0.0f : 1.0f;
		out->handover = leave >= 0.0f ? 1.0f : 0.0f;
		return;
	}
	if(mode == UI_MOTION_REDUCED) {
		out->paper = out->chrome = stageClamp(seconds / STAGE_FADE);
		out->handover = 0.0f;
		if(leave >= 0.0f) {
			out->paper *= 1.0f - stageClamp(leave / STAGE_LEAVE_REDUCED);
			out->chrome = out->paper;
		}
		return;
	}
	{
		float gone = stageClamp(seconds / STAGE_HANDOVER);

		out->handover = 1.0f - gone * gone * gone;
	}
	out->paper = stageClamp((seconds - STAGE_PAPER_IN) / STAGE_PAPER_TIME);
	out->chrome = stageSmooth((seconds - STAGE_CHROME_IN) / STAGE_CHROME_TIME);
	if(leave >= 0.0f) {
		float back = stageClamp((leave - STAGE_RETURN_START) /
			(STAGE_LEAVE - STAGE_RETURN_START));

		out->chrome *= 1.0f - stageClamp(leave / STAGE_LEAVE_CHROME);
		out->paper *= 1.0f - stageSmooth(leave / STAGE_LEAVE);
		/* ease out, cubic */
		back = 1.0f - (1.0f - back) * (1.0f - back) * (1.0f - back);
		if(back > out->handover) out->handover = back;
	}
}

float UIFiles_LeaveSeconds(int mode)
{
	return mode == UI_MOTION_OFF ? 0.0f : mode == UI_MOTION_REDUCED ?
		STAGE_LEAVE_REDUCED : STAGE_LEAVE;
}

float UIFiles_SwapHalfSeconds(int mode)
{
	return mode == UI_MOTION_OFF ? 0.0f : mode == UI_MOTION_REDUCED ? 0.05f : 0.075f;
}

float UIFiles_SwapStep(float content, bool swapping, float delta, int mode)
{
	float step;

	if(mode == UI_MOTION_OFF) return 1.0f;
	step = delta / UIFiles_SwapHalfSeconds(mode);
	if(!(step >= 0.0f)) step = 0.0f;
	return stageClamp(swapping ? content - step : content + step);
}
