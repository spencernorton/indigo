/* manual.c - a game's manual on the console: finds its file, reads its
 * pages into the viewer's two slots, on a thread of their own where the
 * device allows it, and runs the viewer. ui_manual.c has the rules, and
 * FrameBufferMagic.c (DrawManual) draws. */

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "files.h"
#include "input.h"
#include "menuaudio.h"
#include "util.h"
#include "ui_manual.h"
#include "ui_menu_input.h"
#include "ui_stage.h"
#include "manual.h"
#include "ui_lang.h"

#define SCROLL_ROWS 8	/* a frame, the D-pad held; the stick up to 4 times that */
#define STICK_DEAD 16
#define READER_STACK (32 * 1024)
#define READER_PRIORITY (LWP_PRIO_NORMAL - 1)	/* below the menus, as the posters' */

static const char fullHints[] =
	"STICK / D-PAD  SCROLL   \213 \233  PAGE   L/R  FIRST, LAST   Y  WHOLE PAGE   B  BACK";
static const char wholeHints[] =
	"\213 \233  PAGE   L/R  FIRST, LAST   Y  FULL WIDTH   B  BACK";

/* Manual_Pages' last answer. */
static struct {
	DEVICEHANDLER_INTERFACE *device;
	char gameId[7];
	int32_t pages;
	bool known;
} answer;

/* What malloc can still hand out: its free chunks and the arena it grows
 * into (as saves.c counts it). */
static u32 freeBytes(void)
{
	struct mallinfo info = mallinfo();

	return (u32)info.fordblks + SYS_GetArena1Size();
}

static bool readAt(file_handle *file, u32 offset, void *data, u32 size)
{
	return file->device->seekFile(file, offset, DEVICE_HANDLER_SEEK_SET) == offset &&
		file->device->readFile(file, data, size) == (s32)size;
}

/* The game's manual: <ID>.pak, else <first four>.pak, the same game in the
 * same region from any publisher. */
static bool findManual(DEVICEHANDLER_INTERFACE *device, const char *gameId,
	file_handle *file)
{
	char name[40];
	int length;

	if(device == NULL || device->initial == NULL || device->statFile == NULL ||
		device->seekFile == NULL || device->readFile == NULL || gameId == NULL ||
		strnlen(gameId, 6) != 6) {
		return false;
	}
	for(length = 6; length >= 4; length -= 2) {
		memset(file, 0, sizeof(*file));
		snprintf(name, sizeof(name), "swiss/ui/manuals/%.*s.pak", length, gameId);
		concat_path(file->name, device->initial->name, name);
		file->device = device;
		if(device->statFile(file) == 0 && file->size > 0) {
			return true;
		}
	}
	return false;
}

static void closeManual(file_handle *file)
{
	if(file->device != NULL && file->device->closeFile != NULL) {
		file->device->closeFile(file);
	}
}

/* The header and index, checked: the pages (*pages, the caller's to free). */
static bool readIndex(file_handle *file, uiManualInfo_t *info,
	uiManualPage_t **pages)
{
	u8 header[UI_MANUAL_HEADER_BYTES];
	u8 *index;
	bool ok;

	*pages = NULL;
	if(!readAt(file, 0, header, sizeof(header)) ||
		!UIManual_Header(header, file->size, info)) {
		return false;
	}
	index = malloc(info->indexBytes);
	*pages = malloc(info->pages * sizeof(**pages));
	ok = index != NULL && *pages != NULL &&
		readAt(file, UI_MANUAL_HEADER_BYTES, index, info->indexBytes) &&
		UIManual_Index(header, index, info, *pages);
	free(index);
	if(!ok) {
		free(*pages);
		*pages = NULL;
	}
	return ok;
}

int32_t Manual_Pages(DEVICEHANDLER_INTERFACE *device, const char *gameId)
{
	file_handle file;
	uiManualInfo_t info;
	uiManualPage_t *pages;

	if(gameId == NULL) {
		return 0;
	}
	if(answer.known && answer.device == device &&
		strncmp(answer.gameId, gameId, 6) == 0) {
		return answer.pages;
	}
	answer.pages = 0;
	if(findManual(device, gameId, &file)) {
		answer.pages = readIndex(&file, &info, &pages) ? (int32_t)info.pages : -1;
		free(pages);
		closeManual(&file);
	}
	answer.device = device;
	snprintf(answer.gameId, sizeof(answer.gameId), "%.6s", gameId);
	answer.known = true;
	return answer.pages;
}

void Manual_Forget(void)
{
	answer.known = false;
}

/* A page into its slot: read, checked against its CRC, and flushed for the
 * GPU. On the reader thread, or between frames for a device that isn't
 * thread-safe (the disc drive), as the file list's banners are read. */
typedef struct {
	file_handle *file;
	const uiManualInfo_t *info;
	const uiManualPage_t *page;
	u8 *into;
	u32 offset;
} readJob_t;

static lwp_t readerThread = LWP_THREAD_NULL;
static sem_t readerStart;
static readJob_t job;
/* Set as a job is handed over, cleared once it is read, after jobOk. */
static volatile bool jobBusy;
static volatile bool jobOk;
static volatile bool readerStop;

static bool readPage(const readJob_t *read)
{
	bool ok = readAt(read->file, read->offset, read->into, read->info->recordBytes) &&
		UIManual_RecordIntact(read->info, read->page, read->into);

	DCFlushRange(read->into, read->info->recordBytes);
	return ok;
}

static void *readerMain(void *unused)
{
	(void)unused;
	for(;;) {
		LWP_SemWait(readerStart);
		if(readerStop) {
			break;
		}
		jobOk = readPage(&job);
		jobBusy = false;
	}
	return NULL;
}

static bool startReader(void)
{
	readerStop = false;
	jobBusy = false;
	if(LWP_SemInit(&readerStart, 0, 1) < 0) {
		return false;
	}
	if(LWP_CreateThread(&readerThread, readerMain, NULL, NULL, READER_STACK,
		READER_PRIORITY) < 0) {
		readerThread = LWP_THREAD_NULL;
		LWP_SemDestroy(readerStart);
		return false;
	}
	return true;
}

/* A read in hand finishes first: the card's driver isn't stopped midway. */
static void stopReader(void)
{
	if(readerThread == LWP_THREAD_NULL) {
		return;
	}
	readerStop = true;
	LWP_SemPost(readerStart);
	LWP_JoinThread(readerThread, NULL);
	readerThread = LWP_THREAD_NULL;
	LWP_SemDestroy(readerStart);
}

static u32 elapsedUs(u32 *lastRetrace)
{
	u32 retrace = VIDEO_GetRetraceCount();
	u32 frames = retrace - *lastRetrace;
	float rate = VIDEO_GetRetraceRate();
	float elapsed;

	*lastRetrace = retrace;
	if(!(rate >= 1.0f)) rate = 60.0f;
	elapsed = (float)frames * 1000000.0f / rate;
	return elapsed >= (float)UI_MENU_INPUT_MAX_ELAPSED_US ?
		UI_MENU_INPUT_MAX_ELAPSED_US : (u32)elapsed;
}

static void say(const char *message)
{
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(D_FAIL, message));

	wait_press_A();
	DrawDispose(box);
}

void Manual_Show(DEVICEHANDLER_INTERFACE *device, const char *gameId)
{
	const u32 actions = BUTTON_B | BUTTON_Y | BUTTON_LEFT | BUTTON_RIGHT |
		BUTTON_L | BUTTON_R;
	file_handle file;
	uiManualInfo_t info;
	uiManualPage_t *pages = NULL;
	uiManualReader_t reader;
	uiManualView_t view;
	uiMenuActionState_t input;
	uiMenuInputState_t stick;
	uiDrawObj_t *page = NULL;
	u8 *slot[UI_MANUAL_SLOTS] = {NULL, NULL};
	u32 before = freeBytes();	/* closing gives all of it back */
	u32 pending = UI_MANUAL_SLOTS;	/* the slot the reader thread is filling */
	u32 lastRetrace = VIDEO_GetRetraceCount();
	u32 scroll = 0;
	bool whole = false;
	bool threaded;
	bool found;
	const char *problem = NULL;
	/* With Menu Widescreen, the page keeps its shape unless it is to fill
	 * the 16:9 screen (ui_manual.h). */
	float zoom = UI_MANUAL_FILL_WIDESCREEN && UIStage_PixelWidth() != 1.0f ?
		4.0f / 3.0f : 1.0f;
	int i;

	found = findManual(device, gameId, &file);
	if(!found) {
		problem = "This game's manual isn't on the card any more.\n"
			"Press A to continue.";
	}
	else if(!readIndex(&file, &info, &pages)) {
		problem = "This game's manual can't be read.\n"
			"Convert it again.\nPress A to continue.";
	}
	else {
		memset(&view, 0, sizeof(view));
		view.pageBytes = info.pageBytes;
		view.height = info.height;
		view.format = info.format;
		view.zoom = zoom;
		if(!UIManual_Fits(freeBytes(), &info) ||
			(slot[0] = memalign(32, info.recordBytes)) == NULL ||
			(slot[1] = memalign(32, info.recordBytes)) == NULL ||
			(page = DrawManual(&view)) == NULL) {
			problem = "Not enough memory to open the manual.\n"
				"Press A to continue.";
		}
	}
	if(problem != NULL) {
		free(slot[0]);
		free(slot[1]);
		free(pages);
		if(found) closeManual(&file);
		Manual_Forget();	/* the row says what it finds next time */
		say(problem);
		return;
	}
	threaded = (device->features & FEAT_THREAD_SAFE) != 0u && startReader();
	print_debug("manual: %.6s open, %u pages of %u bytes, %u bytes free (%u before)\n",
		gameId, (unsigned)info.pages, (unsigned)info.recordBytes,
		(unsigned)freeBytes(), (unsigned)before);

	UIManual_ReaderInit(&reader, info.pages);
	DrawPublish(page);
	UIMenuAction_Init(&input, padsButtonsHeld());
	(void)padsButtonsTaken(actions);
	UIMenuInput_Init(&stick);

	for(;;) {
		u32 now;
		u32 pressed;
		u32 held;
		u32 s, p;
		int32_t shown;
		int dy = 0;
		s8 y;
		uiMenuInputDirection_t push;

		VIDEO_WaitVSync();
		now = VIDEO_GetRetraceCount();
		if(pending < UI_MANUAL_SLOTS && !jobBusy) {
			UIManual_ReadDone(&reader, pending, jobOk, now);
			pending = UI_MANUAL_SLOTS;
		}
		if(pending == UI_MANUAL_SLOTS && UIManual_NextRead(&reader, now, &s, &p)) {
			job = (readJob_t){&file, &info, &pages[p], slot[s],
				UIManual_RecordOffset(&info, p)};
			if(threaded) {
				pending = s;
				jobBusy = true;
				LWP_SemPost(readerStart);
			}
			else {
				UIManual_ReadDone(&reader, s, readPage(&job), now);
			}
		}

		held = padsButtonsHeld();
		pressed = UIMenuAction_Update(&input, held | padsButtonsTaken(actions),
			actions, 0u, BUTTON_B);
		push = padsMenuInputPoll(&stick, elapsedUs(&lastRetrace),
			UI_MENU_INPUT_AXIS_HORIZONTAL, (held & actions) != 0u);
		if(push == UI_MENU_INPUT_LEFT) pressed |= BUTTON_LEFT;
		if(push == UI_MENU_INPUT_RIGHT) pressed |= BUTTON_RIGHT;
		if(pressed & BUTTON_B) {
			break;
		}
		if(pressed & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_L | BUTTON_R)) {
			u32 to = (pressed & BUTTON_L) ? 0u :
				(pressed & BUTTON_R) ? info.pages - 1u :
				(pressed & BUTTON_LEFT) ? (reader.page > 0u ? reader.page - 1u : 0u) :
				(reader.page + 1u < info.pages ? reader.page + 1u : reader.page);
			if(to != reader.page) {
				UIManual_Go(&reader, to, now);
				scroll = 0;	/* a new page from its top */
				menuaudio_blip();
			}
		}
		if(pressed & BUTTON_Y) {
			whole = !whole;
			menuaudio_blip();
		}
		if(!whole) {
			u32 most = UIManual_MaxScroll(&pages[reader.page], zoom);

			if(held & BUTTON_UP) dy -= SCROLL_ROWS;
			if(held & BUTTON_DOWN) dy += SCROLL_ROWS;
			y = padsStickY();
			if(y > STICK_DEAD || y < -STICK_DEAD) dy -= y / 4;
			scroll = dy < 0 && (u32)-dy > scroll ? 0u : scroll + (u32)dy;
			if(scroll > most) scroll = most;
		}

		shown = UIManual_Shown(&reader);
		view.texels = shown >= 0 ? slot[shown] : NULL;
		view.pageWidth = pages[reader.page].width;
		view.pageHeight = pages[reader.page].height;
		view.whole = whole;
		view.scroll = scroll;
		snprintf(view.counter, sizeof(view.counter), UI_T("PAGE %u / %u"),
			(unsigned)reader.page + 1u, (unsigned)info.pages);
		snprintf(view.message, sizeof(view.message), UIManual_ShownFailed(&reader) ?
			"Page %u can't be read." : "Reading page %u\205",
			(unsigned)reader.page + 1u);
		snprintf(view.hints, sizeof(view.hints), "%s", whole ? wholeHints : fullHints);
		DrawUpdateManual(page, &view);
	}
	menuaudio_select();
	stopReader();
	DrawDispose(page);
	/* The GPU's last frame with a page in it is done before the pages go. */
	for(i = 0; i < 3; ++i) {
		VIDEO_WaitVSync();
	}
	free(slot[0]);
	free(slot[1]);
	free(pages);
	closeManual(&file);
	print_debug("manual: closed, %u bytes free (%u before)\n", (unsigned)freeBytes(),
		(unsigned)before);
}
