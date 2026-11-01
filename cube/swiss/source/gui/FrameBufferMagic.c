/* -----------------------------------------------------------
      FrameBufferMagic.c - Framebuffer routines with GX
	      - by emu_kidid & sepp256

      Version 1.0 11/11/2009
        - Initial Code
   ----------------------------------------------------------- */

#include <fnmatch.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <ogc/exi.h>
#include <gctypes.h>
#include <ogc/lwp_watchdog.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "filemeta.h"
#include "swiss.h"
#include "main.h"
#include "util.h"
#include "ata.h"
#include "dolparameters.h"
#include "cheats.h"
#include "indigo_background.h"
#include "ui_anim.h"
#include "ui_clock.h"
#include "ui_color.h"
#include "ui_perf.h"
#include "ui_scene.h"
#include "ui_stage.h"
#include "ui_hint.h"
#include "ui_hint_geometry.h"
#include "ui_system_info.h"
#include "ui_assets.h"
#include "ui_command_rail.h"
#include "ui_home_layout.h"
#include "ui_home_text.h"
#include "ui_settings_focus.h"
#include "ui_gameflow_detail.h"
#include "ui_gameflow_library.h"
#include "ui_cheats.h"
#include "ui_about.h"
#include "ui_launch.h"
#include "ui_jewel_case.h"
#include "card_art.h"
#include "manual.h"
#include "ui_manual.h"

#define GUI_MSGBOX_ALPHA 225
#define GUI_PANEL_ALPHA 150	// Phase 2: translucent content panels (config-gated; dialogs stay at GUI_MSGBOX_ALPHA)

TPLFile imagesTPL;
TPLFile buttonsTPL;
GXTexObj gcdvdsmallTexObj;
GXTexObj sdsmallTexObj;
GXTlutObj sdsmallTlutObj;
GXTexObj hddTexObj;
GXTlutObj hddTlutObj;
GXTexObj qoobTexObj;
GXTlutObj qoobTlutObj;
GXTexObj qoobIndTexObj;
GXTexObj wodeimgTexObj;
GXTexObj usbgeckoTexObj;
GXTlutObj usbgeckoTlutObj;
GXTexObj memcardTexObj;
GXTlutObj memcardTlutObj;
GXTexObj memcardIndTexObj;
GXTexObj bbaTexObj;
GXTexObj wiikeyTexObj;
GXTexObj systemTexObj;
GXTexObj boxinnerTexObj;
GXTexObj boxouterTexObj;
GXTexObj ntscjTexObj;
GXTexObj ntscuTexObj;
GXTexObj palTexObj;
GXTexObj checkedTexObj;
GXTexObj uncheckedTexObj;
GXTexObj loadingTexObj;
/* The 96x32 file-type tags Swiss's lists drew. filemeta.c still points a
 * file's meta at them, but nothing draws them, so they are never loaded. */
GXTexObj dirimgTexObj;
GXTexObj dolimgTexObj;
GXTexObj dolcliimgTexObj;
GXTexObj elfimgTexObj;
GXTexObj fileimgTexObj;
GXTexObj fpkgimgTexObj;
GXTexObj gcmimgTexObj;
GXTexObj mp3imgTexObj;
GXTexObj tgcimgTexObj;
GXTexObj gcloaderTexObj;
GXTexObj m2loaderTexObj;
GXTexObj eth2gcTexObj;
GXTexObj flippyTexObj;
GXTexObj gcnetTexObj;
GXTexObj kunaigcTexObj;

static char fbTextBuffer[256];

// Video threading vars
#define VIDEO_STACK_SIZE (64*1024)
#define VIDEO_PRIORITY LWP_PRIO_HIGHEST
static char  video_thread_stack[VIDEO_STACK_SIZE] ATTRIBUTE_ALIGN (8);
static lwp_t video_thread = LWP_THREAD_NULL;
static mutex_t _videomutex = LWP_MUTEX_NULL;
static bool sceneRenderingEnabled;
/* While a Settings page is up, the screen keeps the colors that page was
 * drawn with (DrawUpdateSettingsPage): Menu, Backdrop and Wave Color, one per
 * UI_COLOR_LAYER_; disposing the page lets them go. While one of their lists
 * is open, the color it has focused shows instead. -1: none. */
static uiDrawObj_t *menuColorPage;
static int menuColorPinned[UI_COLOR_LAYERS] = {-1, -1, -1};
static int menuColorPreview[UI_COLOR_LAYERS] = {-1, -1, -1};
/* The UI_COLOR_ value each layer shows this frame. */
static int frameColors[UI_COLOR_LAYERS];

typedef struct {
	uiClockFrame_t clock;
	time_t sampledSecond;
	int hour;
	int minute;
	int second;
	char timeText[9];
	char temperatureText[8];
	s8 coreTemperature;
	uiSystemTemperature_t temperatureFilter;
	bool civilSecondSampled;
	bool civilTimeAvailable;
	bool temperatureSampled;
} uiSystemInstrument_t;

static uiSystemInstrument_t systemInstrument;
static void _UpdateSystemInstrument(void);
static indigoPadFrame_t padInstrument;
/* This frame shows a full-screen page over everything before it. */
static bool backgroundCovered;
static file_handle posterPackFile;
static DEVICEHANDLER_INTERFACE *posterPackDevice;
static bool posterPackAttempted;
static bool posterPackFileOwned;
/* Spotlight's gameplay stills: stills.pak beside posters.pak, tried in the
 * same attempt on the same device. */
static file_handle stillsPackFile;
static bool stillsPackFileOwned;
static bool stillsPackAttempted;
/* The Jewel Case's disc art: discs.pak, opened the same way. */
static file_handle discsPackFile;
static bool discsPackFileOwned;
static bool discsPackAttempted;
/* Spotlight's game descriptions: swiss/ui/descriptions.txt on the browsed
 * device, read whole the first time Spotlight asks there. The menu thread's
 * alone; the video thread only ever sees a snapshot's copy. */
static DEVICEHANDLER_INTERFACE *descriptionsDevice;
static bool descriptionsAttempted;
static char *descriptionsText;
static uiAboutEntry_t *descriptionsIndex;
static uiAbout_t descriptions;
static bool gameflowResetRegistered;
static s32 _GameflowOnReset(s32 final);
static bool _LaunchTakesBar(bool indeterminate);
static sys_resetinfo gameflowResetInfo = {
	{NULL, NULL}, _GameflowOnReset, 0
};

static uiMotionMode_t _CurrentMotionMode(void);

enum VideoEventType
{
	EV_TEXOBJ = 0,
	EV_MSGBOX,
	EV_IMAGE,
	EV_BACKGROUND,
	EV_PROGRESS,
	EV_SELECTABLEBUTTON,
	EV_EMPTYBOX,
	EV_TRANSPARENTBOX,
	EV_VERTSCROLLBAR,
	EV_STYLEDLABEL,
	EV_CONTAINER,
	EV_HOME,
	EV_DEVICESELECTOR,
	EV_TOOLTIP,
	EV_TITLEBAR,
	EV_GAMEFLOW,
	EV_PRESENTATION,
	EV_SETTINGS,
	EV_CHEATS,
	EV_SETTINGSLIST,
	EV_SETTINGSHELP,
	EV_MEMORY_FOLDER,
	EV_SAVES,
	EV_SAVE_CUBES,
	EV_SAVE_DETAILS,
	EV_FILES,
	EV_MANUAL
};

char * typeStrings[] = {"TexObj", "MsgBox", "Image", "Background", "Progress", "SelectableButton", "EmptyBox", "TransparentBox",
						"VertScrollbar", "StyledLabel", "Container", "Home", "DeviceSelector", "Tooltip", "TitleBar", "Gameflow", "Presentation", "Settings", "Cheats", "SettingsList", "SettingsHelp", "MemoryCardFolder", "Saves", "SaveCubes", "SaveDetails", "Files", "Manual"};
_Static_assert(sizeof(typeStrings) / sizeof(typeStrings[0]) == EV_MANUAL + 1u,
	"every video event needs a diagnostic name");

typedef struct drawTexObjEvent {
	GXTexObj *texObj;
	int x;
	int y;
	int width;
	int height;
	int depth;
	float s1;
	float s2;
	float t1;
	float t2;
} drawTexObjEvent_t;

typedef struct drawVertScrollbarEvent {
	int x;
	int y;
	int width;
	int height;
	float scrollPercent;
	int scrollHeight;
} drawVertScrollbarEvent_t;

typedef struct drawImageEvent {
	int textureId;
	int x;
	int y;
	int width;
	int height;
	int depth;
	float s1;
	float s2;
	float t1;
	float t2;
} drawImageEvent_t;

typedef struct drawStyledLabelEvent {
	int x;
	int y;
	const char *(*getString)(void);
	char *string;
	float size;
	int align;
	GXColor color;
	int fadingDirection;
	bool showCaret;
	int caretPosition;
	GXColor caretColor;
	bool hint;	/* draw button names as icons */
} drawStyledLabelEvent_t;

typedef struct drawSelectableButtonEvent {
	int x1;
	int y1;
	int x2;
	int y2;
	int mode;
	char *msg;
} drawSelectableButtonEvent_t;

typedef struct drawBoxEvent {
	int x1;
	int y1;
	int x2;
	int y2;
	GXColor backfill;
} drawBoxEvent_t;

typedef struct drawHomeEvent {
	uiHomeState_t state;
	uiHomeCapabilities_t capabilities;
	uiHomeLayout_t layout;
	bool visible;
	bool layoutValid;
	char sourceName[64];
	char heading[96];
	char command[96];
	float focusScale[UI_HOME_FACE_COUNT];
	float rowSelectedScale[UI_HOME_LAYOUT_MAX_ROWS];
	float rowIdleScale[UI_HOME_LAYOUT_MAX_ROWS];
	float headingScale;
	float commandScale;
	float contextCommandScale;
	float confirmCommandScale;
	float consequenceScale;
	/* Seconds since the surface changed, up to HOME_SURFACE_REVEAL: a new
	 * surface fades in. */
	float surfaceAge;
	/* Each row's style from idle (0) to selected (1), springing between as
	 * the selection moves; a new surface's rows start where they rest. */
	uiMotionSpring_t rowWeight[UI_HOME_LAYOUT_MAX_ROWS];
	bool rowsPlaced;
} drawHomeEvent_t;

#define HOME_SURFACE_REVEAL 0.18f
#define HOME_ROW_RESPONSE 25.0f

static const char homeContextCommand[] =
	"D-PAD  SELECT    A  OPEN    B  BACK";
static const char homeConfirmCommand[] =
	"\213  \233  CHOOSE    A  SELECT    B  CANCEL";
static const char homeRestartConsequence[] =
	"RELOADS INDIGO AND ENDS THIS SESSION";

/* One device the Source picker lists, copied when the menu thread publishes:
 * FTP, SMB and FSP change their picture, name and place as they probe, so
 * the video thread never reads a device handler. */
typedef struct {
	textureImage picture;
	char name[40];
	char facts[48];		/* what it can do, and where it plugs in */
	char status[48];	/* DETECTED or NOT DETECTED, CURRENT, SETTINGS */
	float nameScale;
	bool detected;
} drawDeviceTile_t;

typedef struct {
	drawDeviceTile_t tiles[MAX_DEVICES];
	char hint[112];
	float hintScale;
	int count;
	int travel;	/* the focus, unwrapped: a step moves it by one */
	bool showAllDevices;
	bool inAdvanced;
	bool exiFast;
} drawDeviceSelectorSnapshot_t;

typedef struct drawDeviceSelectorEvent {
	drawDeviceSelectorSnapshot_t snapshot;
	bool destination;
	/* The video thread's own. */
	uiMotionSpring_t position;
	bool started;
	bool shownAll;
} drawDeviceSelectorEvent_t;

typedef struct drawTooltipEvent {
	char *tooltip;
} drawTooltipEvent_t;

typedef struct drawGameflowDetailPresentation {
	float titleScale;
	float companyScale;
	float factsScale;
	float statusScale;
	float lastPlayedScale;
	float savesSummaryScale;
	float savesUpdatedScale;
	float cheatSummaryScale;
	float cheatPreviewScale;
	float settingsSummaryScale;
	float settingsPreviewScale;
	float manualSummaryScale;
	float launchScale;
	float primaryActionsScale;
	float advancedLineOneScale;
	float advancedLineTwoScale;
	GXColor accent;
} drawGameflowDetailPresentation_t;

typedef struct drawGameflowCardPresentation {
	float titleScale;
	float companyScale;
	float factsScale;
} drawGameflowCardPresentation_t;

typedef struct drawGameflowPreparedCard {
	drawGameflowCardPresentation_t sizes;
	u16 keep[3]; /* prefix before ellipsis, or 0xffff for the complete text */
} drawGameflowPreparedCard_t;

/* Menu-side staging: prepare text without holding the video's mutex.
 * Keep it small enough for a menu action's stack, with no allocation. */
typedef struct drawGameflowSnapshotPresentation {
	drawGameflowPreparedCard_t cards[UI_GAMEFLOW_RENDER_SLOTS];
	char spotlightLines[UI_GAMEFLOW_DESCRIPTION_LINES][UI_CHEATS_TEXT_CAPACITY];
} drawGameflowSnapshotPresentation_t;

_Static_assert(sizeof(drawGameflowSnapshotPresentation_t) <= 1536u,
	"Library text staging must stay within its menu stack budget");

typedef struct drawGameflowEvent {
	uiGameflowRenderSnapshot_t snapshot;
	uiGameflowState_t state;
	GXTexObj bannerTexObj[UI_GAMEFLOW_RENDER_SLOTS];
	uiGameflowDetailSnapshot_t detail;
	drawGameflowDetailPresentation_t detailPresentation;
	drawGameflowCardPresentation_t
		cardPresentation[UI_GAMEFLOW_RENDER_SLOTS];
	GXTexObj detailBannerTexObj;
	/* Outside the snapshot, so a republished Detail keeps it. */
	uiGameflowDetailFocus_t detailFocus;
	/* The focused row's frame, its top and height, sliding between rows;
	 * placed afresh (response 0) each time Detail opens. */
	uiMotionSpring_t detailLit[2];
	/* Spotlight: the selected game's description, wrapped for its column
	 * when the snapshot is published. */
	char spotlightLines[UI_GAMEFLOW_DESCRIPTION_LINES][UI_CHEATS_TEXT_CAPACITY];
} drawGameflowEvent_t;

typedef struct drawPresentationEvent {
	uiPresentationSnapshot_t snapshot;
	float titleScale;
	float messageScale;
	float detailScale;
	float actionScale;
} drawPresentationEvent_t;

/* Every fit is computed before this immutable event is published. */
typedef struct {
	uiSaveDetailsSnapshot_t snapshot;
	char blocks[12];
	char kib[12];
	float nameScale;
	float blocksScale;
	float kibScale;
	float sourceScale;
	float updatedScale;
	float iconScale;
} drawSaveDetailsEvent_t;

_Static_assert(sizeof(uiGameflowCardSnapshot_t) % 32u == 0u,
	"Gameflow record stride must preserve banner alignment");
_Static_assert(offsetof(uiGameflowRenderSnapshot_t, records) % 32u == 0u,
	"Gameflow records must begin at a cache-line boundary");
_Static_assert(offsetof(drawGameflowEvent_t, detail) % 32u == 0u,
	"Gameflow detail snapshot must begin at a cache-line boundary");
_Static_assert(offsetof(uiGameflowDetailSnapshot_t, banner) % 32u == 0u,
	"Gameflow detail banner must begin at a cache-line boundary");

typedef struct drawMsgBoxEvent {
	int type;
} drawMsgBoxEvent_t;

typedef struct drawProgressEvent {
	bool indeterminate;
	bool miniMode;
	bool hidden;	/* a launch's: the launch screen shows its step */
	int miniModePos;
	float miniModeAlpha;
	float seconds;	/* shown for: the spinner's and sweep's clock */
	int percent;
	int speed;	// in bytes
	int timestart;
	int timeremain;
	bool files;	/* the File Browser's copy: a scrim under it, and B Stop */
} drawProgressEvent_t;

typedef struct uiDrawObjQueue {
	struct uiDrawObj *event;
	struct uiDrawObjQueue *next;
} uiDrawObjQueue_t;

static uiDrawObjQueue_t *videoEventQueue = NULL;
static uiDrawObj_t *buttonPanel = NULL;

static void drawInit(void);
static void _DrawHintText(int x, int y, const char *text, float scale, int align,
	GXColor color);
static void _DrawSimpleBox(int x, int y, int width, int height, int depth,
	GXColor fillColor, GXColor borderColor);
static void _DrawDialogCard(int x, int y, int width, int height, int type);
static void _DrawDialogBar(int x, int y, int width, int start, int length);
static void _SaveCubesBar(float x, float y, float width, float height,
	GXColor color);

#if UI_PERF_CAPTURE
/* The GPU's own counters, summed between two refreshes of the overlay: the
 * vertices, pixels and texels it drew and the clocks it spent copying the
 * EFB (the glass copies and the display copy). A console counts them;
 * Dolphin may not. */
static u64 perfGpuVertices, perfGpuPixels, perfGpuTexels, perfGpuCopyClocks;
static u32 perfGpuFrames;

static void _PerfGpuFrameStart(void)
{
	GX_ClearGPMetric();
	GX_ClearPixMetric();
}

/* After GX_DrawDone: the frame's work is finished. */
static void _PerfGpuFrameEnd(void)
{
	u32 vertices, texels, topIn, topOut, bottomIn, bottomOut, clearIn, copyClocks;

	GX_ReadGPMetric(&vertices, &texels);
	GX_ReadPixMetric(&topIn, &topOut, &bottomIn, &bottomOut, &clearIn, &copyClocks);
	perfGpuVertices += vertices;
	perfGpuPixels += (u64)topIn + bottomIn;
	perfGpuTexels += texels;
	perfGpuCopyClocks += copyClocks;
	perfGpuFrames++;
}

static void _DrawPerfOverlay(void)
{
	static char summary[128] = "PERF capture warming up";
	static char gpu[128] = "";
	static u32 framesUntilRefresh = 1;
	uiPerfSnapshot_t snapshot;
	u64 workP99;
	u64 backgroundP99;
	u64 periodP99;

	if(--framesUntilRefresh == 0) {
		UIPerf_Snapshot(&snapshot);
		workP99 = UIPerf_PercentileUs(&snapshot.metrics[UI_PERF_METRIC_FRAME_WORK], 99);
		backgroundP99 = UIPerf_PercentileUs(
			&snapshot.metrics[UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT], 99);
		periodP99 = UIPerf_PercentileUs(&snapshot.metrics[UI_PERF_METRIC_FRAME_PERIOD], 99);
		snprintf(summary, sizeof(summary),
			"PERF p99 work %llu.%02llums  bg %llu.%02llums  cadence %llu.%02llums  drops %llu",
			(unsigned long long)(workP99 / 1000),
			(unsigned long long)((workP99 % 1000) / 10),
			(unsigned long long)(backgroundP99 / 1000),
			(unsigned long long)((backgroundP99 % 1000) / 10),
			(unsigned long long)(periodP99 / 1000),
			(unsigned long long)((periodP99 % 1000) / 10),
			(unsigned long long)snapshot.metrics[UI_PERF_METRIC_FRAME_PERIOD].thresholdExceedances);
		if(perfGpuFrames) {
			/* Per frame: vertices, then thousands of pixels and texels,
			 * then thousands of copy clocks (162 per microsecond). */
			snprintf(gpu, sizeof(gpu),
				"GPU/frame verts %llu  kpx %llu  ktex %llu  copy %llu kclk",
				(unsigned long long)(perfGpuVertices / perfGpuFrames),
				(unsigned long long)(perfGpuPixels / perfGpuFrames / 1000),
				(unsigned long long)(perfGpuTexels / perfGpuFrames / 1000),
				(unsigned long long)(perfGpuCopyClocks / perfGpuFrames / 1000));
		}
		perfGpuVertices = perfGpuPixels = perfGpuTexels = perfGpuCopyClocks = 0;
		perfGpuFrames = 0;
		framesUntilRefresh = 60;
	}

	drawInit();
	_DrawSimpleBox(118, 82, 404, 36, 0,
		(GXColor) {7, 6, 24, 218}, (GXColor) {135, 124, 209, 150});
	drawString(320, 94, summary, 0.42f, ALIGN_CENTER, defaultColor);
	drawString(320, 108, gpu, 0.42f, ALIGN_CENTER, defaultColor);
}
#endif

// Add root level uiDrawObj_t
static uiDrawObj_t* addVideoEvent(uiDrawObj_t *event) {
	// First entry, make it root
	if(videoEventQueue == NULL) {
		videoEventQueue = calloc(1, sizeof(uiDrawObjQueue_t));
		videoEventQueue->event = event;
		//print_debug("Added first event %08X (type %s)\n", (u32)videoEventQueue, typeStrings[event->type]);
		return event;
	}
	
	uiDrawObjQueue_t *current = videoEventQueue;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = calloc(1, sizeof(uiDrawObjQueue_t));
	current->next->event = event;
	event->disposed = false;
	//print_debug("Added a new event %08X (type %s)\n", (u32)event, typeStrings[event->type]);
	return event;
}

static void clearNestedEvent(uiDrawObj_t *event) {
	if(event && !event->disposed) {
		print_debug("Event was not disposed!!\n");
		print_debug("Event %08X (type %s)\n", (u32)event, typeStrings[event->type]);
	}
	
	if(event->child && event->child != event) {
		clearNestedEvent(event->child);
	}
	//print_debug("Dispose nested event %08X\n", (u32)event);
	if(event && event->data) {
		// Free any attached data
		if(event->type == EV_STYLEDLABEL) {
			if(((drawStyledLabelEvent_t*)event->data)->string) {
				//print_debug("Clear Nested EV_STYLEDLABEL\n");
				free(((drawStyledLabelEvent_t*)event->data)->string);
			}
		}
		else if(event->type == EV_SELECTABLEBUTTON) {
			if(((drawSelectableButtonEvent_t*)event->data)->msg) {
				//print_debug("Clear Nested EV_SELECTABLEBUTTON\n");
				free(((drawSelectableButtonEvent_t*)event->data)->msg);
			}
		}
		else if(event->type == EV_TOOLTIP) {
			if(((drawTooltipEvent_t*)event->data)->tooltip) {
				//print_debug("Clear Nested EV_TOOLTIP\n");
				free(((drawTooltipEvent_t*)event->data)->tooltip);
			}
		}
		//print_debug("Clear Nested event->data\n");
		free(event->data);
	}
	if(event) {
		//print_debug("Clear event\n");
		memset(event, 0, sizeof(uiDrawObj_t));
		free(event);
	}
}

static void disposeEvent(uiDrawObj_t *event) {
	if(videoEventQueue == NULL) {
		return;
	}

	// See if this is in our root event queue
	uiDrawObjQueue_t *current = videoEventQueue->next;
	uiDrawObjQueue_t *previous = videoEventQueue;
	// First node is what we're after.
	while (current != NULL) {
		if(current->event == event) {
			//print_debug("Disposing event %08X\n", (u32)current);
			clearNestedEvent(current->event);
			previous->next = current->next;
			free(current);
		}
		else {
			previous = current;
		}
		current = previous->next;
	}
}


static void init_textures() 
{
	TPL_OpenTPLFromMemory(&imagesTPL, (void *)images_tpl, images_tpl_size);
	TPL_OpenTPLFromMemory(&buttonsTPL, (void *)buttons_tpl, buttons_tpl_size);
	TPL_GetTexture(&imagesTPL, gcdvdsmall, &gcdvdsmallTexObj);
	TPL_GetTextureCI(&imagesTPL, sdsmall, &sdsmallTexObj, &sdsmallTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&sdsmallTexObj, &sdsmallTlutObj);
	TPL_GetTextureCI(&imagesTPL, hddimg, &hddTexObj, &hddTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&hddTexObj, &hddTlutObj);
	TPL_GetTextureCI(&imagesTPL, qoobimg, &qoobTexObj, &qoobTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&qoobTexObj, &qoobTlutObj);
	TPL_GetTexture(&imagesTPL, qoobimg_ind, &qoobIndTexObj);
	TPL_GetTexture(&imagesTPL, wodeimg, &wodeimgTexObj);
	TPL_GetTexture(&imagesTPL, wiikeyimg, &wiikeyTexObj);
	TPL_GetTexture(&imagesTPL, systemimg, &systemTexObj);
	TPL_GetTextureCI(&imagesTPL, memcardimg, &memcardTexObj, &memcardTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&memcardTexObj, &memcardTlutObj);
	TPL_GetTexture(&imagesTPL, memcardimg_ind, &memcardIndTexObj);
	TPL_GetTextureCI(&imagesTPL, usbgeckoimg, &usbgeckoTexObj, &usbgeckoTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&usbgeckoTexObj, &usbgeckoTlutObj);
	TPL_GetTexture(&imagesTPL, bbaimg, &bbaTexObj);
	TPL_GetTexture(&buttonsTPL, boxinner, &boxinnerTexObj);
	TPL_GetTexture(&buttonsTPL, boxouter, &boxouterTexObj);
	TPL_GetTexture(&imagesTPL, ntscjimg, &ntscjTexObj);
	TPL_GetTexture(&imagesTPL, ntscuimg, &ntscuTexObj);
	TPL_GetTexture(&imagesTPL, palimg, &palTexObj);
	TPL_GetTexture(&buttonsTPL, checked_32, &checkedTexObj);
	TPL_GetTexture(&buttonsTPL, unchecked_32, &uncheckedTexObj);
	TPL_GetTexture(&buttonsTPL, loading_16, &loadingTexObj);
	TPL_GetTexture(&imagesTPL, gcloaderimg, &gcloaderTexObj);
	TPL_GetTexture(&imagesTPL, m2loaderimg, &m2loaderTexObj);
	TPL_GetTexture(&imagesTPL, eth2gcimg, &eth2gcTexObj);
	TPL_GetTexture(&imagesTPL, flippyimg, &flippyTexObj);
	TPL_GetTexture(&imagesTPL, gcnetimg, &gcnetTexObj);
	TPL_GetTexture(&imagesTPL, kunaigcimg, &kunaigcTexObj);
	/* These textures stay immutable; publish them before video starts. */
	GX_InvalidateTexAll();
}

static void drawInit()
{
	Mtx44 GXprojection2D;
	Mtx GXmodelView2D;

	// Reset various parameters from gfx plugin
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	guMtxIdentity(GXmodelView2D);
	GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	guOrtho(GXprojection2D, 0, 480, 0, 640, 0, 1);
	UIStage_Project(GXprojection2D);
	GX_LoadProjectionMtx(GXprojection2D, GX_ORTHOGRAPHIC);

	GX_SetZMode(GX_DISABLE,GX_ALWAYS,GX_FALSE);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	//set vertex attribute formats here
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	//enable textures
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordScaleManually(GX_TEXCOORD0, GX_DISABLE, 0, 0);

	GX_SetNumIndStages (0);
	GX_SetNumTevStages (2);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn (GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect (GX_TEVSTAGE0);
	GX_SetTevOrder (GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn (GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASA, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect (GX_TEVSTAGE1);

	//set blend mode
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR); //Fix src alpha
	GX_SetColorUpdate(GX_ENABLE);
//	GX_SetAlphaUpdate(GX_ENABLE);
//	GX_SetDstAlpha(GX_DISABLE, 0xFF);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

static void _drawRect(int x, int y, int width, int height, int depth, GXColor color, float s0, float s1, float t0, float t1)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t0);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t0);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t1);
	GX_End();
}

static void _putFlatVertex(float x, float y, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Position3f32(x, y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}

static void _putFlatRect(float x, float y, float width, float height, GXColor color)
{
	_putFlatVertex(x, y, color);
	_putFlatVertex(x + width, y, color);
	_putFlatVertex(x + width, y + height, color);
	_putFlatVertex(x, y + height, color);
}

static void _DrawSimpleBox(int x, int y, int width, int height, int depth, GXColor fillColor, GXColor borderColor) 
{
	//Adjust for blank texture border
	x-=4; y-=4; width+=8; height+=8;
	
	GX_LoadTexObj(&boxinnerTexObj, GX_TEXMAP0);

	_drawRect(x, y, width/2, height/2, depth, fillColor, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	_drawRect(x+(width/2), y, width/2, height/2, depth, fillColor, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	_drawRect(x, y+(height/2), width/2, height/2, depth, fillColor, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	_drawRect(x+(width/2), y+(height/2), width/2, height/2, depth, fillColor, ((float)width/32), 0.0f, ((float)height/32), 0.0f);

	GX_LoadTexObj(&boxouterTexObj, GX_TEXMAP0);

	_drawRect(x, y, width/2, height/2, depth, borderColor, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	_drawRect(x+(width/2), y, width/2, height/2, depth, borderColor, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	_drawRect(x, y+(height/2), width/2, height/2, depth, borderColor, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	_drawRect(x+(width/2), y+(height/2), width/2, height/2, depth, borderColor, ((float)width/32), 0.0f, ((float)height/32), 0.0f);
}

// Internal
static void _DrawImageNow(int textureId, int x, int y, int width, int height, int depth,
		float s1, float s2, float t1, float t2, int centered, u8 opacity) {
	u16 ss = 0, ts = 0;
	GXTexObj *texObj = NULL;
	GXTexObj *indTexObj = NULL;
	GXColor color = (GXColor) {255,255,255,255};
	
	switch(textureId)
	{
		case TEX_GCDVDSMALL:
			texObj = &gcdvdsmallTexObj;
			break;
		case TEX_SDSMALL:
			texObj = &sdsmallTexObj;
			break;
		case TEX_HDD:
			texObj = &hddTexObj;
			break;
		case TEX_QOOB:
			texObj = &qoobTexObj;
			indTexObj = &qoobIndTexObj;
			ss = 96; ts = 102;
			break;
		case TEX_WODEIMG:
			texObj = &wodeimgTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_USBGECKO:
			texObj = &usbgeckoTexObj;
			break;
		case TEX_WIIKEY:
			texObj = &wiikeyTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_SYSTEM:
			texObj = &systemTexObj;
			break;
		case TEX_MEMCARD:
			texObj = &memcardTexObj;
			indTexObj = &memcardIndTexObj;
			ss = 80; ts = 92;
			break;
		case TEX_BBA:
			texObj = &bbaTexObj;
			break;
		case TEX_CHECKED:
			texObj = &checkedTexObj; color = (GXColor) {0,128,0,255};
			break;
		case TEX_UNCHECKED:
			texObj = &uncheckedTexObj; color = (GXColor) {87,87,87,255};
			ss = 32; ts = 32;
			break;
		case TEX_GCLOADER:
			texObj = &gcloaderTexObj; color = (GXColor) {216,216,216,255};
			ts = 76;
			break;
		case TEX_M2LOADER:
			texObj = &m2loaderTexObj;
			break;
		case TEX_ETH2GC:
			texObj = &eth2gcTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_FLIPPY:
			texObj = &flippyTexObj; color = (GXColor) {216,216,216,255};
			t1 -= 18.0f/40.0f;
			break;
		case TEX_GCNET:
			texObj = &gcnetTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_GCODE:
			texObj = &gcloaderTexObj; color = (GXColor) {216,216,216,255};
			t1 -= 12.0f/88.0f;
			break;
		case TEX_KUNAIGC:
			texObj = &kunaigcTexObj; color = (GXColor) {216,216,216,255};
			break;
	}
	
	if(!ss) ss = GX_GetTexObjWidth(texObj);
	if(!ts) ts = GX_GetTexObjHeight(texObj);
	GX_SetTexCoordScaleManually(GX_TEXCOORD0, GX_ENABLE, ss, ts);
	
	GX_InvalidateTexAll();
	GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
	if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
	GX_LoadTexObj(texObj, GX_TEXMAP0);
	
	if(indTexObj) {
		GX_LoadTexObj(indTexObj, GX_TEXMAP1);
		
		GX_SetNumIndStages(1);
		GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP1);
		GX_SetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_16, GX_ITS_16);
		
		switch(GX_GetTexObjFmt(indTexObj)) {
			case GX_TF_I8:
				GX_SetTevIndTile(GX_TEVSTAGE0, GX_INDTEXSTAGE0, 16, 16, 16, 0, GX_ITF_8, GX_ITM_0, GX_ITB_NONE, GX_ITBA_OFF);
				GX_SetTevIndRepeat(GX_TEVSTAGE1);
				break;
			case GX_TF_IA8:
				GX_SetTevIndTile(GX_TEVSTAGE0, GX_INDTEXSTAGE0, 16, 16, 16, 16, GX_ITF_8, GX_ITM_0, GX_ITB_NONE, GX_ITBA_OFF);
				GX_SetTevIndRepeat(GX_TEVSTAGE1);
				break;
		}
	}
	
	color.a = (u8)(((u16)color.a * opacity) / 255);
	_drawRect(x, y, width, height, depth, color, s1, s2, t1, t2);
}

// Internal
static void _DrawImage(uiDrawObj_t *evt) {
	drawImageEvent_t *data = (drawImageEvent_t*)evt->data;
	_DrawImageNow(data->textureId, data->x, data->y, data->width, data->height,
		data->depth, data->s1, data->s2, data->t1, data->t2, 0, 255);
}

/* The icon Settings chose for each Home face, a choice of that face's own
 * four, in uiHomeFace_t order. Apps, Memory Cards, Emulators and File
 * Browser have their one icon each, drawn while the face shows at all: without them the cube
 * draws exactly what it did. */
static void _HomeFaceIcons(int icons[UI_HOME_FACE_COUNT])
{
	icons[UI_HOME_FACE_LIBRARY] = swissSettings.libraryIcon;
	icons[UI_HOME_FACE_SOURCE] = swissSettings.sourceIcon;
	icons[UI_HOME_FACE_SETTINGS] = swissSettings.settingsIcon;
	icons[UI_HOME_FACE_SYSTEM] = swissSettings.systemIcon;
	for(int face = UI_HOME_FACE_APPS; face < UI_HOME_FACE_COUNT; face++) {
		icons[face] = UIScene_Frame()->homeMotifAlpha[face] > 0.0f ? 0 : -1;
	}
}

static void _DrawBackground(uiDrawObj_t *evt)
{
	/* Wave Speed, in swiss.h's order: Normal, Fast, Slow. */
	static const float waveSpeeds[WAVE_SPEED_MAX] = {1.0f, 3.0f, 0.5f};
	bool decorativeAnimated = _CurrentMotionMode() == UI_MOTION_FULL;
	int icons[UI_HOME_FACE_COUNT];

	if(backgroundCovered) {
		/* Not a pixel of it would show. */
		IndigoBackground_TrackPad(UIAnim_Seconds(),
			decorativeAnimated && UIScene_Frame()->visible, &padInstrument);
		return;
	}
	UI_PERF_BEGIN(backgroundStart);

	(void)evt;
	_HomeFaceIcons(icons);
	/* The glass copies the frame it is drawn in. */
	IndigoBackground_SetFramebuffer(getVideoMode()->fbWidth,
		getVideoMode()->efbHeight);
	IndigoBackground_SetWaveSpeed(waveSpeeds[swissSettings.waveSpeed]);
	IndigoBackground_SetWaves(!swissSettings.hideWaves);
	IndigoBackground_SetIdleSway(swissSettings.idleAnimation != 0);
	IndigoBackground_Draw(UIAnim_Seconds(),
		decorativeAnimated && !swissSettings.disableAnimatedBackdrop,
		decorativeAnimated,
		UIScene_Frame(), &systemInstrument.clock, &padInstrument, icons);
	/* The background uses a raster-only TEV stage; never leak that state. */
	drawInit();
	UI_PERF_END(UI_PERF_METRIC_BACKGROUND_CPU_SUBMIT, backgroundStart);
}

static uiDrawObj_t* DrawBackground(void)
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_BACKGROUND;
	return event;
}
const char *DeviceDisplayName(const DEVICEHANDLER_INTERFACE *device)
{
	return device == &__device_dvd ? "Game Disc" : device->deviceName;
}


// External
uiDrawObj_t* DrawImage(int textureId, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	drawImageEvent_t *eventData = calloc(1, sizeof(drawImageEvent_t));
	eventData->textureId = textureId;
	eventData->x = centered ? ((int) x - width/2) : x;
	eventData->y = y;
	eventData->width = width;
	eventData->height = height;
	eventData->depth = depth;
	eventData->s1 = s1;
	eventData->s2 = s2;
	eventData->t1 = t1;
	eventData->t2 = t2;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_IMAGE;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTexObjNow(GXTexObj *texObj, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	if(GX_GetTexObjMagFilt(texObj) == GX_NEAR) {
		GX_SetNumTevStages(1);
		GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	}
	GX_InvalidateTexAll();
	GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
	if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
	GX_LoadTexObj(texObj, GX_TEXMAP0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t1);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t2);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t2);
	GX_End();
}

// Internal
static void _DrawTexObj(uiDrawObj_t *evt)
{
	drawTexObjEvent_t *data = (drawTexObjEvent_t*)evt->data;
	_DrawTexObjNow(data->texObj, data->x, data->y, data->width, data->height, data->depth, data->s1, data->s2, data->t1, data->t2, 0);
}

// External
uiDrawObj_t* DrawTexObj(GXTexObj *texObj, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	drawTexObjEvent_t *eventData = calloc(1, sizeof(drawTexObjEvent_t));
	eventData->texObj = texObj;
	eventData->x = centered ? ((int) x - width/2) : x;
	eventData->y = y;
	eventData->width = width;
	eventData->height = height;
	eventData->depth = depth;
	eventData->s1 = s1;
	eventData->s2 = s2;
	eventData->t1 = t1;
	eventData->t2 = t2;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TEXOBJ;
	event->data = eventData;
	return event;
}

/* The top corner Clock or Temperature puts its instrument in: 1 the right
 * (the default), -1 the left, 0 neither. */
static int _Corner(int position)
{
	switch(position) {
		case CLOCK_LEFT: return -1;
		case CLOCK_OFF: return 0;
		default: return 1;
	}
}

/* The top corner the header's other items take (the loading spinner, the
 * Source picker's label): the left while the time and the dial leave it,
 * else the right; when they hold a corner each, the dial's, inside it. */
static int _FreeCorner(float *inset)
{
	int clock = _Corner(swissSettings.clockPosition);
	int dial = _Corner(swissSettings.temperaturePosition);

	*inset = 0.0f;
	if(clock != -1 && dial != -1) return -1;
	if(clock != 1 && dial != 1) return 1;
	*inset = 48.0f;
	return dial;
}

// Internal
static void _DrawProgressBar(uiDrawObj_t *evt) {
	
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));

	if(data->hidden) {
		return;
	}
	data->seconds += UIAnim_Delta();
	if(data->miniMode) {	
		int x = 30, y = 420, corner = -1;
		/* The header's free corner, level with the clock and as far in from
		 * the edge the frame shows; the word on the wheel's inner side. */
		if(data->miniModePos == PROGRESS_BOX_TOPLEFT) {
			float inset;
			corner = _FreeCorner(&inset);
			x = (int)(corner > 0 ? UIStage_Right() - 44.0f - inset : UIStage_Left() + 44.0f + inset);
			y = 43;
		}
		/* The File Browser's: in its info bar's corner, the word inside. */
		else if(data->miniModePos == PROGRESS_BOX_FILES) {
			corner = 1;
			x = (int)UIStage_Right() - 56;
			y = 418;
		}
		GXColor loadingColor = (GXColor) {255,255,255,(u8)data->miniModeAlpha};
		/* In seconds, the same at 50 Hz and 60 Hz: a turn of the eight
		 * segments every 5/6 s, a fade in or out over 1.4 s. */
		int numSegments = (int)(data->seconds * 9.6f) % 8;
		if(data->speed != 0) {
			data->miniModeAlpha = MIN(255.0f, data->miniModeAlpha + UIAnim_Delta() * 180.0f);
		}
		else {
			data->miniModeAlpha = MAX(0.0f, data->miniModeAlpha - UIAnim_Delta() * 180.0f);
		}
		GX_InvalidateTexAll();
		GX_LoadTexObj(&loadingTexObj, GX_TEXMAP0);
		_drawRect(x-8, y-8, 16, 16, 0, loadingColor, (float) (numSegments)/8, (float) (numSegments+1)/8, 0.0f, 1.0f);
		drawString(x - 8 * corner, y, "Loading\205", 0.55f,
			corner > 0 ? ALIGN_RIGHT : ALIGN_LEFT, loadingColor);
		return;
	}
	if(data->files) {
		_SaveCubesBar(UIStage_Left(), 0.0f, UIStage_Right() - UIStage_Left(), 480.0f,
			(GXColor) {8, 12, 27, 200});
	}
	_DrawDialogCard(x1, y1, x2-x1, y2-y1, -1);
	if(data->files) {
		_DrawHintText(x1 + 16, y2 - 12, "B  Stop", 0.46f, ALIGN_LEFT, defaultColor);
	}

	int middleY = (y2+y1)/2;
	if(data->indeterminate) {
		/* There and back every 3 1/3 s. */
		data->percent = (int)fmodf(data->seconds * 120.0f, 400.0f);
		int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
		int progressBarWidth = multiplier*100;
		int progressStart = 0;
		int progressSize = 0;
		if(data->percent < 100) {
			progressStart = 0;
			progressSize = data->percent%100;
		}
		else if(data->percent >= 100 && data->percent < 200) {
			progressStart = (data->percent%100);
			progressSize = 100-progressStart;
		}
		else if(data->percent >= 200 && data->percent < 300) {
			progressStart = 100-(data->percent%100);
			progressSize = 100-progressStart;
		}
		else {
			progressStart = 0;
			progressSize = 100-(data->percent%100);
		}
		
		_DrawDialogBar(640/2 - progressBarWidth/2, y1+20, progressBarWidth,
			progressStart*multiplier, progressSize*multiplier);
	}
	else {
		int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
		int progressBarWidth = multiplier*100;
		_DrawDialogBar(640/2 - progressBarWidth/2, y1+20, progressBarWidth, 0,
			multiplier*MIN(data->percent, 100));
		sprintf(fbTextBuffer,"%d%%", data->percent);
		bool displaySpeed = data->speed != 0;
		drawString(displaySpeed ? (x1 + 80) : (640/2), middleY+30, fbTextBuffer, 1.0f, ALIGN_CENTER, defaultColor);
		if(displaySpeed) {
			formatBytes(fbTextBuffer, data->speed, 0, true);
			strcat(fbTextBuffer, "/s");
			drawString(x1 + 280, middleY+30, fbTextBuffer, 1.0f, ALIGN_CENTER, defaultColor);
			sprintf(fbTextBuffer,"Elapsed: %02i:%02i:%02i", data->timestart / 3600, (data->timestart / 60)%60,  data->timestart % 60);
			drawString(x1 + 500, middleY+30, fbTextBuffer, 0.65f, ALIGN_CENTER, defaultColor);
			sprintf(fbTextBuffer,"Remain: %02i:%02i:%02i", data->timeremain / 3600, (data->timeremain / 60)%60,  data->timeremain % 60);
			drawString(x1 + 500, middleY+45, fbTextBuffer, 0.65f, ALIGN_CENTER, defaultColor);
		}
	}	
}

// External
uiDrawObj_t* DrawProgressBar(bool indeterminate, int percent, const char *message) {
	drawProgressEvent_t *eventData = calloc(1, sizeof(drawProgressEvent_t));
	eventData->percent = percent;
	eventData->indeterminate = indeterminate;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_PROGRESS;
	event->data = eventData;
	if(_LaunchTakesBar(indeterminate) && DrawLaunchStep(message)) {
		eventData->hidden = true;
		return event;
	}
	if(message && strlen(message) > 0) {
		sprintf(txtbuffer, "%s", message);
		// Add child component(s) for label(s)
		char *tok = strtok(txtbuffer,"\n");
		int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
		int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
		int middleY = (y2+y1)/2;
		while(tok != NULL) {
			DrawAddChild(event, DrawStyledLabel(640/2, middleY, tok, 1.0f, ALIGN_CENTER, defaultColor));
			tok = strtok(NULL,"\n");
			middleY+=24;
		}
	}
	return event;
}

uiDrawObj_t* DrawProgressLoading(int miniModePos) {
	drawProgressEvent_t *eventData = calloc(1, sizeof(drawProgressEvent_t));
	eventData->miniMode = true;
	eventData->miniModePos = miniModePos;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_PROGRESS;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawMessageBox(uiDrawObj_t *evt) {
	drawMsgBoxEvent_t *data = (drawMsgBoxEvent_t*)evt->data;
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));

	_DrawDialogCard(x1, y1, x2-x1, y2-y1, data->type);
}

// External
uiDrawObj_t* DrawMessageBox(int type, const char *msg)
{
	drawMsgBoxEvent_t *eventData = calloc(1, sizeof(drawMsgBoxEvent_t));
	eventData->type = type;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_MSGBOX;
	event->data = eventData;
	
	// Add child component(s) for label(s)
	sprintf(txtbuffer, "%s", msg);
	/* "Press A to continue." and the like become a line of button icons. */
	char hint[UI_HINT_LABEL_CAPACITY];
	bool hasHint = UIHint_SplitPrompt(txtbuffer, hint, sizeof(hint)) != 0;
	char *tok = strtok(txtbuffer,"\n");
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int middleY = y2-y1 < 23 ? y1+3 : (y2+y1)/2-12;
	while(tok != NULL) {
		uiDrawObj_t *lineLabel = DrawStyledLabel(640/2, middleY, tok, 1.0f, ALIGN_CENTER, defaultColor);
		tok = strtok(NULL,"\n");
		middleY+=24;
		DrawAddChild(event, lineLabel);
	}
	if(hasHint) {
		DrawAddChild(event, DrawHintLabel(640/2, middleY, hint, 0.8f, ALIGN_CENTER, defaultColor));
	}
	
	return event;
}

static GXColor _PresentationAccent(uiPresentationKind_t kind)
{
	switch(kind) {
		case UI_PRESENTATION_EMPTY:
			return (GXColor) {142, 128, 202, 255};
		case UI_PRESENTATION_LOADING:
			return (GXColor) {182, 170, 242, 255};
		case UI_PRESENTATION_RECOVERABLE_ERROR:
			return (GXColor) {243, 126, 145, 255};
		case UI_PRESENTATION_INFORMATION:
			return (GXColor) {117, 181, 218, 255};
		default:
			return (GXColor) {142, 128, 202, 255};
	}
}

static void _DrawPresentation(uiDrawObj_t *evt)
{
	drawPresentationEvent_t *data = (drawPresentationEvent_t*)evt->data;
	GXColor transparent = {0, 0, 0, 0};
	GXColor scrim = {4, 3, 15, 184};
	GXColor shadow = {2, 1, 10, 194};
	GXColor panel = {10, 8, 31, 246};
	GXColor border = {135, 122, 199, 205};
	GXColor primary = {244, 239, 255, 255};
	GXColor secondary = {184, 174, 225, 238};
	GXColor muted = {153, 145, 190, 225};
	GXColor accent;
	uiMotionMode_t motionMode;
	u32 activeCell = 0u;
	u32 i;
	int scrimLeft;

	if(data == NULL) {
		return;
	}
	accent = _PresentationAccent(data->snapshot.kind);
	motionMode = _CurrentMotionMode();
	if(data->snapshot.kind == UI_PRESENTATION_LOADING &&
		motionMode != UI_MOTION_OFF) {
		float rate = motionMode == UI_MOTION_REDUCED ? 2.0f : 5.0f;

		activeCell = (u32)(UIAnim_Seconds() * rate) % 3u;
	}

	drawInit();
	/* The scrim covers the whole frame, the widescreen margins too. */
	scrimLeft = (int)floorf(UIStage_Left());
	_DrawSimpleBox(scrimLeft, 0, (int)ceilf(UIStage_Right()) - scrimLeft, 480, 0,
		scrim, transparent);
	_DrawSimpleBox(68, 115, 504, 254, 0, shadow, transparent);
	_DrawSimpleBox(72, 111, 496, 254, 0, panel, border);
	_DrawSimpleBox(72, 111, 6, 254, 0, accent, transparent);
	_DrawSimpleBox(102, 202, 436, 1, 0,
		(GXColor) {135, 122, 199, 118}, transparent);

	/* Three code-native cells make state visible without relying on hue. The
	 * loading variant advances one bright cell; all other kinds stay still. */
	for(i = 0u; i < 3u; ++i) {
		GXColor cell = accent;

		if(data->snapshot.kind == UI_PRESENTATION_LOADING &&
			motionMode != UI_MOTION_OFF) {
			cell.a = i == activeCell ? 255u : 72u;
		}
		else if(data->snapshot.kind == UI_PRESENTATION_LOADING) {
			cell.a = 172u;
		}
		else {
			cell.a = (u8)(216u - i * 48u);
		}
		_DrawSimpleBox(492 + (int)i * 16, 143, 10, 10, 0, cell,
			transparent);
	}

	drawStringMedium(104, 151,
		UIPresentation_KindLabel(data->snapshot.kind), 0.38f, ALIGN_LEFT,
		accent);
	drawStringMedium(104, 185, data->snapshot.title, data->titleScale,
		ALIGN_LEFT, primary);
	drawStringMedium(104, 231, data->snapshot.message, data->messageScale,
		ALIGN_LEFT, secondary);
	if(data->snapshot.detail[0] != '\0') {
		drawStringMedium(104, 263, data->snapshot.detail, data->detailScale,
			ALIGN_LEFT, muted);
	}
	if(data->snapshot.action[0] != '\0') {
		drawStringMedium(320, 332, data->snapshot.action, data->actionScale,
			ALIGN_CENTER, primary);
	}
	drawInit();
}

static bool _PreparePresentation(drawPresentationEvent_t *data,
	const uiPresentationSnapshot_t *snapshot)
{
	if(data == NULL || !UIPresentation_Valid(snapshot)) {
		return false;
	}
	memset(data, 0, sizeof(*data));
	data->snapshot.kind = snapshot->kind;
	data->titleScale = UIHomeText_CopyFitted(data->snapshot.title,
		sizeof(data->snapshot.title), snapshot->title, 392, 0.76f,
		GetTextSizeInPixels, NULL);
	data->messageScale = UIHomeText_CopyFitted(data->snapshot.message,
		sizeof(data->snapshot.message), snapshot->message, 432, 0.54f,
		GetTextSizeInPixels, NULL);
	data->detailScale = UIHomeText_CopyFitted(data->snapshot.detail,
		sizeof(data->snapshot.detail), snapshot->detail, 432, 0.46f,
		GetTextSizeInPixels, NULL);
	data->actionScale = UIHomeText_CopyFitted(data->snapshot.action,
		sizeof(data->snapshot.action), snapshot->action, 432, 0.50f,
		GetTextSizeInPixels, NULL);
	return UIPresentation_Valid(&data->snapshot);
}

uiDrawObj_t* DrawPresentation(const uiPresentationSnapshot_t *snapshot)
{
	drawPresentationEvent_t *eventData;
	uiDrawObj_t *event;

	if(!UIPresentation_Valid(snapshot)) {
		return NULL;
	}
	eventData = calloc(1, sizeof(*eventData));
	event = calloc(1, sizeof(*event));
	if(eventData == NULL || event == NULL ||
		!_PreparePresentation(eventData, snapshot)) {
		free(eventData);
		free(event);
		return NULL;
	}
	event->type = EV_PRESENTATION;
	event->data = eventData;
	return event;
}

bool DrawUpdatePresentation(uiDrawObj_t *evt,
	const uiPresentationSnapshot_t *snapshot)
{
	drawPresentationEvent_t next;
	bool updated = false;

	if(evt == NULL || !_PreparePresentation(&next, snapshot)) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	if(!evt->disposed && evt->type == EV_PRESENTATION && evt->data != NULL) {
		memcpy(evt->data, &next, sizeof(next));
		updated = true;
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

/* Save details has its own hierarchy; other retained dialogs stay compact. */
static void _DrawSaveDetails(uiDrawObj_t *evt)
{
	const drawSaveDetailsEvent_t *data = evt->data;
	const GXColor transparent = {0, 0, 0, 0};
	const GXColor primary = {244, 239, 255, 255};
	const GXColor secondary = {204, 193, 239, 255};
	const GXColor accent = {176, 153, 248, 255};
	int scrimLeft;

	if(data == NULL) {
		return;
	}
	drawInit();
	scrimLeft = (int)floorf(UIStage_Left());
	_DrawSimpleBox(scrimLeft, 0, (int)ceilf(UIStage_Right()) - scrimLeft, 480,
		0, (GXColor) {4, 3, 15, 184}, transparent);
	_DrawSimpleBox(68, 95, 504, 312, 0,
		(GXColor) {2, 1, 10, 194}, transparent);
	_DrawSimpleBox(72, 91, 496, 312, 0,
		(GXColor) {10, 8, 31, 250}, (GXColor) {135, 122, 199, 220});
	_DrawSimpleBox(72, 91, 5, 312, 0, accent, transparent);
	_DrawSimpleBox(104, 174, 204, 61, 0,
		(GXColor) {33, 25, 67, 255}, (GXColor) {103, 87, 166, 180});
	_DrawSimpleBox(332, 174, 204, 61, 0,
		(GXColor) {33, 25, 67, 255}, (GXColor) {103, 87, 166, 180});
	_DrawSimpleBox(104, 345, 432, 1, 0,
		(GXColor) {135, 122, 199, 160}, transparent);

	drawStringMedium(104, 119, "SAVE DETAILS", 0.48f, ALIGN_LEFT, accent);
	if(data->snapshot.estimated) {
		drawStringMedium(536, 119, "Estimated size", 0.48f, ALIGN_RIGHT, secondary);
	}
	drawStringMedium(104, 150, data->snapshot.name, data->nameScale,
		ALIGN_LEFT, primary);
	drawStringMedium(128, 197, data->blocks, data->blocksScale,
		ALIGN_LEFT, primary);
	drawStringMedium(128, 221, "Blocks", 0.48f, ALIGN_LEFT, secondary);
	drawStringMedium(356, 197, data->kib, data->kibScale,
		ALIGN_LEFT, primary);
	drawStringMedium(356, 221, "KiB", 0.48f, ALIGN_LEFT, secondary);
	drawStringMedium(104, 257, "Source", 0.54f, ALIGN_LEFT, secondary);
	drawStringMedium(250, 257, data->snapshot.source, data->sourceScale,
		ALIGN_LEFT, primary);
	drawStringMedium(104, 287, "Last updated", 0.54f, ALIGN_LEFT, secondary);
	drawStringMedium(250, 287, data->snapshot.updated, data->updatedScale,
		ALIGN_LEFT, primary);
	drawStringMedium(104, 317, "Save icon", 0.54f, ALIGN_LEFT, secondary);
	drawStringMedium(250, 317, data->snapshot.icon, data->iconScale,
		ALIGN_LEFT, primary);
	_DrawHintText(216, 373, "A  Actions", 0.60f, ALIGN_CENTER, primary);
	_DrawHintText(412, 373, "B  Back", 0.60f, ALIGN_CENTER, primary);
	drawInit();
}

static bool _PrepareSaveDetails(drawSaveDetailsEvent_t *data,
	const uiSaveDetailsSnapshot_t *snapshot)
{
	if(data == NULL || !UISaveDetails_Valid(snapshot)) {
		return false;
	}
	memset(data, 0, sizeof(*data));
	data->snapshot.blocks = snapshot->blocks;
	data->snapshot.estimated = snapshot->estimated;
	data->nameScale = UIHomeText_CopyFitted(data->snapshot.name,
		sizeof(data->snapshot.name), snapshot->name, 432, 0.86f,
		GetTextSizeInPixels, NULL);
	data->sourceScale = UIHomeText_CopyFitted(data->snapshot.source,
		sizeof(data->snapshot.source), snapshot->source, 286, 0.60f,
		GetTextSizeInPixels, NULL);
	data->updatedScale = UIHomeText_CopyFitted(data->snapshot.updated,
		sizeof(data->snapshot.updated), snapshot->updated, 286, 0.60f,
		GetTextSizeInPixels, NULL);
	data->iconScale = UIHomeText_CopyFitted(data->snapshot.icon,
		sizeof(data->snapshot.icon), snapshot->icon, 286, 0.60f,
		GetTextSizeInPixels, NULL);
	snprintf(data->blocks, sizeof(data->blocks), "%u", (unsigned)snapshot->blocks);
	snprintf(data->kib, sizeof(data->kib), "%u", (unsigned)(snapshot->blocks * 8u));
	data->blocksScale = UIHomeText_FitScale(data->blocks, 156, 0.92f,
		GetTextSizeInPixels);
	data->kibScale = UIHomeText_FitScale(data->kib, 156, 0.92f,
		GetTextSizeInPixels);
	return UISaveDetails_Valid(&data->snapshot);
}

uiDrawObj_t* DrawSaveDetails(const uiSaveDetailsSnapshot_t *snapshot)
{
	drawSaveDetailsEvent_t *eventData;
	uiDrawObj_t *event;

	if(!UISaveDetails_Valid(snapshot)) {
		return NULL;
	}
	eventData = calloc(1, sizeof(*eventData));
	event = calloc(1, sizeof(*event));
	if(eventData == NULL || event == NULL ||
		!_PrepareSaveDetails(eventData, snapshot)) {
		free(eventData);
		free(event);
		return NULL;
	}
	event->type = EV_SAVE_DETAILS;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawSelectableButton(uiDrawObj_t *evt) {
	drawSelectableButtonEvent_t *data = (drawSelectableButtonEvent_t*)evt->data;
	int x1 = data->x1;
	int x2 = data->x2;
	GXColor selectColor = (GXColor) {96,107,164,GUI_MSGBOX_ALPHA}; //bluish
	GXColor noColor = (GXColor) {0,0,0,0}; //black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	int borderSize = 4;
	//determine length of the text ourselves if x2 == -1
	x2 = (x2 == -1) ? GetTextSizeInPixels(data->msg)+x1+(borderSize*2)+6 : x2;
	//Draw Text and backfill (if selected)
	if(data->mode==B_SELECTED) {
		_DrawSimpleBox( x1, data->y1, x2-x1, data->y2-data->y1+2, 0, selectColor, borderColor);
	}
	else {
		_DrawSimpleBox( x1, data->y1, x2-x1, data->y2-data->y1+2, 0, noColor, borderColor);
	}
	
	if(data->msg) {
		float scale = GetTextScaleToFitInWidth(data->msg, (x2-x1)-(borderSize*2)-6);
		// Adjust font when we can't fit vertically too
		int availHeight = data->y2 - data->y1 - 4;
		if(GetFontHeight(scale) > availHeight) {
			int fullHeight = GetFontHeight(1.0f);
			scale = (float)availHeight / (float)fullHeight;
		}
		drawString(data->x1+borderSize+3, data->y1+(data->y2-data->y1)/2, data->msg, scale, ALIGN_LEFT, defaultColor);
	}
}

// External
uiDrawObj_t* DrawSelectableButton(int x1, int y1, int x2, int y2, const char *message, int mode)
{	
	drawSelectableButtonEvent_t *eventData = calloc(1, sizeof(drawSelectableButtonEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->mode = mode;
	if(message) {
		eventData->msg = strdup(message);
	}
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_SELECTABLEBUTTON;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTooltip(uiDrawObj_t *evt) {

	drawTooltipEvent_t *data = (drawTooltipEvent_t*)evt->data;

	if(data->tooltip/* && data->tooltiptime >= 50*/) {
		//int alpha = data->tooltiptime*4 > 255 ? 255 : data->tooltiptime;
		int alpha = 255;
		int borderSize = 4;
		GXColor borderColorTT = (GXColor) {255,255,255,alpha};
		GXColor backColorTT = (GXColor) {122,122,122,alpha}; //grey
		int numLines = 1;
		char *strPtr = data->tooltip;
		for (numLines=1; strPtr[numLines]; strPtr[numLines]=='\n' ? numLines++ : *strPtr++);
		int height = numLines*26;
		int tooltipY1 = (getVideoMode()->efbHeight / 2) - (height/2);
		// TODO centre on Y based on total size.
		int tooltipX1 = 25, tooltipX2 = getVideoMode()->fbWidth-25, tooltipY2 = tooltipY1+height;
		_DrawSimpleBox( tooltipX1, tooltipY1-6, tooltipX2-tooltipX1, (tooltipY2-tooltipY1)+6, 0, backColorTT, borderColorTT);
		
		// Write each line
		strPtr = data->tooltip;
		int curLine = 0;
		while(numLines) {
			float scale = GetTextScaleToFitInWidthWithMax(strPtr, (tooltipX2-tooltipX1)-(borderSize*2)-6, 0.75f);
			drawString(tooltipX1+borderSize+3, tooltipY1+11+(curLine*25), strPtr, scale, ALIGN_LEFT, borderColorTT);
			numLines--;
			curLine++;
			// Increment to the next line if we have one.
			if(numLines > 0) {
				while(*strPtr != '\n') strPtr++;
				strPtr++;
			}
		}
	}
}

// External
uiDrawObj_t* DrawTooltip(const char *tooltip) {
	drawTooltipEvent_t *eventData = calloc(1, sizeof(drawTooltipEvent_t));
	if(tooltip && strlen(tooltip) > 0) {
		eventData->tooltip = strdup(tooltip);
	}
	else {
		eventData->tooltip = NULL;
	}
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TOOLTIP;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawStyledLabel(uiDrawObj_t *evt) {
	drawStyledLabelEvent_t *data = (drawStyledLabelEvent_t*)evt->data;
	const char *string = data->getString ? data->getString() : data->string;
	
	if(data->showCaret) {
		// blink the caret
		if(data->fadingDirection) {
			data->caretColor.a += (data->fadingDirection * 20);
			if(data->caretColor.a >= 255) { data->fadingDirection = -1; data->caretColor.a = 255; }
			else if(data->caretColor.a <= 15) { data->fadingDirection = 1; data->caretColor.a = 0; }
		}		
		drawStringWithCaret(data->x, data->y, string, data->size, data->align, data->color, data->caretPosition, data->caretColor);
	}
	else {
		if(data->fadingDirection) {
			data->color.a += data->fadingDirection;
			if(data->color.a >= 255) data->fadingDirection = -1;
			else if(data->color.a <= 15) data->fadingDirection = 1;
		}
		if(data->hint) {
			_DrawHintText(data->x, data->y, string, data->size, data->align,
				data->color);
		}
		else {
			drawStringMedium(data->x, data->y, string, data->size, data->align,
				data->color);
		}
	}
}

// External
uiDrawObj_t* DrawStyledLabel(int x, int y, const char *string, float size, int align, GXColor color)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	if(string && strlen(string) > 0) {
		eventData->string = strdup(string);
	}
	else {
		eventData->string = NULL;
	}
	eventData->size = size;
	eventData->align = align;
	eventData->color = color;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External: a label whose button names ("A  OPEN") draw as button icons.
uiDrawObj_t* DrawHintLabel(int x, int y, const char *string, float size, int align, GXColor color)
{
	uiDrawObj_t *event = DrawStyledLabel(x, y, string, size, align, color);
	((drawStyledLabelEvent_t*)event->data)->hint = true;
	return event;
}

// External
uiDrawObj_t* DrawStyledLabelWithCaret(int x, int y, const char *string, float size, int align, GXColor color, int caretPosition)
{	
	uiDrawObj_t *event = DrawStyledLabel(x, y, string, size, align, color);
	drawStyledLabelEvent_t *eventData = (drawStyledLabelEvent_t*)event->data;
	eventData->caretPosition = caretPosition;
	eventData->showCaret = true;
	eventData->fadingDirection = 1;
	eventData->caretColor = eventData->color;
	return event;
}

// External
uiDrawObj_t* DrawLabel(int x, int y, const char *string)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	if(string && strlen(string) > 0) {
		eventData->string = strdup(string);
	}
	else {
		eventData->string = NULL;
	}
	eventData->size = 1.0f;
	eventData->align = ALIGN_LEFT;
	eventData->color = defaultColor;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawFadingLabel(int x, int y, const char *string, float size)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	if(string && strlen(string) > 0) {
		eventData->string = strdup(string);
	}
	else {
		eventData->string = NULL;
	}
	eventData->size = size;
	eventData->align = ALIGN_LEFT;
	eventData->color = (GXColor) {255, 255, 255, 0};
	eventData->fadingDirection = 1;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawDynamicLabel(int x, int y, const char *(*getString)(void), float size, int align, GXColor color)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	eventData->getString = getString;
	eventData->size = size;
	eventData->align = align;
	eventData->color = color;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External (this is used to tie objects together, think of it as an invisible panel)
uiDrawObj_t* DrawContainer()
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_CONTAINER;
	return event;
}

// Internal
static void _DrawEmptyBox(uiDrawObj_t *evt) {
	drawBoxEvent_t *data = (drawBoxEvent_t*)evt->data;
	
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //Silver
	
	_DrawSimpleBox(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 0, data->backfill, borderColor);
}

// External
uiDrawObj_t* DrawEmptyBox(int x1, int y1, int x2, int y2) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;
	
	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->backfill = (GXColor) {0,0,0, swissSettings.disablePanelTransparency ? GUI_MSGBOX_ALPHA : GUI_PANEL_ALPHA}; //Black — Phase 2: translucent unless disabled
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_EMPTYBOX;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawEmptyColouredBox(int x1, int y1, int x2, int y2, GXColor colour) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;
	
	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->backfill = colour;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_EMPTYBOX;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTransparentBox(uiDrawObj_t *evt) {
	drawBoxEvent_t *data = (drawBoxEvent_t*)evt->data;
	
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //Silver
	
	_DrawSimpleBox( data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 0, data->backfill, borderColor);
}

// External
uiDrawObj_t* DrawTransparentBox(int x1, int y1, int x2, int y2) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;

	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->backfill = (GXColor) {0,0,0,0};
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TRANSPARENTBOX;
	event->data = eventData;
	return event;
}

typedef struct systemDialPoint {
	float x;
	float y;
} systemDialPoint_t;

static const systemDialPoint_t systemDialCircle[24] = {
	{1.000000f, 0.000000f}, {0.965926f, 0.258819f},
	{0.866025f, 0.500000f}, {0.707107f, 0.707107f},
	{0.500000f, 0.866025f}, {0.258819f, 0.965926f},
	{0.000000f, 1.000000f}, {-0.258819f, 0.965926f},
	{-0.500000f, 0.866025f}, {-0.707107f, 0.707107f},
	{-0.866025f, 0.500000f}, {-0.965926f, 0.258819f},
	{-1.000000f, 0.000000f}, {-0.965926f, -0.258819f},
	{-0.866025f, -0.500000f}, {-0.707107f, -0.707107f},
	{-0.500000f, -0.866025f}, {-0.258819f, -0.965926f},
	{0.000000f, -1.000000f}, {0.258819f, -0.965926f},
	{0.500000f, -0.866025f}, {0.707107f, -0.707107f},
	{0.866025f, -0.500000f}, {0.965926f, -0.258819f}
};

static void _SetupRasterColor(void)
{
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

/* ------------------------------------------------------------------------
 * Button icons in hint lines. "A  OPEN" draws the A button, then "OPEN",
 * like a game's on-screen prompts. Shapes are drawn like the title-bar dial:
 * a solid core and a one-pixel feathered rim instead of MSAA. Colours are
 * the GameCube controller's; letters use the console's own font.
 * --------------------------------------------------------------------- */
#define HINT_POINTS_MAX 40
#define HINT_QUARTER_TURN 1.5707963f

static void _HintVertex(float x, float y, GXColor color)
{
	GX_Position3f32(x, y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}

/* A convex outline round (cx, cy): a fan for the core, then a rim pushed
 * out one pixel along each corner's normal, fading to clear. */
static void _HintShape(float cx, float cy, const float (*points)[2], int count,
	GXColor color)
{
	const float (*normals)[2] = UIHintGeometry_Normals(cx, cy, points, count);
	GXColor clear = color;
	int i;

	if(normals == NULL) return;
	clear.a = 0;
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, (u16)(count + 2));
	_HintVertex(cx, cy, color);
	for(i = 0; i <= count; i++) {
		_HintVertex(points[i % count][0], points[i % count][1], color);
	}
	GX_End();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)((count + 1) * 2));
	for(i = 0; i <= count; i++) {
		const float *here = points[i % count];
		float nx = normals[i % count][0];
		float ny = normals[i % count][1];
		_HintVertex(here[0], here[1], color);
		_HintVertex(here[0] + nx, here[1] + ny, clear);
	}
	GX_End();
}

/* A rectangle with round corners; radius = half the height makes a pill. */
#define HINT_CORNER_STEPS 6
static void _HintRoundRect(float cx, float cy, float width, float height,
	float radius, GXColor color)
{
	/* The corners' points on a unit circle, worked out on the first call:
	 * cosf and sinf are software on the console, and every disc, pill and
	 * badge in a hint line draws through here each frame. */
	static float unit[4 * (HINT_CORNER_STEPS + 1)][2];
	static bool unitReady = false;
	float points[HINT_POINTS_MAX][2];
	float halfW = width / 2.0f - radius;
	float halfH = height / 2.0f - radius;
	int corner, i, count = 0;

	if(!unitReady) {
		for(corner = 0; corner < 4; corner++) {
			for(i = 0; i <= HINT_CORNER_STEPS; i++) {
				float angle = HINT_QUARTER_TURN *
					((float)corner + (float)i / (float)HINT_CORNER_STEPS);

				unit[count][0] = cosf(angle);
				unit[count][1] = sinf(angle);
				count++;
			}
		}
		unitReady = true;
		count = 0;
	}
	for(corner = 0; corner < 4; corner++) {
		float ox = (corner == 0 || corner == 3) ? halfW : -halfW;
		float oy = (corner < 2) ? halfH : -halfH;

		for(i = 0; i <= HINT_CORNER_STEPS; i++) {
			points[count][0] = cx + ox + radius * unit[count][0];
			points[count][1] = cy + oy + radius * unit[count][1];
			count++;
		}
	}
	_HintShape(cx, cy, points, count, color);
}

static void _HintDisc(float cx, float cy, float radius, GXColor color)
{
	_HintRoundRect(cx, cy, radius * 2.0f, radius * 2.0f, radius, color);
}

static GXColor _HintAlpha(GXColor color, u8 alpha)
{
	color.a = (u8)(((u16)color.a * alpha) / 255u);
	return color;
}

/* A regular octagon, flat side up: the control stick's gate. */
static void _HintOctagon(float cx, float cy, float radius, GXColor color)
{
	float points[8][2];
	int i;

	for(i = 0; i < 8; i++) {
		float angle = HINT_QUARTER_TURN * ((float)i + 0.5f) / 2.0f;

		points[i][0] = cx + radius * cosf(angle);
		points[i][1] = cy + radius * sinf(angle);
	}
	_HintShape(cx, cy, points, 8, color);
}

static void _HintLetter(float cx, float cy, const char *letter, float scale,
	GXColor color)
{
	drawStringMediumUntinted((int)(cx + 0.5f), (int)(cy + 0.5f), letter, scale,
		ALIGN_CENTER, color);
	drawInit();
	_SetupRasterColor();
}

/* One icon centred on (cx, cy), size tall and width wide. Letters are about
 * the label's own size so they stay legible on a 480-line screen. */
static void _DrawHintGlyph(uiHintGlyph_t glyph, float cx, float cy, float size,
	float width, float scale, u8 alpha)
{
	const GXColor grey = _HintAlpha((GXColor) {202, 202, 214, 255}, alpha);
	const GXColor ink = _HintAlpha((GXColor) {42, 40, 56, 255}, alpha);
	const GXColor white = _HintAlpha((GXColor) {255, 255, 255, 255}, alpha);

	drawInit();
	_SetupRasterColor();
	switch(glyph) {
		case UI_HINT_GLYPH_A:
			_HintDisc(cx, cy, width / 2.0f, _HintAlpha((GXColor) {0, 170, 122, 255}, alpha));
			_HintLetter(cx, cy, "A", scale, white);
			break;
		case UI_HINT_GLYPH_B:
			_HintDisc(cx, cy, width / 2.0f, _HintAlpha((GXColor) {222, 48, 56, 255}, alpha));
			_HintLetter(cx, cy, "B", scale * 0.9f, white);
			break;
		case UI_HINT_GLYPH_X:
			_HintRoundRect(cx, cy, width, size, width / 2.0f, grey);
			_HintLetter(cx, cy, "X", scale * 0.9f, ink);
			break;
		case UI_HINT_GLYPH_Y:
			_HintRoundRect(cx, cy, width, size * 0.78f, size * 0.39f, grey);
			_HintLetter(cx, cy, "Y", scale * 0.9f, ink);
			break;
		case UI_HINT_GLYPH_Z:
			_HintRoundRect(cx, cy, width, size * 0.8f, size * 0.22f,
				_HintAlpha((GXColor) {122, 86, 214, 255}, alpha));
			_HintLetter(cx, cy, "Z", scale * 0.86f, white);
			break;
		case UI_HINT_GLYPH_L:
		case UI_HINT_GLYPH_R:
			_HintRoundRect(cx, cy, width, size * 0.8f, size * 0.26f, grey);
			_HintLetter(cx, cy, glyph == UI_HINT_GLYPH_L ? "L" : "R",
				scale * 0.86f, ink);
			break;
		case UI_HINT_GLYPH_START:
			_HintRoundRect(cx, cy, width, size * 0.86f, size * 0.43f, grey);
			_HintLetter(cx, cy, "START", scale * UI_HINT_START_TEXT_SCALE, ink);
			break;
		case UI_HINT_GLYPH_STICK:
			/* The stick from above: its octagonal gate, then the cap. */
			_HintOctagon(cx, cy, size * 0.54f,
				_HintAlpha((GXColor) {104, 102, 124, 255}, alpha));
			_HintDisc(cx, cy, size * 0.3f, grey);
			break;
		case UI_HINT_GLYPH_DPAD:
			_HintRoundRect(cx, cy, size, size * 0.36f, size * 0.08f, grey);
			_HintRoundRect(cx, cy, size * 0.36f, size, size * 0.08f, grey);
			break;
		default:
			break;
	}
}

int GetHintSizeInPixels(const char *text)
{
	return (int)ceilf(UIHint_LineWidth(text, GetFontHeight(1.0f), 1.0f,
		GetTextSizeInPixels));
}

/* The largest scale up to max at which a hint line fits width, like
 * GetTextScaleToFitInWidthWithMax with the button icons measured. */
float GetHintScaleToFitInWidthWithMax(const char *text, int width, float maximum)
{
	int natural = GetHintSizeInPixels(text);

	return natural > 0 && (float)natural * maximum > (float)width ?
		(float)width / (float)natural : maximum;
}

/* One hint line: button names become icons, the rest stays text. x is the
 * line's left edge, centre or right edge as align says; y is its middle. */
static void _DrawHintText(int x, int y, const char *text, float scale, int align,
	GXColor color)
{
	uiHintItem_t items[UI_HINT_MAX_ITEMS];
	char label[UI_HINT_MAX_ITEMS][UI_HINT_LABEL_CAPACITY];
	float size = UIHint_GlyphSize(GetFontHeight(1.0f), scale);
	float total = 0.0f;
	float cursor;
	int count = UIHint_Parse(text, items, UI_HINT_MAX_ITEMS);
	int i, g;

	for(i = 0; i < count; i++) {
		total += UIHint_ItemWidth(&items[i], size, scale, GetTextSizeInPixels,
			label[i], sizeof(label[i])) + (i > 0 ? size * UI_HINT_ITEM_GAP : 0.0f);
	}
	cursor = (float)x - (align == ALIGN_CENTER ? total / 2.0f :
		(align == ALIGN_RIGHT ? total : 0.0f));
	for(i = 0; i < count; i++) {
		for(g = 0; g < items[i].glyphCount; g++) {
			float width = UIHint_GlyphWidth(items[i].glyph[g], size, scale,
				GetTextSizeInPixels);

			if(g > 0 && items[i].chord) {
				/* A chord: the two buttons are held together. */
				drawStringMedium((int)(cursor + size * UI_HINT_ALTERNATIVE_GAP + 0.5f), y,
					"+", scale, ALIGN_LEFT, color);
			}
			if(g > 0) {
				cursor += UIHint_JoinWidth(&items[i], size, scale, GetTextSizeInPixels);
			}
			_DrawHintGlyph(items[i].glyph[g], cursor + width / 2.0f, (float)y, size,
				width, scale, color.a);
			cursor += width;
		}
		if(label[i][0] != '\0') {
			if(items[i].glyphCount > 0) {
				cursor += size * UI_HINT_LABEL_GAP;
			}
			drawStringMedium((int)(cursor + 0.5f), y, label[i], scale, ALIGN_LEFT, color);
			cursor += (float)GetTextSizeInPixels(label[i]) * scale;
		}
		cursor += size * UI_HINT_ITEM_GAP;
	}
}

static void _PutSystemDialVertex(float centerX, float centerY, float radius,
		const systemDialPoint_t *point, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Position3f32(centerX + (point->x * radius), centerY + (point->y * radius), 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}

static void _DrawSystemRing(float centerX, float centerY, float radius,
		float thickness, int start, int segments, GXColor color)
{
	/* Three joined bands provide one native pixel of coverage at both edges;
	 * preserve the original stroke's integrated width without MSAA. */
	float core = fmaxf(0.0f, thickness - 0.5f);
	float offsets[4] = {-core - 1.0f, -core, core, core + 1.0f};
	for(int band = 0; band < 3; band++) {
		GXColor a = color, b = color;
		if(band == 0) a.a = 0;
		if(band == 2) b.a = 0;
		GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (segments + 1) * 2);
		for(int i = 0; i <= segments; i++) {
			const systemDialPoint_t *point = &systemDialCircle[(start + i) % 24];
			_PutSystemDialVertex(centerX, centerY, radius + offsets[band], point, a);
			_PutSystemDialVertex(centerX, centerY, radius + offsets[band + 1], point, b);
		}
		GX_End();
	}
}

static void _DrawSystemDial(float centerX, float centerY, s8 coreTemperature,
		u8 opacity)
{
	int temperatureSegments = coreTemperature < 20 ? 0 :
		(coreTemperature > 80 ? 24 : (coreTemperature - 20) * 24 / 60);

	drawInit();
	_SetupRasterColor();
	_DrawSystemRing(centerX, centerY, 19.0f, 0.8f, 0, 24,
		(GXColor) {122, 112, 201, (u8)((62 * opacity) / 255)});
	if(temperatureSegments > 0) {
		_DrawSystemRing(centerX, centerY, 16.0f, 1.45f, 18, temperatureSegments,
			(GXColor) {183, 171, 238, (u8)((196 * opacity) / 255)});
	}
	drawInit();
}

/* A sensor reading into the dial, smoothed: the sensor answers in 4 degree
 * steps (UISystem_SmoothTemperature). */
static void _SetCoreTemperature(int reading)
{
	systemInstrument.coreTemperature = (s8)UISystem_SmoothTemperature(
		&systemInstrument.temperatureFilter, reading);
	if(systemInstrument.coreTemperature >= 0) {
		(void)snprintf(systemInstrument.temperatureText,
			sizeof(systemInstrument.temperatureText), "%i\260C",
			systemInstrument.coreTemperature);
	}
	else {
		systemInstrument.temperatureText[0] = '\0';
	}
	systemInstrument.temperatureSampled = true;
}

int CoreTemperature(void)
{
	return systemInstrument.coreTemperature;
}

static void _UpdateSystemInstrument(void)
{
	struct timeval now;

	if(gettimeofday(&now, NULL) != 0) {
		/* Civil time and thermal telemetry are independent instruments. */
		if(!systemInstrument.temperatureSampled) {
			_SetCoreTemperature(SYS_GetCoreTemperature());
		}
		(void)UIClock_Compose(&systemInstrument.clock, -1, -1, -1.0f);
		systemInstrument.civilSecondSampled = false;
		systemInstrument.civilTimeAvailable = false;
		memcpy(systemInstrument.timeText, "--:--:--", 9u);
		return;
	}
	if(!systemInstrument.civilSecondSampled ||
		now.tv_sec != systemInstrument.sampledSecond) {
		struct tm localTime;
		if(localtime_r(&now.tv_sec, &localTime) != NULL) {
			systemInstrument.hour = localTime.tm_hour;
				systemInstrument.minute = localTime.tm_min;
				systemInstrument.second = localTime.tm_sec;
				systemInstrument.civilTimeAvailable = true;
				/* Refresh on every sampled second so timezone/RTC corrections in
				 * the same minute cannot leave stale text. */
				(void)strftime(systemInstrument.timeText,
					sizeof(systemInstrument.timeText), "%H:%M:%S", &localTime);
		}
		else {
			systemInstrument.civilTimeAvailable = false;
			memcpy(systemInstrument.timeText, "--:--:--", 9u);
		}
		_SetCoreTemperature(SYS_GetCoreTemperature());
		systemInstrument.sampledSecond = now.tv_sec;
		systemInstrument.civilSecondSampled = true;
	}
	if(systemInstrument.civilTimeAvailable) {
		(void)UIClock_Compose(&systemInstrument.clock,
			systemInstrument.hour, systemInstrument.minute,
			(float)systemInstrument.second +
				(float)now.tv_usec / 1000000.0f);
	}
	else {
		(void)UIClock_Compose(&systemInstrument.clock, -1, -1, -1.0f);
	}
}

// Internal
static void _DrawTitleBar(uiDrawObj_t *evt) {
	float reveal = UIScene_Frame()->chromeProgress;
	int clock = _Corner(swissSettings.clockPosition);
	int dial = _Corner(swissSettings.temperaturePosition);
	/* The dial sits 40 in from the edge the frame shows; the time on its
	 * inner side in the same corner, else 40 in itself. */
	int dialX = (int)(dial < 0 ? UIStage_Left() + 40.0f : UIStage_Right() - 40.0f);
	int timeX = clock == dial ? dialX - 32 * clock :
		(int)(clock < 0 ? UIStage_Left() + 40.0f : UIStage_Right() - 40.0f);
	int offsetY;
	GXColor textColor;

	(void)evt;
	if(reveal <= 0.0f || (clock == 0 && dial == 0)) {
		return;
	}
	if(reveal > 1.0f) {
		reveal = 1.0f;
	}
	offsetY = (int)((reveal - 1.0f) * 18.0f);
	textColor = (GXColor) {209, 201, 255, (u8)(232.0f * reveal)};

	/* A single pre-traversal instrument snapshot owns both header and cube. */
	if(dial != 0) {
		_DrawSystemDial((float)dialX, 43.0f + offsetY, systemInstrument.coreTemperature, (u8)(255.0f * reveal));
		if(systemInstrument.temperatureText[0]) {
			drawStringMedium(dialX, 43 + offsetY,
				systemInstrument.temperatureText,
				0.42f, ALIGN_CENTER, textColor);
		}
	}
	if(clock != 0 && systemInstrument.clock.available) {
		drawStringMedium(timeX, 43 + offsetY, systemInstrument.timeText,
			0.54f, clock < 0 ? ALIGN_LEFT : ALIGN_RIGHT, textColor);
	}
}

// External
uiDrawObj_t* DrawTitleBar()
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TITLEBAR;
	return event;
}

static uiMotionMode_t _CurrentMotionMode(void)
{
	/* Backdrop animation is decorative. Disabling it must not silently weaken
	 * primary navigation, focus, or scene-transition motion. */
	return UIMotion_ModeFromFlags(swissSettings.disableUIAnimations,
		swissSettings.reduceUIAnimations);
}

typedef struct gameflowPoint {
	float x;
	float y;
} gameflowPoint_t;

typedef struct gameflowQuad {
	gameflowPoint_t point[4];
} gameflowQuad_t;

/* visualSlot orders the painting (farthest first); focus lights the card's
 * border and art says whether it shows its cover, banner or emblem, laid out
 * for artSlot. On a ring focus and artSlot follow visualSlot; in the grid
 * they follow the highlight. */
typedef struct gameflowRenderCard {
	const uiGameflowCardSnapshot_t *record;
	u32 recordIndex;
	float visualSlot;
	float artSlot;
	float focus;
	float presence;
	bool art;
	/* Spotlight's row: a banner tile, framed in whole pixels rather than in
	 * proportion, since it is three times as wide as it is tall. */
	bool tile;
	gameflowQuad_t quad;
} gameflowRenderCard_t;

static const gameflowQuad_t gameflowSlotPoses[7] = {
	{{{-28.0f, 185.0f}, {10.0f, 174.0f}, {10.0f, 244.0f}, {-28.0f, 233.0f}}},
	{{{14.0f, 151.0f}, {72.0f, 143.0f}, {72.0f, 273.0f}, {14.0f, 265.0f}}},
	{{{78.0f, 132.0f}, {208.0f, 121.0f}, {208.0f, 295.0f}, {78.0f, 284.0f}}},
	{{{230.0f, 88.0f}, {410.0f, 88.0f}, {410.0f, 328.0f}, {230.0f, 328.0f}}},
	{{{432.0f, 121.0f}, {562.0f, 132.0f}, {562.0f, 284.0f}, {432.0f, 295.0f}}},
	{{{568.0f, 143.0f}, {626.0f, 151.0f}, {626.0f, 265.0f}, {568.0f, 273.0f}}},
	{{{630.0f, 174.0f}, {668.0f, 185.0f}, {668.0f, 233.0f}, {630.0f, 244.0f}}}
};

/* The Vertical layout: the same ring on end, the selected cover at the left
 * of the screen and its neighbours tipped back above and below it. */
static const gameflowQuad_t gameflowVerticalPoses[7] = {
	{{{112.0f, -36.0f}, {196.0f, -36.0f}, {202.0f, -28.0f}, {106.0f, -28.0f}}},
	{{{112.0f, 16.0f}, {196.0f, 16.0f}, {206.0f, 24.0f}, {102.0f, 24.0f}}},
	{{{98.0f, 36.0f}, {210.0f, 36.0f}, {224.0f, 98.0f}, {84.0f, 98.0f}}},
	{{{72.0f, 108.0f}, {236.0f, 108.0f}, {236.0f, 328.0f}, {72.0f, 328.0f}}},
	{{{84.0f, 338.0f}, {224.0f, 338.0f}, {210.0f, 400.0f}, {98.0f, 400.0f}}},
	{{{102.0f, 408.0f}, {206.0f, 408.0f}, {196.0f, 416.0f}, {112.0f, 416.0f}}},
	{{{106.0f, 480.0f}, {202.0f, 480.0f}, {196.0f, 488.0f}, {112.0f, 488.0f}}}
};

/* The Spotlight layout, designed after Gameplay Spotlight by mvizensk
 * (github.com/mvizensk/gameplay-spotlight), with its author's permission;
 * see NOTICE. The same ring as a row of disc banners along the
 * bottom, each 96x32 inside a 4-pixel frame, the selected one a quarter
 * larger. Above the row, the selected game's picture fills a 4:3 panel,
 * 320x240 inside its frame so a gameplay still is drawn pixel for pixel, and
 * its details stand in the column beside it. A cover leaves the panel's
 * middle for Detail. */
static const gameflowQuad_t gameflowSpotlightPoses[7] = {
	{{{-92.0f, 352.0f}, {12.0f, 352.0f}, {12.0f, 392.0f}, {-92.0f, 392.0f}}},
	{{{28.0f, 352.0f}, {132.0f, 352.0f}, {132.0f, 392.0f}, {28.0f, 392.0f}}},
	{{{148.0f, 352.0f}, {252.0f, 352.0f}, {252.0f, 392.0f}, {148.0f, 392.0f}}},
	{{{256.0f, 348.0f}, {384.0f, 348.0f}, {384.0f, 396.0f}, {256.0f, 396.0f}}},
	{{{388.0f, 352.0f}, {492.0f, 352.0f}, {492.0f, 392.0f}, {388.0f, 392.0f}}},
	{{{508.0f, 352.0f}, {612.0f, 352.0f}, {612.0f, 392.0f}, {508.0f, 392.0f}}},
	{{{628.0f, 352.0f}, {732.0f, 352.0f}, {732.0f, 392.0f}, {628.0f, 392.0f}}}
};
static const gameflowQuad_t gameflowSpotlightPanel =
	{{{32.0f, 74.0f}, {360.0f, 74.0f}, {360.0f, 322.0f}, {32.0f, 322.0f}}};
static const gameflowQuad_t gameflowSpotlightCover =
	{{{106.0f, 78.0f}, {286.0f, 78.0f}, {286.0f, 318.0f}, {106.0f, 318.0f}}};
#define GAMEFLOW_SPOTLIGHT_COLUMN_X 380
#define GAMEFLOW_SPOTLIGHT_COLUMN_W 224
#define GAMEFLOW_SPOTLIGHT_TEXT_SCALE 0.46f
#define GAMEFLOW_SPOTLIGHT_NO_DESCRIPTION "No description for this game."

/* The Jewel Case layout (ui_jewel_case.c), after portlandhodl's jewel case
 * games view for Swiss; see NOTICE. No cards: one clear case at a time, its
 * disc printed with the middle of the game's cover, as wide as the cover's
 * 192x256 (s0, t0, s1, t1 of the pack's canvas), or its own disc art from
 * discs.pak, which comes first when the pack has it. A cover leaves the case's
 * middle for Detail. The carousel swaps cases a little slower than it turns
 * the other rings, so a case can be seen falling back and the next one
 * swinging in. */
static const float gameflowJewelCasePrint[4] = {0.0f, 0.125f, 0.75f, 0.875f};
/* A disc from discs.pak fills its 160x160 square. */
static const float gameflowJewelCaseDisc[4] = {0.0f, 0.0f, 1.0f, 1.0f};
static const gameflowQuad_t gameflowJewelCaseCover =
	{{{245.0f, 106.0f}, {395.0f, 106.0f}, {395.0f, 306.0f}, {245.0f, 306.0f}}};
#define GAMEFLOW_JEWEL_CASE_RESPONSE 8.0f

/* The Grid layout: five columns, three rows on screen, the focused row in the
 * middle. The highlighted card grows by a tenth. */
#define GAMEFLOW_GRID_CENTER_X 320.0f
#define GAMEFLOW_GRID_CENTER_Y 216.0f
#define GAMEFLOW_GRID_PITCH_X 100.0f
#define GAMEFLOW_GRID_PITCH_Y 108.0f
#define GAMEFLOW_GRID_CARD_W 72.0f
#define GAMEFLOW_GRID_CARD_H 96.0f
#define GAMEFLOW_GRID_FOCUS_GROWTH 0.10f

_Static_assert(UI_GAMEFLOW_RENDER_SLOTS >= UI_GAMEFLOW_LIBRARY_GRID_WINDOW &&
	UI_GAMEFLOW_RENDER_SLOTS >= UI_GAMEFLOW_LIBRARY_WINDOW,
	"Gameflow records must hold every layout's window");

static float _GameflowClamp(float value, float minimum, float maximum)
{
	if(value < minimum) {
		return minimum;
	}
	if(value > maximum) {
		return maximum;
	}
	return value;
}

static float _GameflowRound(float value)
{
	return floorf(value + 0.5f);
}

static gameflowPoint_t _GameflowLerpPoint(gameflowPoint_t from,
	gameflowPoint_t to, float progress)
{
	gameflowPoint_t point = {
		from.x + (to.x - from.x) * progress,
		from.y + (to.y - from.y) * progress
	};
	return point;
}

/* A card between two poses, in whole pixels. It goes from one pose to the
 * next a whole pixel of its farthest-moving corner at a time, and each corner
 * rounds on its own there: every edge moves one way through a move, never a
 * pixel back as the spring's tail crosses half pixels, and the card's size
 * from that corner changes one way too. */
static gameflowQuad_t _GameflowSamplePoseIn(const gameflowQuad_t poses[7],
	float slot)
{
	gameflowQuad_t result;
	float clamped = _GameflowClamp(slot, -3.0f, 3.0f);
	int lower = (int)floorf(clamped);
	int upper = lower < 3 ? lower + 1 : lower;
	const gameflowQuad_t *from = &poses[lower + 3];
	const gameflowQuad_t *to = &poses[upper + 3];
	float progress = clamped - (float)lower;
	float travel = 0.0f;
	int i;

	for(i = 0; i < 4; ++i) {
		travel = fmaxf(travel, fabsf(to->point[i].x - from->point[i].x));
		travel = fmaxf(travel, fabsf(to->point[i].y - from->point[i].y));
	}
	if(travel > 0.0f) {
		progress = _GameflowRound(progress * travel) / travel;
	}
	for(i = 0; i < 4; ++i) {
		result.point[i] = _GameflowLerpPoint(from->point[i], to->point[i],
			progress);
		result.point[i].x = _GameflowRound(result.point[i].x);
		result.point[i].y = _GameflowRound(result.point[i].y);
	}
	return result;
}

static gameflowQuad_t _GameflowSamplePose(float slot)
{
	return _GameflowSamplePoseIn(gameflowSlotPoses, slot);
}

/* A grid card at a column and a row (0 is the focused row), grown by
 * focus. Whole pixels, like the carousel's poses: its centre rounded and
 * its rounded half size either side, so it grows and shrinks one way. */
static gameflowQuad_t _GameflowGridQuad(float column, float row, float focus)
{
	float scale = 1.0f + GAMEFLOW_GRID_FOCUS_GROWTH * focus;
	float halfWidth = _GameflowRound(GAMEFLOW_GRID_CARD_W * 0.5f * scale);
	float halfHeight = _GameflowRound(GAMEFLOW_GRID_CARD_H * 0.5f * scale);
	float x = _GameflowRound(GAMEFLOW_GRID_CENTER_X +
		(column - (float)(UI_GAMEFLOW_LIBRARY_GRID_COLUMNS - 1u) * 0.5f) *
		GAMEFLOW_GRID_PITCH_X);
	float y = _GameflowRound(GAMEFLOW_GRID_CENTER_Y +
		row * GAMEFLOW_GRID_PITCH_Y);
	float left = x - halfWidth;
	float right = x + halfWidth;
	float top = y - halfHeight;
	float bottom = y + halfHeight;
	gameflowQuad_t quad = {{{left, top}, {right, top}, {right, bottom},
		{left, bottom}}};
	return quad;
}

/* The focused row is lit and its neighbours dimmed; two rows out a row is
 * gone, which is where rows scroll in and out. */
static float _GameflowGridPresence(float row)
{
	float distance = fabsf(row);

	if(distance >= 2.0f) {
		return 0.0f;
	}
	return distance <= 1.0f ? 1.0f - 0.34f * distance :
		0.66f * (2.0f - distance);
}

static gameflowPoint_t _GameflowQuadPoint(const gameflowQuad_t *quad,
	float u, float v)
{
	gameflowPoint_t top = _GameflowLerpPoint(quad->point[0], quad->point[1], u);
	gameflowPoint_t bottom = _GameflowLerpPoint(quad->point[3], quad->point[2], u);
	return _GameflowLerpPoint(top, bottom, v);
}

static gameflowQuad_t _GameflowInsetQuad(const gameflowQuad_t *quad,
	float horizontal, float vertical)
{
	gameflowQuad_t result = {{
		_GameflowQuadPoint(quad, horizontal, vertical),
		_GameflowQuadPoint(quad, 1.0f - horizontal, vertical),
		_GameflowQuadPoint(quad, 1.0f - horizontal, 1.0f - vertical),
		_GameflowQuadPoint(quad, horizontal, 1.0f - vertical)
	}};
	return result;
}

/* Inset by whole pixels rather than a share of the quad: a Spotlight tile
 * is three times as wide as it is tall and changes size as it slides. */
static gameflowQuad_t _GameflowInsetPixels(const gameflowQuad_t *quad,
	float pixels)
{
	float width = quad->point[1].x - quad->point[0].x;
	float height = quad->point[3].y - quad->point[0].y;

	return _GameflowInsetQuad(quad, width > 0.0f ? pixels / width : 0.0f,
		height > 0.0f ? pixels / height : 0.0f);
}

static gameflowQuad_t _GameflowRectQuad(const gameflowQuad_t *quad,
	float left, float top, float right, float bottom)
{
	gameflowQuad_t result = {{
		_GameflowQuadPoint(quad, left, top),
		_GameflowQuadPoint(quad, right, top),
		_GameflowQuadPoint(quad, right, bottom),
		_GameflowQuadPoint(quad, left, bottom)
	}};
	return result;
}

static float _GameflowPresence(float visualSlot)
{
	static const float presence[4] = {1.0f, 0.72f, 0.52f, 0.0f};
	float distance = fabsf(visualSlot);
	int lower;
	int upper;

	if(distance >= 3.0f) {
		return 0.0f;
	}
	lower = (int)floorf(distance);
	upper = lower + 1;
	return presence[lower] + (presence[upper] - presence[lower]) *
		(distance - (float)lower);
}

static u8 _GameflowAlpha(float value)
{
	return (u8)(_GameflowClamp(value, 0.0f, 255.0f) + 0.5f);
}

static void _GameflowPutVertex(gameflowPoint_t point, GXColor color)
{
	UIColor_Apply(&color.r, &color.g, &color.b);
	GX_Position3f32(point.x, point.y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}

static void _GameflowPutQuad(const gameflowQuad_t *quad, GXColor top,
	GXColor bottom)
{
	_GameflowPutVertex(quad->point[0], top);
	_GameflowPutVertex(quad->point[1], top);
	_GameflowPutVertex(quad->point[2], bottom);
	_GameflowPutVertex(quad->point[3], bottom);
}

static void _GameflowPutBorder(const gameflowQuad_t *outer,
	const gameflowQuad_t *inner, GXColor color)
{
	int i;
	for(i = 0; i < 4; ++i) {
		int next = (i + 1) % 4;
		_GameflowPutVertex(outer->point[i], color);
		_GameflowPutVertex(outer->point[next], color);
		_GameflowPutVertex(inner->point[next], color);
		_GameflowPutVertex(inner->point[i], color);
	}
}

/* Folder colors are identities, independent of Menu Color. Bypass the
 * menu hue transform only for these explicit borders and their swatch. */
static GXColor _GameflowAccent(const uiGameflowCardSnapshot_t *record,
	u8 alpha)
{
	static const GXColor palette[4] = {
		{114, 101, 186, 255}, {82, 112, 171, 255},
		{129, 93, 164, 255}, {83, 127, 150, 255}
	};
	u32 hash = 2166136261u;
	const unsigned char *text = (const unsigned char *)(record->gameId[0] ?
		record->gameId : record->title);
	GXColor color;

	while(*text) {
		hash = (hash ^ *text++) * 16777619u;
	}
	color = palette[hash & 3u];
	if(record->flags & UI_GAMEFLOW_CARD_PARENT) {
		color = (GXColor) {102, 103, 130, 255};
	}
	color.a = alpha;
	return color;
}

/* A name where a game's ID goes: its first eight characters in capitals,
 * with an ellipsis when it is longer, so a card or a tile holds it. */
#define GAMEFLOW_SHORT_NAME_SIZE 12
static void _GameflowShortName(const char *title,
	char name[GAMEFLOW_SHORT_NAME_SIZE])
{
	size_t k;

	for(k = 0; k < 8u && title[k] != '\0'; ++k) {
		char c = title[k];
		name[k] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
	}
	if(title[k] != '\0') {
		name[k++] = '\205';
	}
	name[k] = '\0';
}

static const uiGameflowCardSnapshot_t *_GameflowFindRecord(
	const uiGameflowRenderSnapshot_t *snapshot, u32 libraryIndex,
	u32 *recordIndex)
{
	u32 i;
	for(i = 0u; i < snapshot->recordCount; ++i) {
		if((snapshot->records[i].flags & UI_GAMEFLOW_CARD_VALID) &&
			snapshot->records[i].libraryIndex == libraryIndex) {
			if(recordIndex != NULL) {
				*recordIndex = i;
			}
			return &snapshot->records[i];
		}
	}
	return NULL;
}

static void _GameflowDrawBanner(const gameflowQuad_t *quad,
	GXTexObj *texture, u8 alpha)
{
	drawInit();
	GX_SetNumTevStages(1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_InvalidateTexAll();
	GX_LoadTexObj(texture, GX_TEXMAP0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32(quad->point[0].x, quad->point[0].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(0.0f, 0.0f);
		GX_Position3f32(quad->point[1].x, quad->point[1].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(1.0f, 0.0f);
		GX_Position3f32(quad->point[2].x, quad->point[2].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(1.0f, 1.0f);
		GX_Position3f32(quad->point[3].x, quad->point[3].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(0.0f, 1.0f);
	GX_End();
}

static void _GameflowDrawPoster(const gameflowRenderCard_t *card,
	GXTexObj *texture, float reveal)
{
	gameflowQuad_t content = _GameflowInsetQuad(&card->quad,
		0.033333f, 0.033333f);
	u8 alpha = _GameflowAlpha(255.0f * card->presence * reveal);

	/* The pack stores 192x256 retail-cover content on a 256x256 GX canvas.
	 * Stop at s=0.75 so the edge-extended right padding is never shown. */
	drawInit();
	GX_SetNumTevStages(1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
		GX_LO_CLEAR);
	GX_InvalidateTexAll();
	GX_LoadTexObj(texture, GX_TEXMAP0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32(content.point[0].x, content.point[0].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(0.0f, 0.0f);
		GX_Position3f32(content.point[1].x, content.point[1].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(0.75f, 0.0f);
		GX_Position3f32(content.point[2].x, content.point[2].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(0.75f, 1.0f);
		GX_Position3f32(content.point[3].x, content.point[3].y, 0.0f);
		GX_Color4u8(255, 255, 255, alpha);
		GX_TexCoord2f32(0.0f, 1.0f);
	GX_End();

	/* A following card may still be on its BNR/procedural fallback while
	 * this poster finishes loading. Do not leak the bound cover texture or
	 * textured TEV state into that code-native geometry. */
	drawInit();
	_SetupRasterColor();
}

/* A game with settings of its own: a small sliders mark, the Settings
 * face's emblem, in the top-right corner of its cover. */
static void _GameflowDrawCustomMark(const gameflowRenderCard_t *card, float reveal)
{
	static const float knob[3] = {0.66f, 0.28f, 0.50f};
	gameflowPoint_t top = _GameflowQuadPoint(&card->quad, 1.0f, 0.0f);
	gameflowPoint_t bottom = _GameflowQuadPoint(&card->quad, 1.0f, 1.0f);
	gameflowPoint_t center = _GameflowQuadPoint(&card->quad, 0.85f, 0.085f);
	float size = fabsf(bottom.y - top.y) * 0.13f;
	float bar = fmaxf(1.4f, size * 0.08f);
	u8 alpha = _GameflowAlpha(255.0f * card->presence * reveal);
	GXColor plate = {22, 17, 46, (u8)((alpha * 220u) / 255u)};
	GXColor line = {206, 196, 255, alpha};
	int k;

	UIColor_Apply(&plate.r, &plate.g, &plate.b);
	UIColor_Apply(&line.r, &line.g, &line.b);
	drawInit();
	_SetupRasterColor();
	_HintRoundRect(center.x, center.y, size, size, size * 0.24f, plate);
	for(k = 0; k < 3; k++) {
		float y = center.y + (float)(k - 1) * size * 0.25f;

		_HintRoundRect(center.x, y, size * 0.64f, bar, bar / 2.0f, line);
		_HintRoundRect(center.x - size * 0.32f + knob[k] * size * 0.64f, y,
			size * 0.17f, size * 0.17f, size * 0.05f, line);
	}
}

static void _GameflowDrawFallback(const gameflowRenderCard_t *card,
	uiGameflowLibraryArtwork_t artwork, GXTexObj *bannerTexture,
	float reveal)
{
	uiGameflowLibraryFallbackLayout_t layout;
	gameflowQuad_t surface;
	gameflowQuad_t upperFacet;
	gameflowQuad_t lowerFacet;
	gameflowQuad_t spine;
	gameflowQuad_t identity;
	gameflowQuad_t identityRule;
	GXColor accent;
	GXColor surfaceTop;
	GXColor surfaceBottom;
	GXColor facetUpper;
	GXColor facetLower;
	GXColor dark;
	u8 alpha;

	if(!UIGameflowLibrary_BuildFallbackLayout(card->artSlot, &layout)) {
		return;
	}
	alpha = _GameflowAlpha(255.0f * card->presence * reveal);
	accent = _GameflowAccent(card->record,
		_GameflowAlpha(220.0f * card->presence * reveal));
	surfaceTop = accent;
	surfaceTop.r = (u8)((surfaceTop.r + 24u) / 2u);
	surfaceTop.g = (u8)((surfaceTop.g + 20u) / 2u);
	surfaceTop.b = (u8)((surfaceTop.b + 54u) / 2u);
	surfaceTop.a = _GameflowAlpha(178.0f * card->presence * reveal);
	surfaceBottom = (GXColor) {7, 6, 23,
		_GameflowAlpha(238.0f * card->presence * reveal)};
	facetUpper = accent;
	facetUpper.a = _GameflowAlpha(68.0f * card->presence * reveal);
	facetLower = (GXColor) {92, 76, 151,
		_GameflowAlpha(76.0f * card->presence * reveal)};
	dark = (GXColor) {6, 5, 19,
		_GameflowAlpha(218.0f * card->presence * reveal)};

	surface = _GameflowRectQuad(&card->quad, layout.contentInset,
		layout.contentInset, 1.0f - layout.contentInset,
		1.0f - layout.contentInset);
	upperFacet = (gameflowQuad_t) {{
		_GameflowQuadPoint(&card->quad, 0.055f, 0.10f),
		_GameflowQuadPoint(&card->quad, 0.945f, 0.37f),
		_GameflowQuadPoint(&card->quad, 0.945f, 0.51f),
		_GameflowQuadPoint(&card->quad, 0.055f, 0.24f)
	}};
	lowerFacet = (gameflowQuad_t) {{
		_GameflowQuadPoint(&card->quad, 0.055f, 0.52f),
		_GameflowQuadPoint(&card->quad, 0.945f, 0.34f),
		_GameflowQuadPoint(&card->quad, 0.945f, 0.55f),
		_GameflowQuadPoint(&card->quad, 0.055f, 0.73f)
	}};
	spine = _GameflowRectQuad(&card->quad, 0.075f, 0.08f, 0.105f,
		0.92f);
	identity = _GameflowRectQuad(&card->quad, 0.12f,
		layout.identityTop, 0.88f, 0.91f);
	identityRule = _GameflowRectQuad(&card->quad, 0.12f,
		layout.identityTop, 0.88f, layout.identityTop + 0.018f);

	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 24);
		_GameflowPutQuad(&surface, surfaceTop, surfaceBottom);
		_GameflowPutQuad(&upperFacet, facetUpper, facetUpper);
		_GameflowPutQuad(&lowerFacet, facetLower, facetLower);
		_GameflowPutQuad(&spine, accent, accent);
		_GameflowPutQuad(&identity, dark, dark);
		_GameflowPutQuad(&identityRule, accent, accent);
	GX_End();

	if(artwork == UI_GAMEFLOW_LIBRARY_ART_BANNER && bannerTexture != NULL) {
		gameflowQuad_t bannerFrame = _GameflowRectQuad(&card->quad,
			layout.bannerLeft - 0.025f, layout.bannerTop - 0.02f,
			layout.bannerRight + 0.025f, layout.bannerBottom + 0.02f);
		gameflowQuad_t banner = _GameflowRectQuad(&card->quad,
			layout.bannerLeft, layout.bannerTop, layout.bannerRight,
			layout.bannerBottom);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			_GameflowPutQuad(&bannerFrame, dark, dark);
		GX_End();
		_GameflowDrawBanner(&banner, bannerTexture, alpha);
		drawInit();
		_SetupRasterColor();
	}
	else {
		float radius = layout.motifRadius;
		float verticalRadius = radius * 0.72f;
		gameflowQuad_t diamond = {{
			_GameflowQuadPoint(&card->quad, layout.motifCenterX,
				layout.motifCenterY - verticalRadius),
			_GameflowQuadPoint(&card->quad, layout.motifCenterX + radius,
				layout.motifCenterY),
			_GameflowQuadPoint(&card->quad, layout.motifCenterX,
				layout.motifCenterY + verticalRadius),
			_GameflowQuadPoint(&card->quad, layout.motifCenterX - radius,
				layout.motifCenterY)
		}};
		gameflowQuad_t core = {{
			_GameflowQuadPoint(&card->quad, layout.motifCenterX,
				layout.motifCenterY - verticalRadius * 0.55f),
			_GameflowQuadPoint(&card->quad,
				layout.motifCenterX + radius * 0.55f,
				layout.motifCenterY),
			_GameflowQuadPoint(&card->quad, layout.motifCenterX,
				layout.motifCenterY + verticalRadius * 0.55f),
			_GameflowQuadPoint(&card->quad,
				layout.motifCenterX - radius * 0.55f,
				layout.motifCenterY)
		}};
		GX_Begin(GX_QUADS, GX_VTXFMT0, 8);
			_GameflowPutQuad(&diamond, accent, accent);
			_GameflowPutQuad(&core, dark, dark);
		GX_End();
	}

	if(layout.identityAlpha > 0.001f) {
		const char *identityText = card->record->gameId[0] ?
			card->record->gameId :
			(card->record->flags & UI_GAMEFLOW_CARD_PARENT ?
				"RETURN" : "GAME DISC");
		const char *regionText = card->record->flags & UI_GAMEFLOW_CARD_PARENT ?
			"GAME LIBRARY" :
			UIGameflowLibrary_RegionLabel(card->record->gameId);
		/* An app whose poster isn't made yet, or a folder of games: its
		 * name where a game's ID goes, cut to what a card holds, so the
		 * cards tell apart. */
		char appName[GAMEFLOW_SHORT_NAME_SIZE];

		if(card->record->flags & UI_GAMEFLOW_CARD_APP ||
			card->record->subfolder) {
			_GameflowShortName(card->record->title, appName);
			identityText = appName;
			regionText = card->record->subfolder ? "FOLDER" : "APP";
		}
		gameflowPoint_t idPoint = _GameflowQuadPoint(&card->quad, 0.5f,
			layout.idBaseline);
		gameflowPoint_t regionPoint = _GameflowQuadPoint(&card->quad, 0.5f,
			layout.regionBaseline);
		GXColor primary = {239, 234, 255,
			_GameflowAlpha(244.0f * card->presence * reveal *
			layout.identityAlpha)};
		GXColor secondary = {177, 168, 220,
			_GameflowAlpha(218.0f * card->presence * reveal *
			layout.identityAlpha)};
		drawStringMedium((int)_GameflowRound(idPoint.x),
			(int)_GameflowRound(idPoint.y), identityText, 0.44f,
			ALIGN_CENTER, primary);
		drawStringMedium((int)_GameflowRound(regionPoint.x),
			(int)_GameflowRound(regionPoint.y), regionText, 0.31f,
			ALIGN_CENTER, secondary);
	}
	drawInit();
	_SetupRasterColor();
}

/* A picture fades in over this long from when it arrived. One read while it
 * was off screen, as the cards either side are, is older than that by the
 * time it shows, so scrolling to it shows it at once. */
#define GAMEFLOW_ART_ARRIVAL_MS 200u

/* How far a picture that arrived ageMs ago has faded in, 0 to 1. */
static float _GameflowArrival(u32 ageMs)
{
	if(ageMs >= GAMEFLOW_ART_ARRIVAL_MS ||
		_CurrentMotionMode() == UI_MOTION_OFF) {
		return 1.0f;
	}
	return UIMotion_Smoothstep((float)ageMs / (float)GAMEFLOW_ART_ARRIVAL_MS);
}

/* The share of alpha to draw what a picture fades in over, under the
 * picture at alpha * arrival: together they show as one card at alpha, and
 * nothing of it is left just as the picture is whole. */
static float _GameflowUnderneath(float alpha, float arrival)
{
	float rest = 1.0f - alpha * arrival;

	return rest > 0.0f ? (1.0f - arrival) / rest : 0.0f;
}

/* The record's poster, or NULL; *arrival (when asked for) is how far it has
 * faded in since it arrived. */
static GXTexObj *_GameflowPosterTexture(
	const uiGameflowCardSnapshot_t *record, float *arrival)
{
	uiPosterHandle_t handle;
	uiPosterResult_t result;
	GXTexObj *texture;
	u32 ageMs;

	/* An app's poster, or a folder of games', is made on the console from
	 * its own picture or its name (card_art.c). */
	if((record->flags & UI_GAMEFLOW_CARD_APP) || record->subfolder) {
		texture = CardArt_Poster((int32_t)record->libraryIndex);
		ageMs = CardArt_PosterAgeMs((int32_t)record->libraryIndex);
	}
	else {
		/* _DrawGameflow runs under _videomutex. Query and Peek deliberately
		 * do not lock and the borrowed texture is consumed before that lock
		 * drops. */
		result = UIAssets_Query(record->gameId,
			strnlen(record->gameId, sizeof(record->gameId)),
			(record->flags & UI_GAMEFLOW_CARD_HAS_BANNER) != 0u, &handle);
		if(result != UI_POSTER_EXACT && result != UI_POSTER_UNIVERSAL) {
			return NULL;
		}
		texture = UIAssets_Peek(handle);
		ageMs = UIAssets_PeekAgeMs(handle);
	}
	if(arrival != NULL) {
		*arrival = _GameflowArrival(ageMs);
	}
	return texture;
}

/* The game's gameplay still from stills.pak, or NULL; the same borrowing
 * rules as its poster. */
static GXTexObj *_GameflowStillTexture(const uiGameflowCardSnapshot_t *record)
{
	uiPosterHandle_t handle;
	uiPosterResult_t result = UIStills_Query(record->gameId,
		strnlen(record->gameId, sizeof(record->gameId)), false, &handle);

	if(result != UI_POSTER_EXACT && result != UI_POSTER_UNIVERSAL) {
		return NULL;
	}
	return UIStills_Peek(handle);
}

/* The game's disc from discs.pak: true when the pack has one for it, with
 * *texture NULL while it is still being read, and *arrival how far it has
 * faded in since it arrived. The same borrowing rules as its poster. */
static bool _GameflowDiscTexture(const uiGameflowCardSnapshot_t *record,
	GXTexObj **texture, float *arrival)
{
	uiPosterHandle_t handle;
	uiPosterResult_t result = UIDiscs_Query(record->gameId,
		strnlen(record->gameId, sizeof(record->gameId)), false, &handle);

	if(result != UI_POSTER_EXACT && result != UI_POSTER_UNIVERSAL) {
		return false;
	}
	*texture = UIDiscs_Peek(handle);
	*arrival = _GameflowArrival(UIDiscs_PeekAgeMs(handle));
	return true;
}

/* How far the game's still has faded in since it arrived. */
static float _GameflowStillArrival(const uiGameflowCardSnapshot_t *record)
{
	uiPosterHandle_t handle;
	uiPosterResult_t result = UIStills_Query(record->gameId,
		strnlen(record->gameId, sizeof(record->gameId)), false, &handle);

	if(result != UI_POSTER_EXACT && result != UI_POSTER_UNIVERSAL) {
		return 1.0f;
	}
	return _GameflowArrival(UIStills_PeekAgeMs(handle));
}

/* Spotlight's picture panel: a frame and a dark field, faded by alpha. */
static void _GameflowDrawSpotlightPanel(float alpha)
{
	gameflowQuad_t inner = _GameflowInsetPixels(&gameflowSpotlightPanel, 4.0f);
	GXColor top = {49, 43, 92, _GameflowAlpha(130.0f * alpha)};
	GXColor bottom = {12, 10, 34, _GameflowAlpha(190.0f * alpha)};
	GXColor border = {220, 214, 255, _GameflowAlpha(200.0f * alpha)};

	if(alpha <= 0.001f) {
		return;
	}
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 20);
		_GameflowPutQuad(&inner, top, bottom);
		_GameflowPutBorder(&gameflowSpotlightPanel, &inner, border);
	GX_End();
}

/* One game's cover in the middle of Spotlight's panel, faded by alpha,
 * drawn as Detail draws it, so the cover can leave from there for Detail. */
static void _GameflowDrawSpotlightCover(drawGameflowEvent_t *data,
	const uiGameflowCardSnapshot_t *record, u32 recordIndex, float alpha)
{
	gameflowRenderCard_t cover;
	GXTexObj *texture;
	GXTexObj *bannerTexture = NULL;
	uiGameflowLibraryArtwork_t artwork;
	float arrival;

	memset(&cover, 0, sizeof(cover));
	cover.record = record;
	cover.recordIndex = recordIndex;
	cover.focus = 1.0f;
	cover.presence = alpha;
	cover.art = true;
	cover.quad = gameflowSpotlightCover;
	texture = _GameflowPosterTexture(record, &arrival);
	if(record->flags & UI_GAMEFLOW_CARD_HAS_BANNER) {
		bannerTexture = &data->bannerTexObj[recordIndex];
	}
	artwork = UIGameflowLibrary_ChooseArtwork(texture != NULL,
		bannerTexture != NULL);
	if(artwork == UI_GAMEFLOW_LIBRARY_ART_POSTER) {
		if(arrival < 1.0f) {
			_GameflowDrawFallback(&cover, UIGameflowLibrary_ChooseArtwork(
				false, bannerTexture != NULL), bannerTexture,
				_GameflowUnderneath(alpha, arrival));
		}
		_GameflowDrawPoster(&cover, texture, arrival);
	}
	else {
		_GameflowDrawFallback(&cover, artwork, bannerTexture, 1.0f);
	}
	if(record->flags & UI_GAMEFLOW_CARD_CUSTOM) {
		_GameflowDrawCustomMark(&cover, 1.0f);
	}
}

/* One game's picture in Spotlight's panel, faded by alpha: its gameplay
 * still filling the panel, else its cover in the middle. A still that has
 * just arrived fades in over the cover. A game with settings of its own
 * carries the mark a cover does, on either. */
static void _GameflowDrawSpotlightArt(drawGameflowEvent_t *data,
	const uiGameflowCardSnapshot_t *record, u32 recordIndex, float alpha)
{
	gameflowRenderCard_t mark;
	GXTexObj *texture;

	if(record == NULL || alpha <= 0.001f) {
		return;
	}
	texture = _GameflowStillTexture(record);
	if(texture != NULL) {
		gameflowQuad_t inner = _GameflowInsetPixels(&gameflowSpotlightPanel,
			4.0f);
		float arrival = _GameflowStillArrival(record);
		if(arrival < 1.0f) {
			_GameflowDrawSpotlightCover(data, record, recordIndex,
				alpha * _GameflowUnderneath(alpha, arrival));
		}
		_GameflowDrawBanner(&inner, texture,
			_GameflowAlpha(255.0f * alpha * arrival));
		drawInit();
		_SetupRasterColor();
		if(record->flags & UI_GAMEFLOW_CARD_CUSTOM) {
			memset(&mark, 0, sizeof(mark));
			mark.quad = inner;
			mark.presence = alpha * arrival;
			_GameflowDrawCustomMark(&mark, 1.0f);
		}
		return;
	}
	_GameflowDrawSpotlightCover(data, record, recordIndex, alpha);
}

/* A game on Spotlight's row: its disc banner inside the tile's frame, or its
 * game ID when it has no banner; a folder of games, its short name. */
static void _GameflowDrawSpotlightTile(const gameflowRenderCard_t *card,
	GXTexObj *banner, float reveal)
{
	gameflowQuad_t inner = _GameflowInsetPixels(&card->quad, 4.0f);
	float alpha = card->presence * reveal;

	if(banner != NULL) {
		_GameflowDrawBanner(&inner, banner, _GameflowAlpha(255.0f * alpha));
		drawInit();
		_SetupRasterColor();
		return;
	}
	{
		GXColor text = {190, 181, 231, _GameflowAlpha(220.0f * alpha)};
		gameflowPoint_t middle = _GameflowQuadPoint(&card->quad, 0.5f, 0.5f);
		char shortName[GAMEFLOW_SHORT_NAME_SIZE];
		const char *label = card->record->gameId[0] ?
			card->record->gameId : card->record->title;

		if(card->record->subfolder) {
			_GameflowShortName(card->record->title, shortName);
			label = shortName;
		}
		drawStringMedium((int)_GameflowRound(middle.x),
			(int)_GameflowRound(middle.y) - 6, label, 0.38f,
			ALIGN_CENTER, text);
	}
}

/* Spotlight's column: the selected game's description, under its title. */
static void _GameflowDrawSpotlightDescription(const drawGameflowEvent_t *data,
	float alpha, float reveal)
{
	GXColor text = {190, 181, 231, _GameflowAlpha(230.0f * alpha * reveal)};
	unsigned line;

	if(alpha <= 0.001f) {
		return;
	}
	for(line = 0u; line < UI_GAMEFLOW_DESCRIPTION_LINES &&
		data->spotlightLines[line][0]; ++line) {
		drawStringMedium(GAMEFLOW_SPOTLIGHT_COLUMN_X, 154 + (int)line * 22,
			data->spotlightLines[line], GAMEFLOW_SPOTLIGHT_TEXT_SCALE,
			ALIGN_LEFT, text);
	}
}

/* The selected game's title and publisher: under the carousel's cover,
 * beside the column's or Spotlight's picture with its facts, or in the
 * grid's strip above the command line. */
static void _GameflowDrawMetadata(const uiGameflowCardSnapshot_t *record,
	const drawGameflowCardPresentation_t *presentation, float alpha,
	float reveal, uiGameflowLayout_t layout)
{
	GXColor primary = {246, 243, 255, _GameflowAlpha(255.0f * alpha * reveal)};
	GXColor secondary = {190, 181, 231, _GameflowAlpha(220.0f * alpha * reveal)};
	GXColor muted = {165, 158, 201, _GameflowAlpha(218.0f * alpha * reveal)};
	bool company = record != NULL && record->company[0] && !(record->flags &
		(UI_GAMEFLOW_CARD_PARENT | UI_GAMEFLOW_CARD_FOLDER));

	if(record == NULL || presentation == NULL || alpha <= 0.001f) {
		return;
	}
	if(layout == UI_GAMEFLOW_LAYOUT_VERTICAL) {
		drawStringMedium(262, 206, record->title, presentation->titleScale,
			ALIGN_LEFT, primary);
		if(company) {
			drawStringMedium(262, 235, record->company,
				presentation->companyScale, ALIGN_LEFT, secondary);
		}
		if(record->facts[0] && !(record->flags & UI_GAMEFLOW_CARD_PARENT)) {
			drawStringMedium(262, company ? 260 : 235, record->facts,
				presentation->factsScale, ALIGN_LEFT, muted);
		}
		return;
	}
	if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT) {
		drawStringMedium(GAMEFLOW_SPOTLIGHT_COLUMN_X, 92, record->title,
			presentation->titleScale, ALIGN_LEFT, primary);
		if(company) {
			drawStringMedium(GAMEFLOW_SPOTLIGHT_COLUMN_X, 118, record->company,
				presentation->companyScale, ALIGN_LEFT, secondary);
		}
		if(record->facts[0] && !(record->flags & UI_GAMEFLOW_CARD_PARENT)) {
			drawStringMedium(GAMEFLOW_SPOTLIGHT_COLUMN_X, 306, record->facts,
				presentation->factsScale, ALIGN_LEFT, muted);
		}
		return;
	}
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		drawStringMedium(320, 388, record->title, presentation->titleScale,
			ALIGN_CENTER, primary);
		if(company) {
			drawStringMedium(320, 407, record->company,
				presentation->companyScale, ALIGN_CENTER, secondary);
		}
		return;
	}
	drawStringMedium(320, 350, record->title, presentation->titleScale,
		ALIGN_CENTER, primary);
	if(company) {
		drawStringMedium(320, 376, record->company,
			presentation->companyScale, ALIGN_CENTER, secondary);
	}
}

static float _GameflowPrepareDetailText(char *text, size_t capacity,
	int width, float maximum, float floor)
{
	char original[UI_GAMEFLOW_DETAIL_PRESENTATION_CAPACITY];
	size_t length;
	size_t keep;
	float scale;

	if(text == NULL || capacity == 0u || text[0] == '\0') {
		return 0.0f;
	}
	if(capacity > sizeof(original)) {
		capacity = sizeof(original);
	}
	text[capacity - 1u] = '\0';
	length = strnlen(text, capacity);
	scale = GetTextScaleToFitInWidthWithMax(text, width, maximum);
	if(scale >= floor) {
		return scale;
	}
	memcpy(original, text, length + 1u);
	for(keep = length; keep > 0u; --keep) {
		size_t candidate = keep - 1u;

		if(candidate + 3u >= capacity) {
			continue;
		}
		memcpy(text, original, candidate);
		memcpy(&text[candidate], "...", 4u);
		if((float)GetTextSizeInPixels(text) * floor <= (float)width) {
			break;
		}
	}
	if(keep == 0u && capacity >= 4u) {
		memcpy(text, "...", 4u);
	}
	scale = GetTextScaleToFitInWidthWithMax(text, width, maximum);
	return scale < floor ? floor : scale;
}

/* Measure and clip a private copy. Only the visible prefix length needs
 * staging; the caller's snapshot, including text, stays immutable. */
static float _GameflowPrepareCardText(const char *source, size_t capacity,
	int width, float maximum, float floor, u16 *keep)
{
	char text[UI_GAMEFLOW_TITLE_LENGTH];
	_Static_assert(UI_GAMEFLOW_COMPANY_LENGTH <= sizeof(text) &&
		UI_GAMEFLOW_FACTS_LENGTH <= sizeof(text), "card text scratch size");

	memcpy(text, source, capacity);
	float scale = _GameflowPrepareDetailText(text, capacity, width, maximum, floor);
	size_t length = strlen(text);
	*keep = 0xffffu;
	if(length >= 3u && (length != strnlen(source, capacity - 1u) ||
		memcmp(text, source, length))) {
		*keep = (u16)(length - 3u);
	}
	return scale;
}

static void _GameflowApplyCardText(char *text, size_t capacity, u16 keep)
{
	if(text[0] == '\0') return;
	text[capacity - 1u] = '\0';
	if(keep != 0xffffu) memcpy(text + keep, "...", 4u);
}

static void _GameflowPrepareCardPresentation(const uiGameflowRenderSnapshot_t *snapshot,
	u32 recordIndex, drawGameflowPreparedCard_t *presentation)
{
	/* Room for the title, publisher and facts in each layout, in the
	 * layouts' order: the line under the carousel's cover, the column beside
	 * the vertical cover (x 262 to 606), the grid's strip above the command
	 * line, Spotlight's column beside its picture (x 380 to 604), and the
	 * carousel's line again under the jewel case. */
	static const struct {
		int width;
		float maximum;
		float floor;
	} fit[][3] = {
		{{520, 0.78f, 0.50f}, {420, 0.50f, 0.42f}, {500, 0.44f, 0.42f}},
		{{344, 0.84f, 0.54f}, {344, 0.54f, 0.44f}, {344, 0.46f, 0.42f}},
		{{560, 0.66f, 0.50f}, {480, 0.46f, 0.42f}, {500, 0.44f, 0.42f}},
		{{GAMEFLOW_SPOTLIGHT_COLUMN_W, 0.62f, 0.46f},
			{GAMEFLOW_SPOTLIGHT_COLUMN_W, 0.46f, 0.40f},
			{GAMEFLOW_SPOTLIGHT_COLUMN_W, 0.42f, 0.40f}},
		{{520, 0.78f, 0.50f}, {420, 0.50f, 0.42f}, {500, 0.44f, 0.42f}}
	};
	/* A layout without its row would draw its text at scale 0. */
	_Static_assert(sizeof(fit) / sizeof(fit[0]) == UI_GAMEFLOW_LAYOUT_COUNT,
		"every Library layout needs its text sizes");
	const uiGameflowCardSnapshot_t *record;
	u32 layout;

	if(snapshot == NULL || presentation == NULL ||
		recordIndex >= snapshot->recordCount ||
		recordIndex >= UI_GAMEFLOW_RENDER_SLOTS) {
		return;
	}
	record = &snapshot->records[recordIndex];
	layout = snapshot->layout < UI_GAMEFLOW_LAYOUT_COUNT ?
		snapshot->layout : UI_GAMEFLOW_LAYOUT_HORIZONTAL;
	presentation->sizes.titleScale = _GameflowPrepareCardText(record->title,
		sizeof(record->title), fit[layout][0].width, fit[layout][0].maximum,
		fit[layout][0].floor, &presentation->keep[0]);
	presentation->sizes.companyScale = _GameflowPrepareCardText(record->company,
		sizeof(record->company), fit[layout][1].width, fit[layout][1].maximum,
		fit[layout][1].floor, &presentation->keep[1]);
	presentation->sizes.factsScale = _GameflowPrepareCardText(record->facts,
		sizeof(record->facts), fit[layout][2].width, fit[layout][2].maximum,
		fit[layout][2].floor, &presentation->keep[2]);
}

/* Spotlight's column: the selected game's description wrapped to its
 * width, once per snapshot. A banner breaks its lines with newlines or runs
 * of spaces; the column joins them and wraps at its own width. A game with
 * no description says so rather than leaving the column blank. */
static void _GameflowPrepareSpotlight(const uiGameflowRenderSnapshot_t *snapshot,
	char lines[UI_GAMEFLOW_DESCRIPTION_LINES][UI_CHEATS_TEXT_CAPACITY])
{
	const char *description = snapshot->description;
	const uiGameflowCardSnapshot_t *selected;
	char text[sizeof(snapshot->description)];
	size_t in;
	size_t out = 0u;

	memset(lines, 0, UI_GAMEFLOW_DESCRIPTION_LINES * UI_CHEATS_TEXT_CAPACITY);
	if(snapshot->layout != UI_GAMEFLOW_LAYOUT_SPOTLIGHT) {
		return;
	}
	selected = _GameflowFindRecord(snapshot,
		snapshot->selection.selectedIndex, NULL);
	for(in = 0u; in < sizeof(text) && description[in] != '\0' &&
		out + 1u < sizeof(text); ++in) {
		char c = description[in];
		if(c == '\r' || c == '\n' || c == '\t') {
			c = ' ';
		}
		if(c == ' ' && (out == 0u || text[out - 1u] == ' ')) {
			continue;
		}
		text[out++] = c;
	}
	while(out > 0u && text[out - 1u] == ' ') {
		--out;
	}
	text[out] = '\0';
	if(out == 0u && selected != NULL &&
		!(selected->flags & UI_GAMEFLOW_CARD_PARENT) && !selected->subfolder) {
		memcpy(text, GAMEFLOW_SPOTLIGHT_NO_DESCRIPTION,
			sizeof(GAMEFLOW_SPOTLIGHT_NO_DESCRIPTION));
	}
	UICheats_WrapLines(lines, UI_GAMEFLOW_DESCRIPTION_LINES,
		text, GAMEFLOW_SPOTLIGHT_COLUMN_W, GAMEFLOW_SPOTLIGHT_TEXT_SCALE,
		GetTextSizeInPixels);
}

static void _GameflowPrepareSnapshot(const uiGameflowRenderSnapshot_t *snapshot,
	drawGameflowSnapshotPresentation_t *presentation)
{
	memset(presentation, 0, sizeof(*presentation));
	for(u32 i = 0; i < snapshot->recordCount; i++) {
		_GameflowPrepareCardPresentation(snapshot, i, &presentation->cards[i]);
	}
	_GameflowPrepareSpotlight(snapshot, presentation->spotlightLines);
}

static void _GameflowPrepareDetailPresentation(drawGameflowEvent_t *data)
{
	drawGameflowDetailPresentation_t *presentation;
	u8 coverRed;
	u8 coverGreen;
	u8 coverBlue;

	if(data == NULL) {
		return;
	}
	presentation = &data->detailPresentation;
	memset(presentation, 0, sizeof(*presentation));
	presentation->accent = (GXColor) {135, 120, 207, 255};
	/* Publication already owns _videomutex. Resolve the immutable pack accent
	 * once here instead of searching the pack index every presented frame.
	 * Only Indigo takes the cover's tint: mixed with a warm cover, the accent
	 * can leave the family Menu Color recolors and would stay indigo. */
	if(swissSettings.uiColor == UI_COLOR_INDIGO &&
		UIAssets_DominantColor(data->detail.gameId,
			UI_GAMEFLOW_DETAIL_ID_LENGTH, &coverRed, &coverGreen, &coverBlue)) {
		presentation->accent.r =
			(u8)(((u16)presentation->accent.r * 2u + coverRed) / 3u);
		presentation->accent.g =
			(u8)(((u16)presentation->accent.g * 2u + coverGreen) / 3u);
		presentation->accent.b =
			(u8)(((u16)presentation->accent.b * 2u + coverBlue) / 3u);
	}
	presentation->titleScale = _GameflowPrepareDetailText(
		data->detail.title, sizeof(data->detail.title), 310, 0.72f, 0.50f);
	presentation->companyScale = _GameflowPrepareDetailText(
		data->detail.company, sizeof(data->detail.company), 310, 0.50f, 0.46f);
	presentation->factsScale = _GameflowPrepareDetailText(
		data->detail.facts, sizeof(data->detail.facts), 310, 0.46f, 0.46f);
	presentation->statusScale = _GameflowPrepareDetailText(
		data->detail.statusText, sizeof(data->detail.statusText),
		310, 0.44f, 0.44f);
	presentation->lastPlayedScale = _GameflowPrepareDetailText(
		data->detail.lastPlayedText, sizeof(data->detail.lastPlayedText),
		310, 0.46f, 0.46f);
	/* « SAVES » takes more of the line than SAVES does. */
	presentation->savesSummaryScale = _GameflowPrepareDetailText(
		data->detail.savesSummary, sizeof(data->detail.savesSummary),
		(data->detail.flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) ? 246 : 264,
		0.54f, 0.46f);
	presentation->savesUpdatedScale = _GameflowPrepareDetailText(
		data->detail.savesUpdated, sizeof(data->detail.savesUpdated),
		294, 0.46f, 0.46f);
	presentation->cheatSummaryScale = _GameflowPrepareDetailText(
		data->detail.cheatSummary, sizeof(data->detail.cheatSummary),
		294, 0.46f, 0.46f);
	presentation->cheatPreviewScale = _GameflowPrepareDetailText(
		data->detail.cheatPreview, sizeof(data->detail.cheatPreview),
		294, 0.46f, 0.46f);
	presentation->settingsSummaryScale = _GameflowPrepareDetailText(
		data->detail.settingsSummary, sizeof(data->detail.settingsSummary),
		180, 0.42f, 0.42f);
	presentation->settingsPreviewScale = _GameflowPrepareDetailText(
		data->detail.settingsPreview, sizeof(data->detail.settingsPreview),
		294, 0.46f, 0.46f);
	presentation->manualSummaryScale = _GameflowPrepareDetailText(
		data->detail.manualSummary, sizeof(data->detail.manualSummary),
		180, 0.42f, 0.42f);
	presentation->launchScale = _GameflowPrepareDetailText(
		data->detail.launchLabel, sizeof(data->detail.launchLabel),
		278, 0.56f, 0.50f);
	presentation->primaryActionsScale = _GameflowPrepareDetailText(
		data->detail.primaryActions, sizeof(data->detail.primaryActions),
		590, 0.46f, 0.46f);
	/* The shortcut, launch and cheat hints fit by their text width alone:
	 * their strings are fixed and the icons leave room (the widest, "Z
	 * AUTOLOAD ON   R  VERIFY", draws about 115 px of 164). A longer one is
	 * measured with GetHintSizeInPixels. */
	presentation->advancedLineOneScale = _GameflowPrepareDetailText(
		data->detail.advancedLineOne, sizeof(data->detail.advancedLineOne),
		164, 0.42f, 0.42f);
	presentation->advancedLineTwoScale = _GameflowPrepareDetailText(
		data->detail.advancedLineTwo, sizeof(data->detail.advancedLineTwo),
		164, 0.42f, 0.42f);
}

static void _GameflowPutDetailPanel(int x, int y, int width, int height,
	int edgeWidth, GXColor glow, GXColor fill, GXColor edge)
{
	_GameflowPutVertex((gameflowPoint_t) {(float)x - 3.0f,
		(float)y - 3.0f}, glow);
	_GameflowPutVertex((gameflowPoint_t) {(float)x + (float)width + 3.0f,
		(float)y - 3.0f}, glow);
	_GameflowPutVertex((gameflowPoint_t) {(float)x + (float)width + 3.0f,
		(float)y + (float)height + 3.0f}, glow);
	_GameflowPutVertex((gameflowPoint_t) {(float)x - 3.0f,
		(float)y + (float)height + 3.0f}, glow);
	_GameflowPutVertex((gameflowPoint_t) {(float)x, (float)y}, fill);
	_GameflowPutVertex((gameflowPoint_t) {(float)x + (float)width,
		(float)y}, fill);
	_GameflowPutVertex((gameflowPoint_t) {(float)x + (float)width,
		(float)y + (float)height}, fill);
	_GameflowPutVertex((gameflowPoint_t) {(float)x,
		(float)y + (float)height}, fill);
	_GameflowPutVertex((gameflowPoint_t) {(float)x, (float)y}, edge);
	_GameflowPutVertex((gameflowPoint_t) {(float)x + (float)edgeWidth,
		(float)y}, edge);
	_GameflowPutVertex((gameflowPoint_t) {(float)x + (float)edgeWidth,
		(float)y + (float)height}, edge);
	_GameflowPutVertex((gameflowPoint_t) {(float)x,
		(float)y + (float)height}, edge);
}

static gameflowQuad_t _GameflowGrowQuad(const gameflowQuad_t *quad, float by);

/* Where Detail's lines and rows sit. The SAVES inset takes its room from the
 * title block above it and moves the rows down; without it, Detail keeps the
 * places it had before the inset. The Manual row takes the inset's place, or
 * with both, the title block closes up and the two slim down. */
typedef struct {
	int top, bottom;
} gameflowDetailRow_t;

typedef struct {
	int panelHeight;
	int title, company, status, lastPlayed;
	int lastPlayedValue;	/* 0: on LAST PLAYED's line, at the right */
	int saves, savesHeight, savesLine[2];
	int manual, manualHint;	/* the Manual row's lines (0: no hint line) */
	int settings, cheats, launch;
	gameflowDetailRow_t row[4];	/* the rows the focus moves between, bottom up */
} gameflowDetailLayout_t;

static const gameflowDetailLayout_t gameflowDetailLayouts[4] = {
	{328, 122, 148, 177, 202, 219, 0, 0, {0, 0}, 0, 0, 244, 293, 369,
		{{348, 391}, {281, 340}, {232, 274}, {0, 0}}},
	{344, 122, 140, 159, 176, 191, 202, 43, {214, 232}, 0, 0, 264, 313, 388,
		{{367, 410}, {301, 360}, {252, 294}, {0, 0}}},
	{344, 122, 140, 159, 176, 191, 0, 0, {0, 0}, 214, 232, 264, 313, 388,
		{{367, 410}, {301, 360}, {252, 294}, {202, 245}}},
	{344, 118, 135, 151, 165, 0, 175, 36, {186, 201}, 231, 0, 264, 313, 388,
		{{367, 410}, {301, 360}, {252, 294}, {217, 245}}},
};

static const gameflowDetailLayout_t *_GameflowDetailLayout(
	const uiGameflowDetailSnapshot_t *detail)
{
	return &gameflowDetailLayouts[
		((detail->flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) != 0u) +
		((detail->flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) != 0u) * 2];
}

static void _GameflowDrawDetailPlanes(
	const uiGameflowDetailSnapshot_t *detail,
	const drawGameflowDetailPresentation_t *presentation,
	const uiGameflowFrame_t *frame, float alpha,
	uiGameflowDetailFocus_t focusRow, uiMotionSpring_t lit[2])
{
	/* The rows the focus moves between, bottom up: Launch, Cheats, Settings,
	 * Manual. */
	const gameflowDetailLayout_t *layout = _GameflowDetailLayout(detail);
	int rowTop[4];
	int rowHeight[4];
	bool hasSaves = (detail->flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) != 0u;
	bool hasManual = (detail->flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) != 0u;
	uiMotionMode_t motion = _CurrentMotionMode();
	float litTop;
	float litBottom;
	gameflowQuad_t litRow;
	gameflowQuad_t litInner;
	gameflowQuad_t litGlow;
	gameflowQuad_t litHalo;
	int row;
	bool hasAdvanced = detail->advancedLineOne[0] != '\0' ||
		detail->advancedLineTwo[0] != '\0';
	float focus = _CurrentMotionMode() == UI_MOTION_FULL ?
		0.86f + 0.14f * sinf(UIAnim_Seconds() * 3.5f) : 1.0f;
	GXColor panelGlow = presentation->accent;
	GXColor panelFill = {8, 7, 25, _GameflowAlpha(222.0f * alpha)};
	GXColor panelEdge = presentation->accent;
	GXColor insetGlow = presentation->accent;
	GXColor insetFill = {16, 12, 46, _GameflowAlpha(226.0f * alpha)};
	GXColor insetEdge = presentation->accent;
	GXColor ctaGlow = presentation->accent;
	GXColor ctaFill = frame->launchProgress > 0.02f ?
		(GXColor) {58, 46, 126, _GameflowAlpha(244.0f * alpha)} :
		(GXColor) {43, 33, 101, _GameflowAlpha(238.0f * alpha)};
	GXColor ctaEdge = {
		(u8)(((u16)presentation->accent.r + 255u) / 2u),
		(u8)(((u16)presentation->accent.g + 255u) / 2u),
		(u8)(((u16)presentation->accent.b + 255u) / 2u),
		_GameflowAlpha(255.0f * alpha * focus)};
	/* The Library grid's highlight colors: it reads across the room. */
	GXColor litEdgeColor = {244, 239, 255, _GameflowAlpha(245.0f * alpha)};
	GXColor litGlowColor = {196, 177, 255,
		_GameflowAlpha(150.0f * alpha * focus)};
	GXColor litHaloColor = {117, 88, 244,
		_GameflowAlpha(78.0f * alpha * focus)};
	bool hasSettings = detail->settingsSummary[0] != '\0';
	u16 panelCount = (u16)(3u + (hasSaves ? 1u : 0u) +
		(hasAdvanced ? 1u : 0u) + (hasSettings ? 1u : 0u) +
		(hasManual ? 1u : 0u));

	for(row = 0; row < 4; ++row) {
		rowTop[row] = layout->row[row].top;
		rowHeight[row] = layout->row[row].bottom - layout->row[row].top;
	}
	/* A focus left on a row that has gone (the manual was removed) rests
	 * on the row below it. */
	if(focusRow == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL && !hasManual) {
		focusRow = UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS;
	}

	/* The bright frame slides from row to row, as the cheat list's focus
	 * does; Detail opening places it on its row at once. */
	if(lit[0].response <= 0.0f) {
		UIMotion_SpringInit(&lit[0], (float)rowTop[focusRow], 25.0f);
		UIMotion_SpringInit(&lit[1], (float)rowHeight[focusRow], 25.0f);
	}
	UIMotion_SpringRetarget(&lit[0], (float)rowTop[focusRow], motion);
	UIMotion_SpringRetarget(&lit[1], (float)rowHeight[focusRow], motion);
	litTop = UIMotion_SpringUpdate(&lit[0], UIAnim_Delta(), motion);
	litBottom = litTop + UIMotion_SpringUpdate(&lit[1], UIAnim_Delta(),
		motion);
	litRow = (gameflowQuad_t) {{{260.0f, litTop}, {590.0f, litTop},
		{590.0f, litBottom}, {260.0f, litBottom}}};
	litInner = _GameflowGrowQuad(&litRow, -2.0f);
	litGlow = _GameflowGrowQuad(&litRow, 2.0f);
	litHalo = _GameflowGrowQuad(&litGlow, 3.0f);

	panelGlow.a = _GameflowAlpha(34.0f * alpha);
	panelEdge.a = _GameflowAlpha(172.0f * alpha);
	insetGlow.a = _GameflowAlpha(22.0f * alpha);
	insetEdge.a = _GameflowAlpha(112.0f * alpha);
	ctaGlow.a = _GameflowAlpha(92.0f * alpha * focus);
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(panelCount * 12u + 48u));
		_GameflowPutDetailPanel(246, 76, 358, layout->panelHeight, 2,
			panelGlow, panelFill, panelEdge);
		if(hasSaves) {
			_GameflowPutDetailPanel(260, layout->saves, 330,
				layout->savesHeight, 2, insetGlow, insetFill, insetEdge);
		}
		/* The focused row takes the bright edge and the glow, and the
		 * grid's frame round it; Launch keeps its button fill. */
		for(row = UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH;
			row <= UI_GAMEFLOW_DETAIL_FOCUS_MANUAL; ++row) {
			bool lit = row == (int)focusRow;

			if((row == UI_GAMEFLOW_DETAIL_FOCUS_SETTINGS && !hasSettings) ||
				(row == UI_GAMEFLOW_DETAIL_FOCUS_MANUAL && !hasManual)) {
				continue;
			}
			_GameflowPutDetailPanel(260, rowTop[row], 330, rowHeight[row],
				lit ? 4 : 2, lit ? ctaGlow : insetGlow,
				row == UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH ? ctaFill : insetFill,
				lit ? ctaEdge : insetEdge);
		}
		_GameflowPutBorder(&litHalo, &litGlow, litHaloColor);
		_GameflowPutBorder(&litGlow, &litRow, litGlowColor);
		_GameflowPutBorder(&litRow, &litInner, litEdgeColor);
		if(hasAdvanced) {
			_GameflowPutDetailPanel(48, 350, 180, 52, 2,
				insetGlow, insetFill, insetEdge);
		}
	GX_End();
	drawInit();
}

static void _GameflowDrawDetailDashboard(
	const uiGameflowDetailSnapshot_t *detail,
	const drawGameflowDetailPresentation_t *presentation,
	const uiGameflowFrame_t *frame, float reveal,
	const uiCommandRailFrame_t *commandRail, uiGameflowDetailFocus_t focusRow,
	uiMotionSpring_t lit[2])
{
	const gameflowDetailLayout_t *layout;
	const char *launchText;
	float launchScale;
	float alpha;
	GXColor primary;
	GXColor secondary;
	GXColor muted;
	GXColor focus;

	if(detail == NULL || presentation == NULL || frame == NULL ||
		frame->detailProgress <= 0.001f) {
		lit[0].response = 0.0f;
		return;
	}
	layout = _GameflowDetailLayout(detail);
	alpha = _GameflowClamp(frame->detailProgress * reveal, 0.0f, 1.0f);
	primary = (GXColor) {246, 243, 255, _GameflowAlpha(255.0f * alpha)};
	secondary = (GXColor) {202, 192, 244, _GameflowAlpha(235.0f * alpha)};
	muted = (GXColor) {165, 158, 201, _GameflowAlpha(218.0f * alpha)};
	focus = (GXColor) {244, 239, 255, _GameflowAlpha(255.0f * alpha)};
	_GameflowDrawDetailPlanes(detail, presentation, frame, alpha, focusRow,
		lit);

	drawStringMedium(264, 95, frame->launchProgress > 0.02f ?
		"LAUNCHING" : "GAME DETAIL", 0.42f, ALIGN_LEFT, secondary);
	drawStringMedium(264, layout->title, detail->title,
		presentation->titleScale, ALIGN_LEFT, primary);
	if(detail->company[0] != '\0') {
		drawStringMedium(264, layout->company, detail->company,
			presentation->companyScale, ALIGN_LEFT, secondary);
	}
	if(detail->statusText[0] != '\0') {
		drawStringMedium(264, layout->status, detail->statusText,
			presentation->statusScale, ALIGN_LEFT, muted);
	}

	drawStringMedium(264, layout->lastPlayed, "LAST PLAYED", 0.42f,
		ALIGN_LEFT, secondary);
	if(layout->lastPlayedValue != 0) {
		drawStringMedium(264, layout->lastPlayedValue, detail->lastPlayedText,
			presentation->lastPlayedScale, ALIGN_LEFT, primary);
	}
	else {
		drawStringMedium(576, layout->lastPlayed, detail->lastPlayedText,
			presentation->lastPlayedScale, ALIGN_RIGHT, primary);
	}
	/* The count is the headline. A small trailing label leaves the two-line
	 * inset readable without competing with the actions below it. */
	if(detail->flags & UI_GAMEFLOW_DETAIL_HAS_SAVES) {
		/* With a choice, the label says Left and Right change it. */
		drawStringMedium(576, layout->savesLine[0],
			(detail->flags & UI_GAMEFLOW_DETAIL_SAVE_CHOICE) ?
			"\253 SAVES \273" : "SAVES", 0.38f, ALIGN_RIGHT, secondary);
		drawStringMedium(274, layout->savesLine[0], detail->savesSummary,
			presentation->savesSummaryScale, ALIGN_LEFT, primary);
		drawStringMedium(274, layout->savesLine[1], detail->savesUpdated,
			presentation->savesUpdatedScale, ALIGN_LEFT, secondary);
	}

	/* MANUAL, at the top of the rows: its pages, and A to read it. */
	if(detail->flags & UI_GAMEFLOW_DETAIL_HAS_MANUAL) {
		drawStringMedium(274, layout->manual, "MANUAL", 0.42f, ALIGN_LEFT,
			secondary);
		drawStringMedium(576, layout->manual, detail->manualSummary,
			presentation->manualSummaryScale, ALIGN_RIGHT, muted);
		if(layout->manualHint != 0) {
			_DrawHintText(274, layout->manualHint, "A  Read the manual",
				0.46f, ALIGN_LEFT, muted);
		}
	}

	/* SETTINGS, like CHEATS below it: this game's own rows, or how to set
	 * some (X opens them). */
	if(detail->settingsSummary[0] != '\0') {
		drawStringMedium(274, layout->settings, "SETTINGS", 0.42f,
			ALIGN_LEFT, secondary);
		drawStringMedium(576, layout->settings, detail->settingsSummary,
			presentation->settingsSummaryScale, ALIGN_RIGHT, muted);
		if(detail->customSettings != 0u) {
			drawStringMedium(274, layout->settings + 18, "\267", 0.50f,
				ALIGN_LEFT, focus);
			drawStringMedium(288, layout->settings + 18,
				detail->settingsPreview, presentation->settingsPreviewScale,
				ALIGN_LEFT, primary);
		}
		else {
			_DrawHintText(274, layout->settings + 18, detail->settingsPreview,
				presentation->settingsPreviewScale, ALIGN_LEFT, muted);
		}
	}

	drawStringMedium(274, layout->cheats, "CHEATS", 0.42f, ALIGN_LEFT,
		secondary);
	drawStringMedium(274, layout->cheats + 18, detail->cheatSummary,
		presentation->cheatSummaryScale, ALIGN_LEFT, muted);
	if(detail->cheatPreview[0] != '\0') {
		if(detail->enabledCheatCount != 0u) {
			drawStringMedium(274, layout->cheats + 36, "\267", 0.50f,
				ALIGN_LEFT, focus);
			drawStringMedium(288, layout->cheats + 36, detail->cheatPreview,
				presentation->cheatPreviewScale, ALIGN_LEFT, primary);
		}
		else {
			/* "Y  Choose cheats": a hint only while nothing is on, since
			 * cheat names are free text. */
			_DrawHintText(274, layout->cheats + 36, detail->cheatPreview,
				presentation->cheatPreviewScale, ALIGN_LEFT, muted);
		}
	}

	launchText = frame->launchProgress > 0.02f ?
		"STARTING GAME..." : detail->launchLabel;
	launchScale = frame->launchProgress > 0.02f ?
		0.56f : presentation->launchScale;
	if(focusRow == UI_GAMEFLOW_DETAIL_FOCUS_LAUNCH) {
		drawStringMedium(278, layout->launch, "\267", 0.58f, ALIGN_LEFT,
			focus);
	}
	_DrawHintText(425, layout->launch, launchText, launchScale, ALIGN_CENTER,
		focus);

	if(detail->advancedLineOne[0] != '\0' ||
		detail->advancedLineTwo[0] != '\0') {
		int firstY = detail->advancedLineTwo[0] != '\0' ? 378 : 387;

		drawStringMedium(138, 359, "SHORTCUTS", 0.36f,
			ALIGN_CENTER, secondary);
		if(detail->advancedLineOne[0] != '\0') {
			_DrawHintText(138, firstY, detail->advancedLineOne,
				presentation->advancedLineOneScale, ALIGN_CENTER, muted);
		}
		if(detail->advancedLineTwo[0] != '\0') {
			_DrawHintText(138, 396, detail->advancedLineTwo,
				presentation->advancedLineTwoScale, ALIGN_CENTER, muted);
		}
	}

	if(commandRail != NULL && commandRail->owner == UI_COMMAND_RAIL_DETAIL &&
		commandRail->alpha > 0.001f) {
		GXColor command = secondary;

		command.a = _GameflowAlpha(235.0f * reveal * commandRail->alpha);
		_DrawHintText(320, 433, detail->primaryActions,
			presentation->primaryActionsScale, ALIGN_CENTER, command);
	}
	drawInit();
}

/* An axis-aligned quad grown by pixels on every side. */
static gameflowQuad_t _GameflowGrowQuad(const gameflowQuad_t *quad, float by)
{
	gameflowQuad_t result = {{
		{quad->point[0].x - by, quad->point[0].y - by},
		{quad->point[1].x + by, quad->point[1].y - by},
		{quad->point[2].x + by, quad->point[2].y + by},
		{quad->point[3].x - by, quad->point[3].y + by}
	}};
	return result;
}

/* The grid's highlight: a bright frame and a soft glow that slide along the
 * focused row while the rows scroll under it. */
static void _GameflowDrawGridHighlight(const uiGameflowFrame_t *frame,
	float alpha)
{
	float pulse = _CurrentMotionMode() == UI_MOTION_FULL ?
		0.84f + 0.16f * sinf(UIAnim_Seconds() * 3.0f) : 1.0f;
	gameflowQuad_t card = _GameflowGridQuad(frame->columnPosition, 0.0f,
		1.0f);
	gameflowQuad_t edge = _GameflowGrowQuad(&card, 2.0f);
	gameflowQuad_t glow = _GameflowGrowQuad(&edge, 2.0f);
	gameflowQuad_t halo = _GameflowGrowQuad(&glow, 3.0f);
	GXColor edgeColor = {244, 239, 255, _GameflowAlpha(245.0f * alpha)};
	GXColor glowColor = {196, 177, 255, _GameflowAlpha(150.0f * alpha * pulse)};
	GXColor haloColor = {117, 88, 244, _GameflowAlpha(78.0f * alpha * pulse)};

	if(alpha <= 0.001f) {
		return;
	}
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 48);
		_GameflowPutBorder(&halo, &glow, haloColor);
		_GameflowPutBorder(&glow, &edge, glowColor);
		_GameflowPutBorder(&edge, &card, edgeColor);
	GX_End();
	drawInit();
}

/* The launch screen. From the A that starts a game to the hand-off, Game
 * Detail gives way to the cover alone, centred in a glass ring that fills as
 * Swiss works: its progress boxes become the ring's steps and one caption
 * (_GameflowLaunchMessage), and only its warnings and failures still show as
 * boxes. Launch mode turns it on and off; its state is written under
 * _videomutex. */
#define GAMEFLOW_LAUNCH_X 320.0f
#define GAMEFLOW_LAUNCH_Y 188.0f
#define GAMEFLOW_LAUNCH_RADIUS 136.0f
/* The whole ring's segments: under a third of a pixel from a circle. */
#define GAMEFLOW_LAUNCH_SEGMENTS 48
#define GAMEFLOW_TURN 6.2831853f

/* The cover, centred in the ring with a clear margin at its corners. */
static const gameflowQuad_t gameflowLaunchPose = {{{248.0f, 92.0f},
	{392.0f, 92.0f}, {392.0f, 284.0f}, {248.0f, 284.0f}}};
static bool launchActive;
static uiLaunch_t launchState;
static GXTexObj launchPoster;
static bool launchHasPoster;
/* The launch under way is an app's (Apps): it says app, not game. */
static bool launchIsApp;

/* During a launch the launch screen shows a looping bar's step, and during
 * an app's boot_dol's "Loading DOL" bar too. */
static bool _LaunchTakesBar(bool indeterminate)
{
	return indeterminate || launchIsApp;
}
static float launchWarningScale;

/* A launch starts from an empty ring and keeps the cover Detail shows now:
 * ui_assets keeps a poster's texels until the video thread stops, so closing
 * the pack for the hand-off can't swap it for the banner. */
static void _GameflowSetLaunch(drawGameflowEvent_t *data, bool on)
{
	if(on && !launchActive) {
		const uiGameflowCardSnapshot_t *record = _GameflowFindRecord(
			&data->snapshot, UIGameflow_Frame(&data->state)->focusIndex, NULL);
		GXTexObj *poster = record != NULL ?
			_GameflowPosterTexture(record, NULL) : NULL;

		UILaunch_Begin(&launchState);
		launchWarningScale = 0.0f;
		launchIsApp = record != NULL &&
			(record->flags & UI_GAMEFLOW_CARD_APP) != 0u;
		launchHasPoster = poster != NULL;
		if(poster != NULL) {
			launchPoster = *poster;
		}
	}
	launchActive = on;
}

static void _GameflowLaunchMessage(const char *message)
{
	if(UILaunch_Message(&launchState, message) &&
		launchState.warning[0] != '\0') {
		launchWarningScale = _GameflowPrepareDetailText(launchState.warning,
			sizeof(launchState.warning), 520, 0.50f, 0.42f);
	}
}

static GXColor _GameflowMixColor(GXColor from, GXColor to, float amount)
{
	GXColor mixed = {
		(u8)((float)from.r + ((float)to.r - (float)from.r) * amount + 0.5f),
		(u8)((float)from.g + ((float)to.g - (float)from.g) * amount + 0.5f),
		(u8)((float)from.b + ((float)to.b - (float)from.b) * amount + 0.5f),
		(u8)((float)from.a + ((float)to.a - (float)from.a) * amount + 0.5f)
	};
	return mixed;
}

/* The ring from turn `from` to `to` (0 is twelve o'clock, clockwise): a core
 * halfWidth either side of the radius and a pixel fading to clear at each
 * edge, like the title bar's dial. Its color runs from tail to head, and
 * where the glint passes it turns towards light by glint. */
static void _GameflowPutLaunchArc(float from, float to, float halfWidth,
	GXColor tail, GXColor head, GXColor light, float glintTurn, float glint)
{
	const float edge[4] = {-halfWidth - 1.0f, -halfWidth, halfWidth,
		halfWidth + 1.0f};
	gameflowPoint_t direction[GAMEFLOW_LAUNCH_SEGMENTS + 1];
	GXColor color[GAMEFLOW_LAUNCH_SEGMENTS + 1];
	int segments = (int)ceilf((to - from) * (float)GAMEFLOW_LAUNCH_SEGMENTS);
	int band;
	int i;

	segments = segments < 1 ? 1 : (segments > GAMEFLOW_LAUNCH_SEGMENTS ?
		GAMEFLOW_LAUNCH_SEGMENTS : segments);
	for(i = 0; i <= segments; ++i) {
		float along = (float)i / (float)segments;
		float turn = from + (to - from) * along;
		float angle = (turn - 0.25f) * GAMEFLOW_TURN;
		float away = fabsf(turn - glintTurn);
		float shine;

		away = away > 0.5f ? 1.0f - away : away;
		shine = away < 0.1f ? 1.0f - away / 0.1f : 0.0f;
		direction[i] = (gameflowPoint_t) {cosf(angle), sinf(angle)};
		color[i] = _GameflowMixColor(_GameflowMixColor(tail, head, along),
			light, shine * shine * glint);
	}
	for(band = 0; band < 3; ++band) {
		float inside = GAMEFLOW_LAUNCH_RADIUS + edge[band];
		float outside = GAMEFLOW_LAUNCH_RADIUS + edge[band + 1];

		GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)((segments + 1) * 2));
		for(i = 0; i <= segments; ++i) {
			GXColor inner = color[i];
			GXColor outer = color[i];

			inner.a = band == 0 ? 0 : inner.a;
			outer.a = band == 2 ? 0 : outer.a;
			_GameflowPutVertex((gameflowPoint_t) {
				GAMEFLOW_LAUNCH_X + direction[i].x * inside,
				GAMEFLOW_LAUNCH_Y + direction[i].y * inside}, inner);
			_GameflowPutVertex((gameflowPoint_t) {
				GAMEFLOW_LAUNCH_X + direction[i].x * outside,
				GAMEFLOW_LAUNCH_Y + direction[i].y * outside}, outer);
		}
		GX_End();
	}
}

/* Under the cover: the screen dimmed to its edges, the ring, and below it
 * the title, the publisher, the step and any "Do not remove" line. */
static void _GameflowDrawLaunch(const drawGameflowEvent_t *data,
	const uiGameflowDetailSnapshot_t *detail,
	const uiGameflowCardSnapshot_t *record, u32 recordIndex, float launch,
	float reveal)
{
	uiMotionMode_t motion = _CurrentMotionMode();
	float alpha = _GameflowClamp(launch * reveal, 0.0f, 1.0f);
	gameflowQuad_t screen = {{{UIStage_Left(), 0.0f},
		{UIStage_Right(), 0.0f}, {UIStage_Right(), 480.0f},
		{UIStage_Left(), 480.0f}}};
	/* The screen darkens ahead of the cover, which leaves nothing behind. */
	GXColor dim = {5, 4, 17,
		_GameflowAlpha(240.0f * _GameflowClamp(2.0f * alpha, 0.0f, 1.0f))};
	GXColor track = {122, 112, 201, _GameflowAlpha(76.0f * alpha)};
	GXColor tail = {139, 124, 226, _GameflowAlpha(214.0f * alpha)};
	GXColor head = {232, 226, 255, _GameflowAlpha(255.0f * alpha)};
	GXColor light = {250, 248, 255, _GameflowAlpha(210.0f * alpha)};
	GXColor primary = {246, 243, 255, _GameflowAlpha(255.0f * alpha)};
	GXColor secondary = {190, 181, 231, _GameflowAlpha(224.0f * alpha)};
	GXColor step = {196, 177, 255, _GameflowAlpha(240.0f * alpha)};
	GXColor warning = {255, 207, 139, _GameflowAlpha(255.0f * alpha)};
	float glint = motion == UI_MOTION_OFF ? 0.0f : 0.8f;
	float glintTurn = fmodf(UIAnim_Seconds() *
		(motion == UI_MOTION_REDUCED ? 0.12f : 0.3f), 1.0f);
	float fill;

	if(alpha <= 0.001f) {
		return;
	}
	/* The fill moves while the launch runs; a launch that failed fades out
	 * where it stopped. Animations Off: no glint, and the fill steps. */
	fill = launchActive ? UILaunch_Update(&launchState, UIAnim_Delta(),
		motion != UI_MOTION_OFF) : launchState.fill;
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		_GameflowPutQuad(&screen, dim, dim);
	GX_End();
	_GameflowPutLaunchArc(0.0f, 1.0f, 1.5f, track, track, light, glintTurn,
		glint);
	if(fill > 0.001f) {
		_GameflowPutLaunchArc(0.0f, fill, 2.0f, tail, head, light, glintTurn,
			glint);
	}
	drawInit();
	if(record != NULL) {
		const char *company = detail != NULL ? detail->company :
			record->company;

		drawStringMedium(320, 360, detail != NULL ? detail->title :
			record->title, detail != NULL ?
			data->detailPresentation.titleScale :
			data->cardPresentation[recordIndex].titleScale,
			ALIGN_CENTER, primary);
		if(company[0] != '\0') {
			drawStringMedium(320, 384, company, detail != NULL ?
				data->detailPresentation.companyScale :
				data->cardPresentation[recordIndex].companyScale,
				ALIGN_CENTER, secondary);
		}
	}
	drawStringMedium(320, 414, launchIsApp ?
		UILaunch_AppCaption(launchState.step) :
		UILaunch_Caption(launchState.step), 0.52f, ALIGN_CENTER, step);
	if(launchState.warning[0] != '\0') {
		drawStringMedium(320, 438, launchState.warning, launchWarningScale,
			ALIGN_CENTER, warning);
	}
}

/* The Jewel Case layout's cases: the selected game's at rest, and while the
 * carousel moves, the one leaving and the one arriving, each disc printed
 * with its game's cover. They give way to the cover as Detail opens, and the
 * C-stick turns them. */
static void _GameflowDrawJewelCases(const drawGameflowEvent_t *data,
	const uiGameflowFrame_t *frame, float reveal)
{
	uiJewelCaseShot_t shots[UI_GAMEFLOW_LIBRARY_WINDOW];
	float alpha = reveal * (1.0f - frame->detailProgress) *
		(1.0f - frame->launchProgress);
	int count = 0;
	u32 i;

	if(alpha <= 0.001f) {
		return;
	}
	for(i = 0u; i < data->snapshot.recordCount &&
		count < (int)(sizeof(shots) / sizeof(shots[0])); ++i) {
		const uiGameflowCardSnapshot_t *record = &data->snapshot.records[i];
		uiJewelCaseShot_t *shot = &shots[count];
		float slot = (float)record->relativeSlot + frame->carouselTravel;

		if(!(record->flags & UI_GAMEFLOW_CARD_VALID) || !(fabsf(slot) < 1.0f)) {
			continue;
		}
		shot->slot = slot;
		shot->index = record->libraryIndex;
		shot->printAlpha = 1.0f;
		/* Its own disc from discs.pak, laid over the whole disc: bare
		 * until it is read, then fading in. Without one, the middle of
		 * its cover. */
		if(_GameflowDiscTexture(record, &shot->print, &shot->printAlpha)) {
			memcpy(shot->rect, gameflowJewelCaseDisc, sizeof(shot->rect));
		}
		else {
			shot->print = _GameflowPosterTexture(record, &shot->printAlpha);
			memcpy(shot->rect, gameflowJewelCasePrint, sizeof(shot->rect));
		}
		shot->alpha = alpha *
			((record->flags & UI_GAMEFLOW_CARD_HIDDEN) ? 0.55f : 1.0f);
		count++;
	}
	UIJewelCase_Draw(shots, count, (int)frame->direction, UIAnim_Delta(),
		(float)padInstrument.substickX / 90.0f,
		(float)padInstrument.substickY / 90.0f, _CurrentMotionMode(),
		!swissSettings.stillDiscs);
	drawInit();
}

static void _DrawGameflow(uiDrawObj_t *evt)
{
	drawGameflowEvent_t *data = (drawGameflowEvent_t*)evt->data;
	const uiSceneFrame_t *scene = UIScene_Frame();
	const uiGameflowFrame_t *frame;
	/* A small grid wraps round the screen, so a card can be drawn twice
	 * while its row leaves at one edge and arrives at the other. */
	gameflowRenderCard_t cards[UI_GAMEFLOW_RENDER_SLOTS * 2u];
	const uiGameflowCardSnapshot_t *selectedRecord;
	const uiGameflowCardSnapshot_t *previousRecord;
	const uiGameflowCardSnapshot_t *focusRecord;
	const uiGameflowDetailSnapshot_t *detail = NULL;
	uiCommandRailFrame_t commandRail;
	gameflowQuad_t detailPose = {{{48.0f, 96.0f}, {228.0f, 96.0f},
		{228.0f, 336.0f}, {48.0f, 336.0f}}};
	uiGameflowLayout_t layout;
	float reveal;
	float titleTravel;
	u32 rows = 0u;
	u32 count = 0u;
	u32 i;
	u32 selectedRecordIndex = 0u;
	u32 previousRecordIndex = 0u;
	u32 focusRecordIndex = 0u;

	if(data == NULL || (scene->scene != UI_SCENE_LIBRARY &&
		scene->scene != UI_SCENE_GAME_DETAIL)) {
		return;
	}
	reveal = _GameflowClamp(scene->chromeProgress * scene->libraryReveal,
		0.0f, 1.0f);
	if(reveal <= 0.0f) {
		return;
	}

	UIGameflow_Update(&data->state, UIAnim_Delta(), _CurrentMotionMode());
	frame = UIGameflow_Frame(&data->state);
	if(frame == NULL || !frame->hasSnapshot || frame->itemCount == 0u) {
		return;
	}
	focusRecord = _GameflowFindRecord(&data->snapshot, frame->focusIndex,
		&focusRecordIndex);
	if(focusRecord != NULL && UIGameflowDetail_Matches(&data->detail,
		frame->generation, frame->focusIndex, focusRecord->gameId,
		strnlen(focusRecord->gameId, sizeof(focusRecord->gameId)))) {
		detail = &data->detail;
	}
	layout = (uiGameflowLayout_t)data->snapshot.layout;
	/* Without the memory for its case, the jewel case is the carousel. */
	if(layout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE && !UIJewelCase_Ready()) {
		layout = UI_GAMEFLOW_LAYOUT_HORIZONTAL;
	}
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		rows = (frame->itemCount + data->snapshot.columns - 1u) /
			data->snapshot.columns;
	}

	for(i = 0u; i < data->snapshot.recordCount; ++i) {
		const uiGameflowCardSnapshot_t *record = &data->snapshot.records[i];
		int copies = rows >= 3u ? 1 : 0;
		int copy;

		if(!(record->flags & UI_GAMEFLOW_CARD_VALID)) {
			continue;
		}
		for(copy = -copies; copy <= copies &&
			count < sizeof(cards) / sizeof(cards[0]); ++copy) {
			gameflowRenderCard_t *card = &cards[count];
			bool focused = copy == 0 &&
				record->libraryIndex == frame->focusIndex;
			float presence;

			card->tile = false;

			if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
				float row = (float)record->relativeSlot +
					frame->carouselTravel + (float)copy * (float)rows;
				float across = (float)record->column - frame->columnPosition;
				float distance = sqrtf(across * across + row * row);

				presence = _GameflowGridPresence(row);
				card->visualSlot = distance;
				card->artSlot = distance < 1.0f ? distance : 1.0f;
				card->focus = 1.0f - _GameflowClamp(distance, 0.0f, 1.0f);
				card->art = fabsf(row) < 1.5f;
				card->quad = _GameflowGridQuad((float)record->column, row,
					card->focus);
			}
			else {
				float slot = (float)record->relativeSlot +
					frame->carouselTravel;

				presence = _GameflowPresence(slot);
				card->visualSlot = slot;
				card->artSlot = slot;
				card->focus = 1.0f - _GameflowClamp(fabsf(slot), 0.0f, 1.0f);
				/* The row shows two covers either side, fading in from the
				 * third; the column's second covers are slivers. */
				card->art = layout == UI_GAMEFLOW_LAYOUT_VERTICAL ?
					fabsf(slot) < 1.5f : fabsf(slot) < 3.0f;
				card->quad = layout == UI_GAMEFLOW_LAYOUT_VERTICAL ?
					_GameflowSamplePoseIn(gameflowVerticalPoses, slot) :
					layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT ?
					_GameflowSamplePoseIn(gameflowSpotlightPoses, slot) :
					_GameflowSamplePose(slot);
				card->tile = layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT;
			}
			if(frame->detailProgress > 0.0f) {
				if(focused) {
					presence = 1.0f;
				}
				else {
					presence *= 1.0f - frame->detailProgress;
				}
			}
			/* Spotlight's selected game leaves its row for Detail or the
			 * launch as its cover, from the middle of the picture panel,
			 * fading in over its still. */
			if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT && focused &&
				(frame->detailProgress > 0.0f ||
				frame->launchProgress > 0.0f)) {
				card->quad = gameflowSpotlightCover;
				card->tile = false;
				presence = frame->detailProgress > frame->launchProgress ?
					frame->detailProgress : frame->launchProgress;
			}
			/* The jewel case shows no cards: its selected game leaves the
			 * case for Detail or the launch as its cover. */
			if(layout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE) {
				card->quad = gameflowJewelCaseCover;
				presence = !focused ? 0.0f :
					frame->detailProgress > frame->launchProgress ?
					frame->detailProgress : frame->launchProgress;
			}
			if(presence <= 0.001f) {
				continue;
			}
			card->record = record;
			card->recordIndex = i;
			card->presence = presence *
				((record->flags & UI_GAMEFLOW_CARD_HIDDEN) ? 0.55f : 1.0f);
			if(frame->detailProgress > 0.0f && focused) {
				int vertex;
				for(vertex = 0; vertex < 4; ++vertex) {
					card->quad.point[vertex] = _GameflowLerpPoint(
						card->quad.point[vertex], detailPose.point[vertex],
						frame->detailProgress);
				}
			}
			if(frame->launchProgress > 0.0f && focused) {
				int vertex;
				for(vertex = 0; vertex < 4; ++vertex) {
					card->quad.point[vertex] = _GameflowLerpPoint(
						card->quad.point[vertex],
						gameflowLaunchPose.point[vertex],
						frame->launchProgress);
				}
			}
			count++;
		}
	}

	/* Painter's order: far/sliver cards first, center-most focus last. */
	for(i = 1u; i < count; ++i) {
		gameflowRenderCard_t card = cards[i];
		u32 j = i;
		while(j > 0u && fabsf(cards[j - 1u].visualSlot) <
			fabsf(card.visualSlot)) {
			cards[j] = cards[j - 1u];
			j--;
		}
		cards[j] = card;
	}

	selectedRecord = _GameflowFindRecord(&data->snapshot,
		frame->selectedIndex, &selectedRecordIndex);
	previousRecord = _GameflowFindRecord(&data->snapshot,
		frame->previousIndex, &previousRecordIndex);
	titleTravel = fabsf(frame->carouselTravel);
	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		/* The title changes as the highlight reaches the new card. */
		float across = fabsf(frame->columnPosition - (float)(
			frame->selectedIndex % data->snapshot.columns));
		titleTravel = across > titleTravel ? across : titleTravel;
	}
	titleTravel = _GameflowClamp(titleTravel, 0.0f, 1.0f);
	if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT) {
		/* The picture changes with the title: the old game's fades out as
		 * the row moves and the new game's fades in. A new still fills the
		 * panel, so it fades in over the old picture, which keeps what of
		 * the mix the still leaves: a true mix at the panel's strength, with
		 * no dip to the empty panel half way. A new cover is smaller, so
		 * the old picture fades out around it; a still arriving over its
		 * cover moves from the one to the other as it fades in. */
		float heroAlpha = reveal * (1.0f - frame->detailProgress) *
			(1.0f - frame->launchProgress);
		float previousAlpha = heroAlpha * titleTravel;

		if(selectedRecord != NULL &&
			_GameflowStillTexture(selectedRecord) != NULL) {
			float arrival = _GameflowStillArrival(selectedRecord);
			float mixed = heroAlpha *
				_GameflowUnderneath(heroAlpha, 1.0f - titleTravel);

			/* Nothing of it shows once the still over it is opaque. */
			previousAlpha = _GameflowAlpha(255.0f * heroAlpha *
				(1.0f - titleTravel) * arrival) == 255u ? 0.0f :
				previousAlpha + (mixed - previousAlpha) * arrival;
		}
		_GameflowDrawSpotlightPanel(heroAlpha);
		_GameflowDrawSpotlightArt(data, previousRecord, previousRecordIndex,
			previousAlpha);
		_GameflowDrawSpotlightArt(data, selectedRecord, selectedRecordIndex,
			heroAlpha * (1.0f - titleTravel));
	}

	if(layout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE) {
		_GameflowDrawJewelCases(data, frame, reveal);
	}

	_GameflowDrawLaunch(data, detail, focusRecord, focusRecordIndex,
		frame->launchProgress, reveal);
	drawInit();
	_SetupRasterColor();
	/* The cards' shells: none in the jewel case, at rest. */
	if(count > 0u) {
		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 4u));
		for(i = 0u; i < count; ++i) {
			u8 topAlpha = _GameflowAlpha((104.0f + cards[i].focus * 64.0f) *
				cards[i].presence * reveal);
			u8 bottomAlpha = _GameflowAlpha(210.0f * cards[i].presence * reveal);
			GXColor top = _GameflowAccent(cards[i].record, topAlpha);
			GXColor bottom = {10, 8, 30, bottomAlpha};
			_GameflowPutQuad(&cards[i].quad, top, bottom);
		}
		GX_End();

		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 4u));
		for(i = 0u; i < count; ++i) {
			gameflowQuad_t inner = cards[i].tile ?
				_GameflowInsetPixels(&cards[i].quad, 4.0f) :
				_GameflowInsetQuad(&cards[i].quad, 0.033333f, 0.033333f);
			GXColor top = {49, 43, 92,
				_GameflowAlpha(130.0f * cards[i].presence * reveal)};
			GXColor bottom = {12, 10, 34,
				_GameflowAlpha(190.0f * cards[i].presence * reveal)};
			_GameflowPutQuad(&inner, top, bottom);
		}
		GX_End();

		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 16u));
		for(i = 0u; i < count; ++i) {
			gameflowQuad_t inner = cards[i].tile ?
				_GameflowInsetPixels(&cards[i].quad, 2.0f) :
				_GameflowInsetQuad(&cards[i].quad, 0.018f, 0.018f);
			GXColor border = {220, 214, 255,
				_GameflowAlpha((112.0f + cards[i].focus * 126.0f) *
				cards[i].presence * reveal)};
			_GameflowPutBorder(&cards[i].quad, &inner, border);
		}
		GX_End();
	}

	/* Actual retail covers own the selected and near-card inset. While an
	 * exact/universal record is loading (or unavailable), retain the native
	 * BNR; use the code-native emblem only when neither texture exists. */
	for(i = 0u; i < count; ++i) {
		GXTexObj *posterTexture;
		GXTexObj *bannerTexture = NULL;
		uiGameflowLibraryArtwork_t artwork;
		float arrival = 1.0f;

		if(!cards[i].art) {
			continue;
		}
		posterTexture = launchActive && launchHasPoster &&
			cards[i].record->libraryIndex == frame->focusIndex ?
			&launchPoster : _GameflowPosterTexture(cards[i].record, &arrival);
		if(cards[i].record->flags & UI_GAMEFLOW_CARD_HAS_BANNER) {
			bannerTexture = &data->bannerTexObj[cards[i].recordIndex];
		}
		else if(detail != NULL &&
			cards[i].record->libraryIndex == frame->focusIndex &&
			(detail->flags & UI_GAMEFLOW_DETAIL_HAS_BANNER) != 0u) {
			bannerTexture = &data->detailBannerTexObj;
		}
		if(cards[i].tile) {
			_GameflowDrawSpotlightTile(&cards[i], bannerTexture, reveal);
			continue;
		}
		artwork = UIGameflowLibrary_ChooseArtwork(posterTexture != NULL,
			bannerTexture != NULL);
		if(artwork == UI_GAMEFLOW_LIBRARY_ART_POSTER) {
			/* A poster just read fades in over what stood in for it. */
			if(arrival < 1.0f) {
				_GameflowDrawFallback(&cards[i],
					UIGameflowLibrary_ChooseArtwork(false,
					bannerTexture != NULL), bannerTexture, reveal *
					_GameflowUnderneath(cards[i].presence * reveal, arrival));
			}
			_GameflowDrawPoster(&cards[i], posterTexture, reveal * arrival);
			continue;
		}
		_GameflowDrawFallback(&cards[i], artwork, bannerTexture, reveal);
	}
	for(i = 0u; i < count; ++i) {
	}
	/* Every card that shows its art carries the mark, sized to the card.
	 * Spotlight's banners are too small for one: its panel carries the
	 * selected game's. */
	for(i = 0u; i < count; ++i) {
		if(cards[i].art && !cards[i].tile &&
			(cards[i].record->flags & UI_GAMEFLOW_CARD_CUSTOM)) {
			_GameflowDrawCustomMark(&cards[i], reveal);
		}
	}

	if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
		_GameflowDrawGridHighlight(frame,
			reveal * (1.0f - frame->detailProgress));
	}

	/* The old game's words fade out over the first half of a move and the
	 * new game's in over the second, as the Source picker's names do: never
	 * the two at once. */
	_GameflowDrawMetadata(previousRecord, previousRecord != NULL ?
		&data->cardPresentation[previousRecordIndex] : NULL,
		_GameflowClamp(2.0f * titleTravel - 1.0f, 0.0f, 1.0f) *
		(1.0f - frame->detailProgress), reveal, layout);
	_GameflowDrawMetadata(selectedRecord, selectedRecord != NULL ?
		&data->cardPresentation[selectedRecordIndex] : NULL,
		_GameflowClamp(1.0f - 2.0f * titleTravel, 0.0f, 1.0f) *
		(1.0f - frame->detailProgress), reveal, layout);
	if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT) {
		_GameflowDrawSpotlightDescription(data,
			_GameflowClamp(1.0f - 2.0f * titleTravel, 0.0f, 1.0f) *
			(1.0f - frame->detailProgress), reveal);
	}

	UICommandRail_Gameflow(frame->detailProgress, &commandRail);
	/* Launching, Detail's panels give way to the launch screen. */
	if(frame->launchProgress < 0.999f) {
		_GameflowDrawDetailDashboard(detail, &data->detailPresentation,
			frame, reveal * (1.0f - frame->launchProgress), &commandRail,
			data->detailFocus, data->detailLit);
	}
	if(frame->detailProgress < 0.999f) {
		GXColor label = {177, 168, 220,
			_GameflowAlpha(180.0f * reveal *
			(1.0f - frame->detailProgress))};
		/* Apps are shown as the Library shows games, under their own name
		 * and with their own controls: A starts one, and that is all. */
		bool apps = (data->snapshot.records[0].flags &
			UI_GAMEFLOW_CARD_APP) != 0u;
		/* Inside a Library Folders folder the heading names it. */
		const char *heading = data->snapshot.folder[0] ?
			data->snapshot.folder : apps ? "APPS" : "GAME LIBRARY";

		if(layout == UI_GAMEFLOW_LAYOUT_VERTICAL) {
			drawStringMedium(262, 177, heading, 0.42f, ALIGN_LEFT,
				label);
		}
		else if(layout == UI_GAMEFLOW_LAYOUT_GRID) {
			drawStringMedium(320, 40, heading, 0.46f, ALIGN_CENTER,
				label);
		}
		else if(layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT) {
			drawStringMedium(36, 62, heading, 0.42f, ALIGN_LEFT,
				label);
		}
		else {
			drawStringMedium(320, 70, heading, 0.50f, ALIGN_CENTER,
				label);
		}
		if(commandRail.owner == UI_COMMAND_RAIL_LIBRARY &&
				commandRail.alpha > 0.001f) {
			GXColor command = label;
			command.a = _GameflowAlpha(180.0f * reveal * commandRail.alpha);
			_DrawHintText(320, 428, apps ?
				"D-PAD  BROWSE   A  START   B  HOME" :
				selectedRecord != NULL && selectedRecord->subfolder ?
				(data->snapshot.folder[0] ?
				"D-PAD  BROWSE   A  OPEN   B  BACK" :
				"D-PAD  BROWSE   A  OPEN   X  BACK   B  HOME") :
				data->snapshot.folder[0] ?
				"D-PAD  BROWSE   A  OPEN   Y  SETTINGS   B  BACK" :
				"D-PAD  BROWSE   A  OPEN   Y  SETTINGS   X  BACK   B  HOME",
				0.46f, ALIGN_CENTER, command);
		}
	}
	drawInit();
}

static void _DrawHomeText(int x, int y, const char *text, float scale,
		int align, GXColor color)
{
	/* drawStringMedium already adds exactly one native-pixel coverage pass.
	 * Do not stack extra font passes on the Gekko merely to fake a keyline. */
	drawStringMedium(x, y, text, scale, align, color);
}

/* weight runs from the idle panel (0) to the selected one (1). */
static void _DrawHomePanel(int x, int y, int width, int height,
		float reveal, float weight, bool danger)
{
	GXColor fill = danger ? (GXColor) {82, 20, 42, 0} :
		(GXColor) {20, 13, 58, 0};
	GXColor edge = danger ? (GXColor) {255, 132, 158, 0} :
		(GXColor) {191, 174, 255, 0};
	GXColor glow = danger ? (GXColor) {255, 75, 118, 0} :
		(GXColor) {117, 88, 244, 0};

	fill.a = (u8)((72.0f + 76.0f * weight) * reveal);
	edge.a = (u8)((92.0f + 126.0f * weight) * reveal);
	glow.a = (u8)((28.0f + 56.0f * weight) * reveal);
	drawInit();
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL,
		GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
		GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
		GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
		GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
		GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
		GX_LO_CLEAR);
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 12);
		_putFlatRect((float)x - 3.0f, (float)y - 3.0f,
			(float)width + 6.0f, (float)height + 6.0f, glow);
		_putFlatRect((float)x, (float)y, (float)width, (float)height, fill);
		_putFlatRect((float)x, (float)y, 2.0f + 2.0f * weight,
			(float)height, edge);
	GX_End();
	drawInit();
}

static void _DrawHomeModalDepth(const uiHomeLayout_t *layout, float reveal)
{
	int width = layout->modalBounds.right - layout->modalBounds.left;
	int height = layout->modalBounds.bottom - layout->modalBounds.top;
	GXColor scrim = {4, 3, 16, (u8)(104.0f * reveal)};
	GXColor card = {18, 11, 48, (u8)(220.0f * reveal)};
	GXColor edge = {169, 145, 235, (u8)(112.0f * reveal)};

	/* The confirmation surface sits above the hero scene. One darkening pass
	 * quiets the cube, orbits and silk without replacing their spatial
	 * context; the opaque lower card keeps consequence copy crisp at 480i. */
	drawInit();
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL,
		GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
		GX_CC_RASC);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
		GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
		GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
		GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
		GX_LO_CLEAR);
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 12);
		_putFlatRect(UIStage_Left(), 0.0f, UIStage_Right() - UIStage_Left(), 480.0f, scrim);
		_putFlatRect((float)layout->modalBounds.left,
			(float)layout->modalBounds.top, (float)width, (float)height,
			card);
		_putFlatRect((float)layout->modalBounds.left,
			(float)layout->modalBounds.top, (float)width, 2.0f, edge);
	GX_End();
	drawInit();
}

static void _DrawHomeRoot(const drawHomeEvent_t *data,
		const uiSceneFrame_t *scene, float reveal)
{
	const uiHomeLayout_t *layout = &data->layout;
	uiHomeFace_t face = scene->homeFace;
	float progress = scene->homeFocusProgress;
	float eased;
	float incomingAlpha;
	float scale;
	float travel;
	int incomingX;
	int incomingY;
	GXColor primary = {250, 248, 255, (u8)(255.0f * reveal)};
	GXColor muted = {157, 146, 205, (u8)(184.0f * reveal)};
	GXColor incoming;
	uiMotionMode_t motionMode = _CurrentMotionMode();

	eased = UIMotion_Smoothstep(progress);

	/* The ring names only the selected face, under the cube. Nothing sits
	 * above or beside it: a label the cube does not carry through the turn
	 * reads as detached from it. */
	incomingAlpha = 0.34f + eased * 0.66f;
	travel = UIMotion_Amplitude((float)UI_HOME_LAYOUT_SELECTED_TRAVEL,
		motionMode);
	incomingX = layout->selectedLabelCenter.x;
	incomingY = layout->selectedLabelCenter.y;
	if(scene->homeTurnAxis == UI_HOME_TURN_VERTICAL) {
		incomingY += (int)((float)scene->homeTurnDirection * (1.0f - eased) *
			UIMotion_Amplitude((float)UI_HOME_LAYOUT_SELECTED_VERTICAL_TRAVEL, motionMode));
	}
	else {
		incomingX += (int)((float)scene->homeTurnDirection * (1.0f - eased) * travel);
	}
	incoming = primary;
	incoming.a = (u8)((float)incoming.a * incomingAlpha);
	scale = data->focusScale[(int)face];
	/* Face Labels and On-screen Controls off leave the cube on its own. */
	if(!data->capabilities.hideFaceLabel) {
		_DrawHomeText(incomingX, incomingY,
			UIHome_FaceLabel(face), scale,
			ALIGN_CENTER, incoming);
	}
	if(!data->capabilities.hideCommands) {
		_DrawHintText(layout->commandCenter.x, layout->commandCenter.y,
			data->command, data->commandScale,
			ALIGN_CENTER, muted);
	}
}

static void _DrawHomeRows(const drawHomeEvent_t *data, float reveal)
{
	const uiHomeLayout_t *layout = &data->layout;
	int rowCount = layout->rowCount;
	int row;
	GXColor primary = {250, 248, 255, (u8)(255.0f * reveal)};
	GXColor muted = {164, 153, 212, (u8)(190.0f * reveal)};

	for(row = 0; row < rowCount; ++row) {
		const uiHomeLayoutItem_t *item = &layout->rows[row];
		int width = item->panelBounds.right - item->panelBounds.left;
		int height = item->panelBounds.bottom - item->panelBounds.top;
		float weight = data->rowWeight[row].value;
		const char *label = UIHome_RowLabel(data->state.surface, row);
		_DrawHomePanel(item->panelBounds.left, item->panelBounds.top,
			width, height, reveal, weight, false);
		_DrawHomeText(item->labelCenter.x, item->labelCenter.y, label,
			data->rowIdleScale[row] + (data->rowSelectedScale[row] -
				data->rowIdleScale[row]) * weight,
			ALIGN_CENTER, _GameflowMixColor(muted, primary, weight));
	}
	if(!data->capabilities.hideCommands) {
		_DrawHintText(layout->commandCenter.x, layout->commandCenter.y,
			homeContextCommand, data->contextCommandScale,
			ALIGN_CENTER, muted);
	}
}

static void _DrawHomeContext(const drawHomeEvent_t *data, float reveal)
{
	GXColor title = {220, 211, 255, (u8)(234.0f * reveal)};

	_DrawHomeText(data->layout.titleCenter.x, data->layout.titleCenter.y,
		data->heading, data->headingScale,
		ALIGN_CENTER, title);
	_DrawHomeRows(data, reveal);
}

static void _DrawHomeRestartConfirm(const drawHomeEvent_t *data,
		float reveal)
{
	int row;
	GXColor title = {255, 225, 232, (u8)(250.0f * reveal)};
	GXColor primary = {255, 247, 249, (u8)(255.0f * reveal)};
	GXColor muted = {205, 169, 184, (u8)(202.0f * reveal)};
	GXColor consequence = {220, 210, 239, (u8)(224.0f * reveal)};

	_DrawHomeModalDepth(&data->layout, reveal);
	_DrawHomeText(data->layout.titleCenter.x, data->layout.titleCenter.y,
		"RESTART INDIGO?", 0.60f, ALIGN_CENTER, title);
	_DrawHomeText(data->layout.consequenceCenter.x,
		data->layout.consequenceCenter.y, homeRestartConsequence,
		data->consequenceScale, ALIGN_CENTER, consequence);
	for(row = 0; row < 2; ++row) {
		const uiHomeLayoutItem_t *item = &data->layout.options[row];
		int width = item->panelBounds.right - item->panelBounds.left;
		int height = item->panelBounds.bottom - item->panelBounds.top;
		float weight = data->rowWeight[row].value;
		const char *label = UIHome_RowLabel(
			UI_HOME_SURFACE_RESTART_CONFIRM, row);
		_DrawHomePanel(item->panelBounds.left, item->panelBounds.top,
			width, height, reveal, weight, row == 1);
		_DrawHomeText(item->labelCenter.x, item->labelCenter.y, label,
			0.49f + 0.05f * weight,
			ALIGN_CENTER, _GameflowMixColor(muted, primary, weight));
	}
	_DrawHintText(data->layout.commandCenter.x, data->layout.commandCenter.y,
		homeConfirmCommand, data->confirmCommandScale,
		ALIGN_CENTER, muted);
}

/* Spring each row of the surface towards its style: selected or idle. */
static void _HomeUpdateRowWeights(drawHomeEvent_t *data,
		uiMotionMode_t motionMode)
{
	bool confirm = data->state.surface == UI_HOME_SURFACE_RESTART_CONFIRM;
	const uiHomeLayoutItem_t *items = confirm ?
		data->layout.options : data->layout.rows;
	int count = confirm ? data->layout.optionCount : data->layout.rowCount;
	int row;

	for(row = 0; row < count && row < UI_HOME_LAYOUT_MAX_ROWS; ++row) {
		float target = items[row].selected ? 1.0f : 0.0f;
		if(!data->rowsPlaced) {
			UIMotion_SpringInit(&data->rowWeight[row], target,
				HOME_ROW_RESPONSE);
		}
		UIMotion_SpringRetarget(&data->rowWeight[row], target, motionMode);
		UIMotion_SpringUpdate(&data->rowWeight[row], UIAnim_Delta(),
			motionMode);
	}
	data->rowsPlaced = true;
}

// Internal
static void _DrawHome(uiDrawObj_t *evt)
{
	drawHomeEvent_t *data = (drawHomeEvent_t*)evt->data;
	const uiSceneFrame_t *scene = UIScene_Frame();
	/* Back from another screen the name and hint fade in as the cube grows
	 * to its Home size (every other scene's is 0.70 or less), as the Source
	 * row rises with it. Off snaps the cube, so they show at once. */
	float reveal = scene->chromeProgress *
		UIMotion_Smoothstep((scene->cubeScale - 0.70f) / 0.18f);
	uiMotionMode_t motionMode = _CurrentMotionMode();

	if(!data->visible || !data->layoutValid || reveal <= 0.0f ||
		scene->scene != UI_SCENE_HOME || scene->homeFace != data->state.face ||
		!UIHome_IsFace((int)data->state.face) ||
		!UIHome_IsSurface((int)data->state.surface)) {
		return;
	}
	if(reveal > 1.0f) reveal = 1.0f;
	/* A new surface (the ring, a face's rows, the restart question) fades
	 * in over a moment rather than appearing whole. */
	if(data->surfaceAge < HOME_SURFACE_REVEAL) {
		data->surfaceAge += UIAnim_Delta();
	}
	if(motionMode != UI_MOTION_OFF) {
		reveal *= UIMotion_Smoothstep(data->surfaceAge / HOME_SURFACE_REVEAL);
	}
	_HomeUpdateRowWeights(data, motionMode);
	if(data->state.surface == UI_HOME_SURFACE_RING) {
		_DrawHomeRoot(data, scene, reveal);
	}
	else if(data->state.surface == UI_HOME_SURFACE_RESTART_CONFIRM) {
		_DrawHomeRestartConfirm(data, reveal);
	}
	else {
		_DrawHomeContext(data, reveal);
	}
}

// External
uiDrawObj_t* DrawHome(void)
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	drawHomeEvent_t *eventData = calloc(1, sizeof(drawHomeEvent_t));
	event->type = EV_HOME;
	event->data = eventData;
	/* The first surface shows with the boot's chrome, not after it. */
	eventData->surfaceAge = HOME_SURFACE_REVEAL;
	return event;
}

/* The Source picker: the focused device's tile in the middle of a row
 * under the cube, up to two more each side, smaller and dimmer the farther
 * out. The row slides on a spring and wraps round. */
#define DEVICE_ROW_Y 262.0f
#define DEVICE_TILE_W 168.0f
#define DEVICE_TILE_H 124.0f
#define DEVICE_NEAR_STEP 158.0f
#define DEVICE_FAR_STEP 80.0f
#define DEVICE_ROW_SLOTS 5

/* The device a place in the unwrapped row shows. */
static int _DeviceIndex(int travel, int count)
{
	int index = travel % count;

	return index < 0 ? index + count : index;
}

static void _DeviceRect(float cx, float cy, float width, float height,
	GXColor top, GXColor bottom)
{
	float x = cx - width / 2.0f;
	float y = cy - height / 2.0f;

	_putFlatVertex(x, y, top);
	_putFlatVertex(x + width, y, top);
	_putFlatVertex(x + width, y + height, bottom);
	_putFlatVertex(x, y + height, bottom);
}

/* A frame thickness wide just inside a centred rectangle: four quads. */
static void _DeviceFrame(float cx, float cy, float width, float height,
	float thickness, GXColor color)
{
	float x = cx - width / 2.0f;
	float y = cy - height / 2.0f;

	_putFlatRect(x, y, width, thickness, color);
	_putFlatRect(x, y + height - thickness, width, thickness, color);
	_putFlatRect(x, y + thickness, thickness, height - thickness * 2.0f, color);
	_putFlatRect(x + width - thickness, y + thickness, thickness,
		height - thickness * 2.0f, color);
}

/* One glass tile, place steps from the middle, and its device's picture. */
static void _DrawDeviceTile(const drawDeviceTile_t *tile, float place,
	float y, float alpha)
{
	const textureImage *picture = &tile->picture;
	float offset = fabsf(place);
	float side = offset < 1.0f ? offset : 1.0f;
	float beyond = offset > 1.0f ? offset - 1.0f : 0.0f;
	float scale = 1.0f - 0.30f * side - 0.18f * beyond;
	float focus = 1.0f - side;
	float x = 320.0f + copysignf(DEVICE_NEAR_STEP * side +
		DEVICE_FAR_STEP * beyond, place);
	float width = DEVICE_TILE_W * scale;
	float height = DEVICE_TILE_H * scale;

	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 28);
		_DeviceRect(x, y, width, height,
			(GXColor) {104, 88, 190, (u8)((96.0f + 72.0f * focus) * alpha)},
			(GXColor) {10, 8, 30, (u8)(214.0f * alpha)});
		_DeviceRect(x, y, width - 10.0f * scale, height - 10.0f * scale,
			(GXColor) {49, 43, 92, (u8)(130.0f * alpha)},
			(GXColor) {12, 10, 34, (u8)(190.0f * alpha)});
		_DeviceFrame(x, y, width, height, 2.0f,
			(GXColor) {220, 214, 255, (u8)((90.0f + 140.0f * focus) * alpha)});
		/* Light along the glass's top edge. */
		_putFlatRect(x - width / 2.0f + 4.0f, y - height / 2.0f + 3.0f,
			width - 8.0f, 1.0f,
			(GXColor) {255, 255, 255, (u8)((34.0f + 56.0f * focus) * alpha)});
	GX_End();
	drawInit();
	if(picture->width > 0 && picture->height > 0) {
		/* Swiss's device-picture sizing, in a box that shrinks with the tile. */
		float fit = fminf(fminf(120.0f / picture->width,
			84.0f / picture->height), 1.0f) * scale;
		int w = (int)(picture->realWidth * fit);
		int h = (int)(picture->realHeight * fit);
		float light = (0.5f + 0.5f * focus) * (1.0f - 0.35f * beyond) *
			(tile->detected ? 1.0f : 0.45f);

		_DrawImageNow(picture->textureId, (int)lrintf(x - w / 2.0f),
			(int)lrintf(y - h / 2.0f), w, h, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0,
			(u8)(255.0f * light * alpha));
		drawInit();
	}
}

static void _DrawDeviceSelector(uiDrawObj_t *evt)
{
	drawDeviceSelectorEvent_t *data = (drawDeviceSelectorEvent_t*)evt->data;
	const drawDeviceSelectorSnapshot_t *s = &data->snapshot;
	const drawDeviceTile_t *tile;
	uiMotionMode_t motion = _CurrentMotionMode();
	struct {
		float place;
		float presence;
		int device;
	} row[DEVICE_ROW_SLOTS];
	const uiSceneFrame_t *scene = UIScene_Frame();
	/* The row rises into view as the cube lifts and shrinks out of its way,
	 * as the Library's posters follow the cube's retreat. */
	float rise = UIMotion_Smoothstep((0.92f - scene->cubeScale) / 0.30f);
	float reveal = fminf(scene->chromeProgress, 1.0f);
	float position, first, last, y, pulse, labels, inset;
	int tiles = 0;
	int nearest, k, i, corner;

	reveal *= rise;
	if(s->count <= 0 || reveal <= 0.0f) {
		return;
	}
	/* A new list (Z) snaps into place: its travel counts from its start. */
	if(!data->started || data->shownAll != s->showAllDevices) {
		UIMotion_SpringInit(&data->position, (float)s->travel, 13.0f);
		data->shownAll = s->showAllDevices;
		data->started = true;
	}
	UIMotion_SpringRetarget(&data->position, (float)s->travel, motion);
	position = UIMotion_SpringUpdate(&data->position, UIAnim_Delta(), motion);
	y = DEVICE_ROW_Y + (1.0f - rise) * 24.0f;

	/* Copy/Move's destination opens over the file list: a page behind it. */
	if(data->destination) {
		drawInit();
		_SetupRasterColor();
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			_putFlatRect(UIStage_Left(), 0.0f, UIStage_Right() - UIStage_Left(),
				480.0f, (GXColor) {8, 12, 27, (u8)(236.0f * reveal)});
		GX_End();
	}

	/* The places that show a device at rest: two each side, or as many as
	 * there are other devices, so none shows twice. A tile leaving them
	 * fades out over half a place. */
	last = s->count >= DEVICE_ROW_SLOTS ? 2.0f : (float)(s->count / 2);
	first = s->count >= DEVICE_ROW_SLOTS ? -2.0f : -(float)((s->count - 1) / 2);
	for(k = (int)floorf(position + first - 0.5f);
			k <= (int)ceilf(position + last + 0.5f); ++k) {
		float place = (float)k - position;
		float presence = place < first ? 1.0f - 2.0f * (first - place) :
			place > last ? 1.0f - 2.0f * (place - last) : 1.0f;

		if(presence <= 0.001f || tiles == DEVICE_ROW_SLOTS) {
			continue;
		}
		/* Painter's order: the farthest first, the middle one last. */
		for(i = tiles++; i > 0 && fabsf(row[i - 1].place) < fabsf(place); --i) {
			row[i] = row[i - 1];
		}
		row[i].place = place;
		row[i].presence = presence;
		row[i].device = _DeviceIndex(k, s->count);
	}
	for(i = 0; i < tiles; ++i) {
		_DrawDeviceTile(&s->tiles[row[i].device], row[i].place, y,
			row[i].presence * reveal);
	}

	/* The focus frame stays in the middle while the tiles slide through. */
	pulse = motion == UI_MOTION_FULL ?
		0.84f + 0.16f * sinf(UIAnim_Seconds() * 3.0f) : 1.0f;
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 48);
		_DeviceFrame(320.0f, y, DEVICE_TILE_W + 14.0f, DEVICE_TILE_H + 14.0f,
			3.0f, (GXColor) {117, 88, 244, (u8)(78.0f * reveal * pulse)});
		_DeviceFrame(320.0f, y, DEVICE_TILE_W + 8.0f, DEVICE_TILE_H + 8.0f,
			2.0f, (GXColor) {196, 177, 255, (u8)(150.0f * reveal * pulse)});
		_DeviceFrame(320.0f, y, DEVICE_TILE_W + 4.0f, DEVICE_TILE_H + 4.0f,
			2.0f, (GXColor) {244, 239, 255, (u8)(245.0f * reveal)});
	GX_End();
	drawInit();

	/* The words are the middle tile's: they fade out, and the next tile's
	 * in, as the row slides. */
	nearest = (int)floorf(position + 0.5f);
	labels = reveal * (1.0f - 2.0f * fabsf(position - (float)nearest));
	tile = &s->tiles[_DeviceIndex(nearest, s->count)];
	/* In the header's free corner. */
	corner = _FreeCorner(&inset);
	drawStringMedium((int)(corner > 0 ? 600.0f - inset : 40.0f + inset), 44,
		data->destination ? "DESTINATION" : "SOURCE", 0.50f,
		corner > 0 ? ALIGN_RIGHT : ALIGN_LEFT,
		(GXColor) {216, 207, 255, (u8)(230.0f * reveal)});
	drawStringMedium(320, (int)(y + 88.0f), tile->name, tile->nameScale,
		ALIGN_CENTER, (GXColor) {246, 243, 255, (u8)(255.0f * labels)});
	drawStringMedium(320, (int)(y + 110.0f), !s->inAdvanced ? tile->facts :
		s->exiFast ? "\213  EXI  27 MHz  \233" : "\213  EXI  13.5 MHz  \233",
		0.50f, ALIGN_CENTER, (GXColor) {216, 207, 255, (u8)(245.0f * labels)});
	drawStringMedium(320, (int)(y + 130.0f), tile->status, 0.44f,
		ALIGN_CENTER, tile->detected ?
		(GXColor) {178, 166, 224, (u8)(230.0f * labels)} :
		(GXColor) {255, 207, 139, (u8)(240.0f * labels)});
	_DrawHintText(320, 440, s->hint, s->hintScale, ALIGN_CENTER,
		(GXColor) {178, 166, 224, (u8)(225.0f * reveal)});
	drawInit();
}

/* What a device can do, where it plugs in and whether it's there, in the
 * picker's words. Menu thread only: it reads the device's handler. */
static void _DeviceSelectorTile(drawDeviceTile_t *tile,
	DEVICEHANDLER_INTERFACE *device, bool current, bool settings)
{
	static const struct {
		u32 location;
		const char *name;
	} places[] = {
		{LOC_MEMCARD_SLOT_A, "SLOT A"}, {LOC_MEMCARD_SLOT_B, "SLOT B"},
		{LOC_SERIAL_PORT_1, "SERIAL PORT 1"},
		{LOC_SERIAL_PORT_2, "SERIAL PORT 2"}, {LOC_HSP, "HI-SPEED PORT"},
		{LOC_DVD_CONNECTOR, "DISC DRIVE"}, {LOC_SYSTEM, "CONSOLE"}
	};
	const char *place = "";
	size_t i;

	for(i = 0; i < sizeof(places) / sizeof(places[0]); ++i) {
		if(device->location & places[i].location) {
			place = places[i].name;
			break;
		}
	}
	tile->picture = device->deviceTexture;
	snprintf(tile->name, sizeof(tile->name), "%s", DeviceDisplayName(device));
	snprintf(tile->facts, sizeof(tile->facts), "%s%s%s",
		!(device->features & FEAT_BOOT_GCM) ? "FILES" :
		(device->features & FEAT_AUDIO_STREAMING) ? "BOOT + STREAM" : "BOOT",
		place[0] ? "  \267  " : "", place);
	tile->detected = deviceHandler_getDeviceAvailable(device);
	snprintf(tile->status, sizeof(tile->status), "%s%s%s",
		tile->detected ? "DETECTED" : "NOT DETECTED",
		current ? "  \267  CURRENT" : "", settings ? "  \267  SETTINGS" : "");
	tile->nameScale = GetTextScaleToFitInWidthWithMax(tile->name, 300, 0.76f);
}

uiDrawObj_t* DrawDeviceSelector(bool destination)
{
	drawDeviceSelectorEvent_t *eventData = calloc(1, sizeof(drawDeviceSelectorEvent_t));
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));

	eventData->destination = destination;
	event->type = EV_DEVICESELECTOR;
	event->data = eventData;
	return event;
}

void DrawUpdateDeviceSelector(uiDrawObj_t *selector,
	DEVICEHANDLER_INTERFACE *const *listed, int count, int travel,
	const DEVICEHANDLER_INTERFACE *current,
	const DEVICEHANDLER_INTERFACE *settings, bool showAllDevices,
	bool inAdvanced)
{
	/* Built outside the video lock, then copied in one step. */
	static drawDeviceSelectorSnapshot_t snapshot;
	drawDeviceSelectorEvent_t *data = (drawDeviceSelectorEvent_t*)selector->data;
	const DEVICEHANDLER_INTERFACE *device;
	const drawDeviceTile_t *tile;
	int i;

	if(count <= 0 || count > MAX_DEVICES) {
		return;
	}
	memset(&snapshot, 0, sizeof(snapshot));
	for(i = 0; i < count; ++i) {
		_DeviceSelectorTile(&snapshot.tiles[i], listed[i],
			listed[i] == current, listed[i] == settings);
	}
	device = listed[_DeviceIndex(travel, count)];
	tile = &snapshot.tiles[_DeviceIndex(travel, count)];
	snapshot.count = count;
	snapshot.travel = travel;
	snapshot.showAllDevices = showAllDevices;
	snapshot.inAdvanced = inAdvanced;
	snapshot.exiFast = swissSettings.exiSpeed != 0;
	if(inAdvanced) {
		strcpy(snapshot.hint, "STICK / D-PAD  SPEED   A  DONE   B  BACK");
	}
	else {
		snprintf(snapshot.hint, sizeof(snapshot.hint),
			"%sA  %s%s%s   Z  %s   B  BACK",
			count > 1 ? "STICK / D-PAD  CHANGE   " : "",
			!tile->detected ? "TRY" : data->destination ? "SELECT" : "OPEN",
			device->details ? "   Y  INFO" : "",
			(device->features & FEAT_EXI_SPEED) ? "   X  EXI" : "",
			showAllDevices ? "DETECTED" : "ALL");
	}
	snapshot.hintScale = GetHintScaleToFitInWidthWithMax(snapshot.hint, 600,
		0.46f);
	LWP_MutexLock(_videomutex);
	data->snapshot = snapshot;
	LWP_MutexUnlock(_videomutex);
}

/* A copy in the File Browser: the same card over a scrim, titled with what
 * goes where, the destination's path under it, and B Stop. */
uiDrawObj_t* DrawProgressBarFiles(const char *title, const char *path)
{
	uiDrawObj_t *event = DrawProgressBar(false, 0, NULL);
	int middleY = ((480/2) - (PROGRESS_BOX_HEIGHT/2) + (480/2) + (PROGRESS_BOX_HEIGHT/2)) / 2;

	((drawProgressEvent_t*)event->data)->files = true;
	DrawAddChild(event, DrawStyledLabel(640/2, middleY - 6, title,
		GetTextScaleToFitInWidthWithMax(title, PROGRESS_BOX_WIDTH - 40, 0.8f), ALIGN_CENTER,
		defaultColor));
	DrawAddChild(event, DrawStyledLabel(640/2, middleY + 12, path,
		GetTextScaleToFitInWidthWithMax(path, PROGRESS_BOX_WIDTH - 40, 0.46f), ALIGN_CENTER,
		defaultColor));
	return event;
}

void DrawUpdateProgressBar(uiDrawObj_t *evt, int percent) {
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	data->percent = percent;
}

void DrawUpdateProgressBarDetail(uiDrawObj_t *evt, int percent, int speed, int timestart, int timeremain) {
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	data->percent = percent;
	data->speed = speed;
	data->timestart = timestart;
	data->timeremain = timeremain;
}

void DrawUpdateProgressLoading(uiDrawObj_t *evt, int increment) {
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	data->speed += increment;
}

static void _PrepareHomeText(drawHomeEvent_t *data)
{
	int commandWidth = data->layout.commandGlyphBounds.right -
		data->layout.commandGlyphBounds.left;
	int focusWidth = data->layout.selectedLabelBounds.right -
		data->layout.selectedLabelBounds.left;
	int titleWidth = data->layout.titleBounds.right -
		data->layout.titleBounds.left;
	char headingSource[sizeof(data->heading)];
	int face;
	int row;

	for(face = 0; face < UI_HOME_FACE_COUNT; ++face) {
		data->focusScale[face] = UIHomeText_FitScale(
			UIHome_FaceLabel((uiHomeFace_t)face),
			focusWidth, 0.78f, GetTextSizeInPixels);
	}
	/* Y opens Settings anywhere on Home; it says so while Settings has no
	 * side to turn to. TURN only while there is a face to turn to. */
	snprintf(data->command, sizeof(data->command),
		"%s%s%s%s", data->state.faceCount > 1 ? "STICK / D-PAD  TURN    " : "",
		UIHome_PrimaryHint(data->state.face, data->capabilities),
		data->capabilities.hasRecent ? "    START  RECENT" : "",
		UIHome_RingIndex(&data->state, UI_HOME_FACE_SETTINGS) < 0 ?
			"    Y  SETTINGS" : "");
	data->commandScale = UIHomeText_FitScale(data->command,
		commandWidth, 0.46f, GetHintSizeInPixels);
	if(data->state.surface == UI_HOME_SURFACE_SOURCE) {
		if(data->capabilities.hasSource && data->sourceName[0] != '\0') {
			snprintf(headingSource, sizeof(headingSource), "SOURCE   %s",
				data->sourceName);
		}
		else {
			strcpy(headingSource, "SOURCE   NO SOURCE");
		}
	}
	else {
		strncpy(headingSource, UIHome_SurfaceTitle(data->state.surface),
			sizeof(headingSource) - 1u);
		headingSource[sizeof(headingSource) - 1u] = '\0';
	}
	data->headingScale = UIHomeText_CopyFitted(data->heading,
		sizeof(data->heading), headingSource, titleWidth, 0.58f,
		GetTextSizeInPixels, NULL);
	for(row = 0; row < UI_HOME_LAYOUT_MAX_ROWS; ++row) {
		const char *label = UIHome_RowLabel(data->state.surface, row);
		if(row < data->layout.rowCount && label[0] != '\0') {
			int rowWidth = data->layout.rows[row].labelBounds.right -
				data->layout.rows[row].labelBounds.left;
			data->rowSelectedScale[row] = UIHomeText_FitScale(label,
				rowWidth, 0.55f, GetTextSizeInPixels);
			data->rowIdleScale[row] = UIHomeText_FitScale(label,
				rowWidth, 0.50f, GetTextSizeInPixels);
		}
		else {
			data->rowSelectedScale[row] = 0.55f;
			data->rowIdleScale[row] = 0.50f;
		}
	}
	data->contextCommandScale = UIHomeText_FitScale(homeContextCommand,
		commandWidth, 0.46f, GetHintSizeInPixels);
	data->confirmCommandScale = UIHomeText_FitScale(homeConfirmCommand,
		commandWidth, 0.46f, GetHintSizeInPixels);
	data->consequenceScale = data->state.surface ==
		UI_HOME_SURFACE_RESTART_CONFIRM ?
		UIHomeText_FitScale(homeRestartConsequence,
			data->layout.consequenceBounds.right -
				data->layout.consequenceBounds.left,
			0.50f, GetTextSizeInPixels) : 0.50f;
}

void DrawUpdateHome(const uiHomeState_t *state,
		uiHomeCapabilities_t capabilities, const char *sourceName)
{
	LWP_MutexLock(_videomutex);
	if(buttonPanel && buttonPanel->data) {
		drawHomeEvent_t *data = (drawHomeEvent_t*)buttonPanel->data;
		data->visible = state != NULL;
		data->layoutValid = false;
		if(state != NULL) {
			if(state->surface != data->state.surface) {
				data->surfaceAge = 0.0f;
				data->rowsPlaced = false;
			}
			data->state = *state;
			data->capabilities = capabilities;
			if(sourceName != NULL) {
				strncpy(data->sourceName, sourceName,
					sizeof(data->sourceName) - 1u);
				data->sourceName[sizeof(data->sourceName) - 1u] = '\0';
			}
			else {
				data->sourceName[0] = '\0';
			}
			data->layoutValid = UIHomeLayout_Compute(&data->state,
				data->capabilities, &data->layout);
			if(data->layoutValid) {
				_PrepareHomeText(data);
			}
			else {
				data->visible = false;
			}
			/* Publish cube and semantic foreground in one video transaction. */
			UIScene_RequestHome(state);
		}
	}
	LWP_MutexUnlock(_videomutex);
}

static bool _GameflowSnapshotValid(const uiGameflowRenderSnapshot_t *snapshot)
{
	bool hasSelected = false;
	bool grid;
	u32 i;
	u32 j;

	if(snapshot == NULL || snapshot->selection.itemCount == 0u ||
		snapshot->selection.selectedIndex >= snapshot->selection.itemCount ||
		snapshot->recordCount == 0u ||
		snapshot->recordCount > UI_GAMEFLOW_RENDER_SLOTS ||
		snapshot->recordCount > snapshot->selection.itemCount ||
		snapshot->layout >= UI_GAMEFLOW_LAYOUT_COUNT) {
		return false;
	}
	/* A grid names its columns; the carousels keep their nine cards. */
	grid = snapshot->layout == UI_GAMEFLOW_LAYOUT_GRID;
	if(grid ? snapshot->columns == 0u ||
		snapshot->columns > UI_GAMEFLOW_LIBRARY_GRID_COLUMNS :
		snapshot->columns != 0u ||
		snapshot->recordCount > UI_GAMEFLOW_LIBRARY_WINDOW) {
		return false;
	}
	for(i = 0u; i < snapshot->recordCount; ++i) {
		const uiGameflowCardSnapshot_t *record = &snapshot->records[i];
		if(!(record->flags & UI_GAMEFLOW_CARD_VALID) ||
			record->libraryIndex >= snapshot->selection.itemCount ||
			record->relativeSlot < -4 || record->relativeSlot > 4 ||
			(grid && (record->column >= snapshot->columns ||
			record->relativeSlot < -2 || record->relativeSlot > 2))) {
			return false;
		}
		if(record->libraryIndex == snapshot->selection.selectedIndex &&
			record->relativeSlot == 0) {
			hasSelected = true;
		}
		for(j = i + 1u; j < snapshot->recordCount; ++j) {
			if(record->libraryIndex == snapshot->records[j].libraryIndex) {
				return false;
			}
		}
	}
	return hasSelected;
}

static void _GameflowClosePosterPackFile(void)
{
	if(posterPackFileOwned && posterPackFile.device != NULL &&
		posterPackFile.device->closeFile != NULL) {
		posterPackFile.device->closeFile(&posterPackFile);
	}
	memset(&posterPackFile, 0, sizeof(posterPackFile));
	posterPackFileOwned = false;
}

static void _GameflowCloseStillsPackFile(void)
{
	if(stillsPackFileOwned && stillsPackFile.device != NULL &&
		stillsPackFile.device->closeFile != NULL) {
		stillsPackFile.device->closeFile(&stillsPackFile);
	}
	memset(&stillsPackFile, 0, sizeof(stillsPackFile));
	stillsPackFileOwned = false;
}

static void _GameflowCloseDiscsPackFile(void)
{
	if(discsPackFileOwned && discsPackFile.device != NULL &&
		discsPackFile.device->closeFile != NULL) {
		discsPackFile.device->closeFile(&discsPackFile);
	}
	memset(&discsPackFile, 0, sizeof(discsPackFile));
	discsPackFileOwned = false;
}

static void _GameflowDropDescriptions(void)
{
	free(descriptionsText);
	free(descriptionsIndex);
	descriptionsText = NULL;
	descriptionsIndex = NULL;
	memset(&descriptions, 0, sizeof(descriptions));
	descriptionsDevice = NULL;
}

/* Read swiss/ui/descriptions.txt whole and index it. A missing, empty or
 * oversized file, or one that cannot be read, leaves no descriptions. */
static void _GameflowLoadDescriptions(DEVICEHANDLER_INTERFACE *device)
{
	file_handle file;
	size_t size;
	size_t games = 1u;
	size_t i;

	memset(&file, 0, sizeof(file));
	concat_path(file.name, device->initial->name,
		"swiss/ui/descriptions.txt");
	file.device = device;
	if(device->statFile == NULL || device->seekFile == NULL ||
		device->readFile == NULL || device->statFile(&file) != 0 ||
		file.size == 0 || file.size > UI_ABOUT_MAX_BYTES) {
		goto done;
	}
	size = (size_t)file.size;
	descriptionsText = malloc(size);
	if(descriptionsText == NULL ||
		device->seekFile(&file, 0, DEVICE_HANDLER_SEEK_SET) != 0 ||
		device->readFile(&file, descriptionsText, (u32)size) != (s32)size) {
		goto done;
	}
	for(i = 0u; i < size; ++i) {
		games += descriptionsText[i] == '\n';
	}
	if(games > UI_ABOUT_MAX_GAMES) {
		games = UI_ABOUT_MAX_GAMES;
	}
	descriptionsIndex = malloc(games * sizeof(*descriptionsIndex));
	if(descriptionsIndex != NULL) {
		UIAbout_Index(&descriptions, descriptionsText, size,
			descriptionsIndex, games);
	}
done:
	if(device->closeFile != NULL) {
		device->closeFile(&file);
	}
}

bool DrawGameflowDescription(DEVICEHANDLER_INTERFACE *device,
	const char *gameId, char *out, size_t capacity)
{
	if(out != NULL && capacity > 0u) {
		out[0] = '\0';
	}
	if(device == NULL || device->initial == NULL) {
		return false;
	}
	if(!descriptionsAttempted || descriptionsDevice != device) {
		_GameflowDropDescriptions();
		descriptionsAttempted = true;
		descriptionsDevice = device;
		_GameflowLoadDescriptions(device);
	}
	return UIAbout_Find(&descriptions, gameId, out, capacity);
}

void DrawGameflowCancelPosters(void)
{
	/* The descriptions are the menu thread's: this can run from the reset
	 * callback, so it only marks them stale, and the next lookup reads the
	 * file again. */
	descriptionsAttempted = false;
	Manual_Forget();
	if(!posterPackAttempted && !posterPackFileOwned &&
		!stillsPackFileOwned && !stillsPackAttempted &&
		!discsPackFileOwned && !discsPackAttempted) {
		return;
	}

	/* Unpublish the packs before closing their sources. Video readers use
	 * the same private mutex through ui_assets' callback contract. */
	UIAssets_CancelForDeviceChange();
	UIStills_CancelForDeviceChange();
	UIDiscs_CancelForDeviceChange();
	_GameflowClosePosterPackFile();
	_GameflowCloseStillsPackFile();
	_GameflowCloseDiscsPackFile();
	posterPackDevice = NULL;
	posterPackAttempted = false;
	stillsPackAttempted = false;
	discsPackAttempted = false;
}

static s32 _GameflowOnReset(s32 final)
{
	if(!final) {
		/* Reset callbacks run in ascending priority. Retire the UI-owned pack
		 * while its source and video mutex are still live; device teardown is
		 * deliberately registered one priority later. */
		DrawGameflowCancelPosters();
	}
	return TRUE;
}

static bool _GameflowOpenPosterPack(DEVICEHANDLER_INTERFACE *device)
{
	uiAssetsSource_t source;
	uiAssetsSync_t sync;
	s32 result;

	if(device == NULL || device->initial == NULL ||
		_videomutex == LWP_MUTEX_NULL) {
		return false;
	}
	if(posterPackAttempted && posterPackDevice == device) {
		return UIAssets_Ready();
	}
	if(posterPackAttempted || posterPackFileOwned) {
		DrawGameflowCancelPosters();
	}

	/* Latch one attempt per mounted device. A missing/corrupt private pack
	 * must not turn an idle game-library frame into repeated device I/O. */
	posterPackDevice = device;
	posterPackAttempted = true;
	memset(&posterPackFile, 0, sizeof(posterPackFile));
	concat_path(posterPackFile.name, device->initial->name,
		"swiss/ui/posters.pak");
	posterPackFile.device = device;
	posterPackFileOwned = true;

	UIAssets_SyncFromMutex(_videomutex, &sync);
	result = UIAssets_SourceFromFileHandle(&posterPackFile, &source);
	if(result == UI_ASSETS_OK) {
		result = UIAssets_Init(&source, &sync);
	}
	if(result != UI_ASSETS_OK) {
		_GameflowClosePosterPackFile();
	}

	return result == UI_ASSETS_OK;
}

/* Only Spotlight needs gameplay stills. Keep one attempt per mounted
 * device, independently of a missing poster pack; a later layout change
 * must still be able to open them. Cancel retires both caches together. */
static bool _GameflowOpenStillsPack(DEVICEHANDLER_INTERFACE *device)
{
	uiAssetsSource_t source;
	uiAssetsSync_t sync;

	if(device == NULL || device->initial == NULL ||
		_videomutex == LWP_MUTEX_NULL) {
		return false;
	}
	if(stillsPackAttempted) {
		return UIStills_Ready();
	}
	stillsPackAttempted = true;
	UIAssets_SyncFromMutex(_videomutex, &sync);
	memset(&stillsPackFile, 0, sizeof(stillsPackFile));
	concat_path(stillsPackFile.name, device->initial->name,
		"swiss/ui/stills.pak");
	stillsPackFile.device = device;
	stillsPackFileOwned = true;
	if(UIAssets_SourceFromFileHandle(&stillsPackFile, &source) !=
		UI_ASSETS_OK || UIStills_Init(&source, &sync) != UI_ASSETS_OK) {
		_GameflowCloseStillsPackFile();
	}
	return UIStills_Ready();
}

/* Only the Jewel Case needs disc art: opened as the stills are, on its
 * first use, once per mounted device. */
static bool _GameflowOpenDiscsPack(DEVICEHANDLER_INTERFACE *device)
{
	uiAssetsSource_t source;
	uiAssetsSync_t sync;

	if(device == NULL || device->initial == NULL ||
		_videomutex == LWP_MUTEX_NULL) {
		return false;
	}
	if(discsPackAttempted) {
		return UIDiscs_Ready();
	}
	discsPackAttempted = true;
	UIAssets_SyncFromMutex(_videomutex, &sync);
	memset(&discsPackFile, 0, sizeof(discsPackFile));
	concat_path(discsPackFile.name, device->initial->name,
		"swiss/ui/discs.pak");
	discsPackFile.device = device;
	discsPackFileOwned = true;
	if(UIAssets_SourceFromFileHandle(&discsPackFile, &source) !=
		UI_ASSETS_OK || UIDiscs_Init(&source, &sync) != UI_ASSETS_OK) {
		_GameflowCloseDiscsPackFile();
	}
	return UIDiscs_Ready();
}

static u32 gameflowArtworkLayout; /* menu-thread scheduling only */

void DrawGameflowRequestPosters(DEVICEHANDLER_INTERFACE *device,
	const uiGameflowRenderSnapshot_t *snapshot)
{
	char ids[UI_ASSETS_SLOTS][8] = {{0}};
	u32 i;

	if(!_GameflowSnapshotValid(snapshot)) {
		return;
	}
	gameflowArtworkLayout = snapshot->layout;
	_GameflowOpenPosterPack(device);
	if(snapshot->layout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT &&
		_GameflowOpenStillsPack(device)) {
		/* The selected game's still and the ones either side of it. */
		char stills[UI_STILLS_SLOTS][8] = {{0}};
		for(i = 0u; i < snapshot->recordCount; ++i) {
			const uiGameflowCardSnapshot_t *record = &snapshot->records[i];
			int position = (int)record->relativeSlot + 1;
			if(position < 0 || position >= UI_STILLS_SLOTS ||
				strnlen(record->gameId, sizeof(record->gameId)) !=
					UI_ASSETS_ID_LEN) {
				continue;
			}
			memcpy(stills[position], record->gameId, UI_ASSETS_ID_LEN);
		}
		UIStills_RequestWindow(stills, UI_STILLS_SLOTS, 1);
	}
	if(snapshot->layout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE &&
		_GameflowOpenDiscsPack(device)) {
		/* The selected game's disc and the ones either side of it. */
		char discs[UI_DISCS_SLOTS][8] = {{0}};
		for(i = 0u; i < snapshot->recordCount; ++i) {
			const uiGameflowCardSnapshot_t *record = &snapshot->records[i];
			int position = (int)record->relativeSlot + 1;
			if(position < 0 || position >= UI_DISCS_SLOTS ||
				strnlen(record->gameId, sizeof(record->gameId)) !=
					UI_ASSETS_ID_LEN) {
				continue;
			}
			memcpy(discs[position], record->gameId, UI_ASSETS_ID_LEN);
		}
		UIDiscs_RequestWindow(discs, UI_DISCS_SLOTS, 1);
	}
	if(!UIAssets_Ready()) {
		return;
	}
	if(snapshot->layout == UI_GAMEFLOW_LAYOUT_GRID) {
		/* The grid window is already nearest first: the focused row from
		 * the highlight outwards, then the rows around it. */
		for(i = 0u; i < snapshot->recordCount; ++i) {
			memcpy(ids[i], snapshot->records[i].gameId, sizeof(ids[i]));
			ids[i][UI_ASSETS_ID_LEN] = '\0';
		}
		UIAssets_RequestWindow(ids, (int)snapshot->recordCount, 0);
		return;
	}
	for(i = 0u; i < snapshot->recordCount; ++i) {
		const uiGameflowCardSnapshot_t *record = &snapshot->records[i];
		int position = (int)record->relativeSlot + 4;
		if(position < 0 || position >= UI_ASSETS_WINDOW ||
			strnlen(record->gameId, sizeof(record->gameId)) !=
				UI_ASSETS_ID_LEN) {
			continue;
		}
		memcpy(ids[position], record->gameId, UI_ASSETS_ID_LEN);
	}
	/* Fixed -4..+4 placement preserves true carousel distance even when
	 * the parent entry has no poster ID or a small library has gaps. */
	UIAssets_RequestWindow(ids, UI_ASSETS_WINDOW, 4);
}

bool DrawGameflowPrimePoster(void)
{
	/* One fixed-size poster (43 KiB), before the first Detail is published.
	 * Never drain neighbours or gameplay stills at this entry boundary. */
	return UIAssets_Poll();
}

bool DrawGameflowPollPosters(void)
{
	/* One 16 KiB range total between input checks; pause hidden packs. */
	return UIAssets_PollVisible(
		gameflowArtworkLayout == UI_GAMEFLOW_LAYOUT_SPOTLIGHT,
		gameflowArtworkLayout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE);
}

static void _GameflowCopySnapshot(drawGameflowEvent_t *data,
	const uiGameflowRenderSnapshot_t *snapshot,
	const drawGameflowSnapshotPresentation_t *presentation)
{
	u32 i;

	/* The header and the records the window filled, nothing after them. */
	memcpy(&data->snapshot, snapshot,
		offsetof(uiGameflowRenderSnapshot_t, records) +
		snapshot->recordCount * sizeof(snapshot->records[0]));
	for(i = 0u; i < UI_GAMEFLOW_RENDER_SLOTS; ++i) {
		uiGameflowCardSnapshot_t *record = &data->snapshot.records[i];
		data->cardPresentation[i] = presentation->cards[i].sizes;
		memset(&data->bannerTexObj[i], 0, sizeof(data->bannerTexObj[i]));
		if(i >= data->snapshot.recordCount) {
			continue;
		}
		_GameflowApplyCardText(record->title, sizeof(record->title),
			presentation->cards[i].keep[0]);
		_GameflowApplyCardText(record->company, sizeof(record->company),
			presentation->cards[i].keep[1]);
		_GameflowApplyCardText(record->facts, sizeof(record->facts),
			presentation->cards[i].keep[2]);
		if(!(record->flags & UI_GAMEFLOW_CARD_HAS_BANNER)) {
			continue;
		}
		DCFlushRange(record->banner, BNR_PIXELDATA_LEN);
		GX_InitTexObj(&data->bannerTexObj[i], record->banner, 96, 32,
			GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GX_InitTexObjFilterMode(&data->bannerTexObj[i], GX_LINEAR, GX_NEAR);
	}
	memcpy(data->spotlightLines, presentation->spotlightLines, sizeof(data->spotlightLines));
}

uiDrawObj_t* DrawGameflow(const uiGameflowRenderSnapshot_t *snapshot)
{
	drawGameflowEvent_t *eventData;
	drawGameflowSnapshotPresentation_t presentation;
	uiDrawObj_t *event;

	if(!_GameflowSnapshotValid(snapshot)) {
		return NULL;
	}
	eventData = memalign(32, sizeof(drawGameflowEvent_t));
	event = calloc(1, sizeof(uiDrawObj_t));
	if(eventData == NULL || event == NULL) {
		free(eventData);
		free(event);
		return NULL;
	}
	memset(eventData, 0, sizeof(*eventData));
	UIGameflow_Init(&eventData->state);
	UIGameflow_SetColumns(&eventData->state, snapshot->columns);
	if(snapshot->layout == UI_GAMEFLOW_LAYOUT_JEWEL_CASE) {
		/* The case's light and meshes, the first time it is shown. */
		UIJewelCase_Prepare();
		eventData->state.carouselSpring.response =
			GAMEFLOW_JEWEL_CASE_RESPONSE;
	}
	if(!UIGameflow_ApplySnapshot(&eventData->state, &snapshot->selection,
		_CurrentMotionMode())) {
		free(eventData);
		free(event);
		return NULL;
	}
	_GameflowPrepareSnapshot(snapshot, &presentation);
	_GameflowCopySnapshot(eventData, snapshot, &presentation);
	event->type = EV_GAMEFLOW;
	event->data = eventData;
	return event;
}

bool DrawUpdateGameflow(uiDrawObj_t *evt,
	const uiGameflowRenderSnapshot_t *snapshot)
{
	drawGameflowSnapshotPresentation_t presentation;
	bool updated = false;

	if(evt == NULL || !_GameflowSnapshotValid(snapshot)) {
		return false;
	}
	/* Font measurement and description wrapping read only the caller's
	 * immutable snapshot. Publish only the ready result under the lock. */
	_GameflowPrepareSnapshot(snapshot, &presentation);
	LWP_MutexLock(_videomutex);
	if(!evt->disposed && evt->type == EV_GAMEFLOW && evt->data != NULL) {
		drawGameflowEvent_t *data = (drawGameflowEvent_t*)evt->data;
		/* An event keeps its layout; a new one needs a new event. */
		if(data->snapshot.layout == snapshot->layout &&
			data->snapshot.columns == snapshot->columns &&
			UIGameflow_ApplySnapshot(&data->state, &snapshot->selection,
			_CurrentMotionMode())) {
			_GameflowCopySnapshot(data, snapshot, &presentation);
			updated = true;
		}
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

static bool _GameflowSetMode(uiDrawObj_t *evt, uiGameflowMode_t mode,
	uiMotionMode_t motion)
{
	bool updated = false;

	if(evt == NULL) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	if(!evt->disposed && evt->type == EV_GAMEFLOW && evt->data != NULL) {
		drawGameflowEvent_t *data = (drawGameflowEvent_t*)evt->data;
		UIGameflow_SetMode(&data->state, mode, motion);
		_GameflowSetLaunch(data, UIGameflow_Frame(&data->state)->mode ==
			UI_GAMEFLOW_MODE_LAUNCH);
		updated = true;
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

bool DrawSetGameflowMode(uiDrawObj_t *evt, uiGameflowMode_t mode)
{
	return _GameflowSetMode(evt, mode, _CurrentMotionMode());
}

bool DrawSetGameflowModeNow(uiDrawObj_t *evt, uiGameflowMode_t mode)
{
	return _GameflowSetMode(evt, mode, UI_MOTION_OFF);
}

bool DrawSetGameflowDetailFocus(uiDrawObj_t *evt,
	uiGameflowDetailFocus_t focus)
{
	bool updated = false;

	if(evt == NULL || focus > UI_GAMEFLOW_DETAIL_FOCUS_MANUAL) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	if(!evt->disposed && evt->type == EV_GAMEFLOW && evt->data != NULL) {
		((drawGameflowEvent_t*)evt->data)->detailFocus = focus;
		updated = true;
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

bool DrawLaunchStep(const char *message)
{
	/* Only this thread turns a launch on or off. */
	if(!launchActive) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	_GameflowLaunchMessage(message);
	LWP_MutexUnlock(_videomutex);
	return true;
}

/* The hand-off: DrawShutdown fades a launch to black over this long. */
#define GAMEFLOW_LAUNCH_FADE_SECONDS 0.25f
static bool launchFading;
static float launchFade;

static void _DrawLaunchFade(void)
{
	GXColor black = {0, 0, 0, 0};

	launchFade = fminf(1.0f,
		launchFade + UIAnim_Delta() / GAMEFLOW_LAUNCH_FADE_SECONDS);
	black.a = (u8)(255.0f * launchFade + 0.5f);
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		_putFlatRect(UIStage_Left(), 0.0f, UIStage_Right() - UIStage_Left(),
			480.0f, black);
	GX_End();
	drawInit();
}

bool DrawUpdateGameflowDetail(uiDrawObj_t *evt,
	const uiGameflowDetailSnapshot_t *snapshot)
{
	bool updated = false;

	if(evt == NULL || snapshot == NULL ||
		(snapshot->flags & UI_GAMEFLOW_DETAIL_VALID) == 0u) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	if(!evt->disposed && evt->type == EV_GAMEFLOW && evt->data != NULL) {
		drawGameflowEvent_t *data = (drawGameflowEvent_t*)evt->data;
		const uiGameflowFrame_t *frame = UIGameflow_Frame(&data->state);
		const uiGameflowCardSnapshot_t *record = frame != NULL ?
			_GameflowFindRecord(&data->snapshot, frame->focusIndex, NULL) : NULL;

		if(frame != NULL && record != NULL &&
			UIGameflowDetail_Matches(snapshot, frame->generation,
				frame->focusIndex, record->gameId,
				strnlen(record->gameId, sizeof(record->gameId)))) {
			memcpy(&data->detail, snapshot, sizeof(data->detail));
			_GameflowPrepareDetailPresentation(data);
			memset(&data->detailBannerTexObj, 0,
				sizeof(data->detailBannerTexObj));
			if((data->detail.flags & UI_GAMEFLOW_DETAIL_HAS_BANNER) != 0u) {
				DCFlushRange(data->detail.banner,
					UI_GAMEFLOW_DETAIL_BANNER_BYTES);
				GX_InitTexObj(&data->detailBannerTexObj, data->detail.banner,
					96, 32, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
				GX_InitTexObjFilterMode(&data->detailBannerTexObj,
					GX_LINEAR, GX_NEAR);
			}
			updated = true;
		}
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

void DrawClearGameflowDetail(uiDrawObj_t *evt)
{
	if(evt == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(!evt->disposed && evt->type == EV_GAMEFLOW && evt->data != NULL) {
		drawGameflowEvent_t *data = (drawGameflowEvent_t*)evt->data;
		memset(&data->detail, 0, sizeof(data->detail));
		memset(&data->detailPresentation, 0,
			sizeof(data->detailPresentation));
		memset(&data->detailBannerTexObj, 0,
			sizeof(data->detailBannerTexObj));
	}
	LWP_MutexUnlock(_videomutex);
}

// Internal
static void _DrawVertScrollBar(uiDrawObj_t *evt) {
	drawVertScrollbarEvent_t *data = (drawVertScrollbarEvent_t*)evt->data;
	int x1 = data->x;
	int x2 = data->x+data->width;
	int y1 = data->y;
	int y2 = data->y+data->height;
	int scrollStartY = y1+3 + (int)((data->height-6-data->scrollHeight)*data->scrollPercent);

	if(scrollStartY > y2-3-data->scrollHeight)
		scrollStartY = y2-3-data->scrollHeight;
	
	GXColor fillColor = (GXColor) {46,57,104,GUI_MSGBOX_ALPHA}; 	//bluish
  	GXColor noColor = (GXColor) {0,0,0,0}; //blank
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	_DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, noColor, borderColor);
	
	_DrawSimpleBox( x1, scrollStartY,
			data->width, data->scrollHeight, 0, fillColor, borderColor); 
}

// External
uiDrawObj_t* DrawVertScrollBar(int x, int y, int width, int height, float scrollPercent, int scrollHeight) {
	scrollHeight = scrollHeight < 10 ? 10:scrollHeight;
	drawVertScrollbarEvent_t *eventData = calloc(1, sizeof(drawVertScrollbarEvent_t));
	eventData->x = x;
	eventData->y = y;
	eventData->width = width;
	eventData->height = height;
	eventData->scrollPercent = scrollPercent;
	eventData->scrollHeight = scrollHeight;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_VERTSCROLLBAR;
	event->data = eventData;
	return event;
}

static uiDrawObj_t* drawParameterForArgsSelector(Parameter *param, int x, int y, int selected) {

	uiDrawObj_t* container = DrawContainer();
	char *name = &param->arg.name[0];
	char *selValue = &param->values[param->currentValueIdx].name[0];
	
	int chkWidth = 32, nameWidth = 300, gapWidth = 13, paramWidth = 120;
	// [32px 10px 250px 10px 5px 80px 5px]
	// If not selected and not enabled, use greyed out font for everything
	GXColor fontColor = (param->enable || selected) ? defaultColor : deSelectedColor;

	// If selected draw that it's selected
	if(selected) DrawAddChild(container, DrawTransparentBox( x+chkWidth+gapWidth, y, getVideoMode()->fbWidth-52, y+30));
	DrawAddChild(container, DrawImage(param->enable ? TEX_CHECKED:TEX_UNCHECKED, x, y, 32, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));
	// Draw the parameter Name
	DrawAddChild(container, DrawStyledLabel(x+chkWidth+gapWidth+5, y+15, name, GetTextScaleToFitInWidth(name, nameWidth-10), ALIGN_LEFT, fontColor));
	// If enabled, draw arrows indicating where in the param list we are
	if(selected && param->enable && param->num_values > 1) {
		if(param->currentValueIdx != 0) {
			DrawAddChild(container, DrawStyledLabel(x+(chkWidth+nameWidth+(gapWidth*4)), y+15, "\213", .8f, ALIGN_LEFT, defaultColor));
		}
		if(param->currentValueIdx != param->num_values-1) {
			DrawAddChild(container, DrawStyledLabel(x+(chkWidth+nameWidth+paramWidth+(gapWidth*7)), y+15, "\233", .8f, ALIGN_LEFT, defaultColor));
		}
	}
	// Draw the current value
	DrawAddChild(container, DrawStyledLabel(x+chkWidth+nameWidth+(gapWidth*6), y+15, selValue, GetTextScaleToFitInWidth(selValue, paramWidth), ALIGN_LEFT, fontColor));
	return container;
}

// External
void DrawArgsSelector(const char *fileName) {
	Parameters* params = getParameters();
	int param_selection = 0;
	int params_per_page = 6;
	
	uiDrawObj_t *container = NULL;
	while (padsButtonsHeld() & BUTTON_A){ VIDEO_WaitVSync (); }
	while(1) {
		uiDrawObj_t *newPanel = DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 460);
		sprintf(txtbuffer, "%s Parameters:", fileName);
		DrawAddChild(newPanel, DrawStyledLabel(25, 74, txtbuffer, GetTextScaleToFitInWidth(txtbuffer, getVideoMode()->fbWidth-50), ALIGN_LEFT, defaultColor));

		int i = 0, j = 0;
		int current_view_start = MIN(MAX(0,param_selection-params_per_page/2),MAX(0,params->num_params-params_per_page));
		int current_view_end = MIN(params->num_params, MAX(param_selection+params_per_page/2,params_per_page));
	
		int scrollBarHeight = 90+(params_per_page*20);
		int scrollBarTabHeight = (int)((float)scrollBarHeight/(float)params->num_params);
		DrawAddChild(newPanel, DrawVertScrollBar(getVideoMode()->fbWidth-45, 120, 25, scrollBarHeight, (float)((float)param_selection/(float)(params->num_params-1)),scrollBarTabHeight));
		for(i = current_view_start,j = 0; i<current_view_end; ++i,++j) {
			DrawAddChild(newPanel, drawParameterForArgsSelector(&params->parameters[i], 25, 120+j*35, i==param_selection));
		}
		// Write about the default if there is any
		DrawAddChild(newPanel, DrawTransparentBox( 35, 350, getVideoMode()->fbWidth-35, 400));
		DrawAddChild(newPanel, DrawStyledLabel(33, 354, "Default values will be used by the DOL being loaded if a", 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawStyledLabel(33, 374, "parameter is not enabled. Please check the documentation", 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawStyledLabel(33, 394, "for this DOL if you are unsure of the default values.", 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawHintLabel(640/2, 440, "A  Toggle Param    START  Load the DOL", 1.0f, ALIGN_CENTER, defaultColor));
		
		container = DrawRepublish(container, newPanel);
		
		while (!(padsButtonsHeld() & (BUTTON_RIGHT|BUTTON_LEFT|BUTTON_UP|BUTTON_DOWN|BUTTON_START|BUTTON_A)))
			{ VIDEO_WaitVSync (); }
		u32 btns = padsButtonsHeld();
		if((btns & (BUTTON_RIGHT|BUTTON_LEFT)) && params->parameters[param_selection].enable) {
			int curValIdx = params->parameters[param_selection].currentValueIdx;
			int maxValIdx = params->parameters[param_selection].num_values;
			curValIdx = btns & BUTTON_LEFT ? 
				((--curValIdx < 0) ? maxValIdx-1 : curValIdx):((curValIdx + 1) % maxValIdx);
			params->parameters[param_selection].currentValueIdx = curValIdx;
		}
		if(btns & (BUTTON_UP|BUTTON_DOWN)) {
			param_selection = btns & BUTTON_UP ? 
				((--param_selection < 0) ? params->num_params-1 : param_selection)
				:((param_selection + 1) % params->num_params);
		}
		if(btns & BUTTON_A) {
			params->parameters[param_selection].enable ^= 1;
		}
		if(btns & BUTTON_START) {
			break;
		}
		while (padsButtonsHeld() & (BUTTON_RIGHT|BUTTON_LEFT|BUTTON_UP|BUTTON_DOWN|BUTTON_START|BUTTON_A))
			{ VIDEO_WaitVSync (); }
	}
	DrawDispose(container);
}

/* Retained cheat screen: menu thread prepares text and selection snapshots;
 * the video thread only draws and advances the focus spring. */
typedef struct {
	char title[UI_CHEATS_TEXT_CAPACITY];
	char rows[UI_CHEATS_VISIBLE_ROWS][UI_CHEATS_TEXT_CAPACITY];
	char selectedName[3][UI_CHEATS_TEXT_CAPACITY];
	char enabledText[32];
	char positionText[32];
	char memoryText[64];
	char warning[96];
	bool enabled[UI_CHEATS_VISIBLE_ROWS];
	bool enabledOnly;
	bool advanced;
	bool debug;
	int count;
	int first;
	int rowCount;
	int focusRow;
	int memoryWidth;
} uiCheatsSnapshot_t;

typedef struct {
	uiCheatsSnapshot_t snapshot;
	uiMotionSpring_t focusY;
	bool focusInitialized;
	bool lastAdvanced;
} drawCheatsEvent_t;

static bool _CheatsEnabled(int index, const void *context)
{
	const CheatEntries *cheats = context;
	return cheats->cheat[index].enabled != 0;
}

static void _CheatsSnapshot(uiCheatsSnapshot_t *out, const char *fileName,
	const CheatEntries *cheats, int selection, bool enabledOnly, bool advanced,
	const char *warning)
{
	char text[UI_CHEATS_SOURCE_LIMIT + 1u];
	int i;
	int selectedIndex;
	int used = getEnabledCheatsSize();
	int capacity = kenobi_get_maxsize();
	memset(out, 0, sizeof(*out));
	UICheats_GameTitle(text, sizeof(text), fileName);
	UICheats_Fit(out->title, sizeof(out->title), text[0] ? text : "Game",
		548, 0.64f, GetTextSizeInPixels);
	out->enabledOnly = enabledOnly;
	out->advanced = advanced;
	out->debug = swissSettings.wiirdDebug != 0;
	out->count = UICheats_Count(cheats->num_cheats, enabledOnly,
		_CheatsEnabled, cheats);
	out->first = UICheats_WindowStart(selection, out->count);
	out->focusRow = selection - out->first;
	(void)snprintf(out->enabledText, sizeof(out->enabledText), "%d enabled",
		getEnabledCheatsCount());
	if(out->count > 0) {
		(void)snprintf(out->positionText, sizeof(out->positionText), "%d / %d",
			selection + 1, out->count);
	}
	(void)snprintf(out->memoryText, sizeof(out->memoryText),
		"Cheat memory   %d of %d bytes", used, capacity);
	if(capacity > 0 && used > 0) {
		double fraction = (double)used / (double)capacity;
		out->memoryWidth = fraction >= 1.0 ? 512 : (int)(fraction * 512.0);
	}
	UICheats_Fit(out->warning, sizeof(out->warning), warning,
		548, 0.48f, GetTextSizeInPixels);
	for(i = 0; i < UI_CHEATS_VISIBLE_ROWS; ++i) {
		int index = UICheats_Index(cheats->num_cheats, enabledOnly,
			out->first + i, _CheatsEnabled, cheats);
		if(index < 0) break;
		UICheats_Name(text, sizeof(text), cheats->cheat[index].name);
		UICheats_Fit(out->rows[i], sizeof(out->rows[i]),
			text[0] ? text : "Unnamed cheat", 454, 0.66f, GetTextSizeInPixels);
		out->enabled[i] = cheats->cheat[index].enabled != 0;
		++out->rowCount;
	}
	selectedIndex = UICheats_Index(cheats->num_cheats, enabledOnly,
		selection, _CheatsEnabled, cheats);
	UICheats_Wrap(out->selectedName,
		selectedIndex >= 0 ? cheats->cheat[selectedIndex].name : "No cheat selected",
		520, 0.58f, GetTextSizeInPixels);
}

/* Flat, lightly rounded geometry; no legacy bevel/shadow or backdrop leak. */
static void _CheatsPanel(int x, int y, int width, int height, GXColor color)
{
	const int radius = height > 8 && width > 12 ? 6 : 0;
	const int px[9] = {x + radius, x + width - radius, x + width,
		x + width, x + width - radius, x + radius, x, x, x + radius};
	const int py[9] = {y, y, y + radius, y + height - radius, y + height,
		y + height, y + height - radius, y + radius, y};
	int i;
	UIColor_Apply(&color.r, &color.g, &color.b);
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 10);
		GX_Position3f32((float)x + (float)width * 0.5f,
			(float)y + (float)height * 0.5f, 0.0f);
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(0.0f, 0.0f);
		for(i = 0; i < 9; ++i) {
			GX_Position3f32((float)px[i], (float)py[i], 0.0f);
			GX_Color4u8(color.r, color.g, color.b, color.a);
			GX_TexCoord2f32(0.0f, 0.0f);
		}
	GX_End();
	drawInit();
}

/* A page that covers the screen, and in widescreen the margins beside it. */
static void _PagePanel(int x, int y, int width, int height, GXColor color)
{
	int left = (int)floorf(UIStage_Left());
	int right = (int)ceilf(UIStage_Right());

	_CheatsPanel(x + left, y, width + (right - 640) - left, height, color);
}

/* Message and progress boxes, in the Settings pages' card language: a flat
 * rounded card with a hairline edge and, for a message, an accent bar that
 * says what kind it is (a warning's amber, a failure's red, otherwise
 * Indigo's lilac). Their place and size are Swiss's own, so every caller's
 * text and prompt lines land where they always did. */
static void _DrawDialogCard(int x, int y, int width, int height, int type)
{
	_CheatsPanel(x - 1, y - 1, width + 2, height + 2, (GXColor) {43, 52, 80, 255});
	_CheatsPanel(x, y, width, height, (GXColor) {16, 23, 43, 250});
	if(type >= 0) {
		_CheatsPanel(x + 20, y + 14, 40, 3, type == D_WARN ?
			(GXColor) {255, 207, 139, 255} : type == D_FAIL ?
			(GXColor) {243, 126, 145, 255} : (GXColor) {196, 177, 255, 255});
	}
}

/* A progress box's bar: its track, filled from start for length. */
static void _DrawDialogBar(int x, int y, int width, int start, int length)
{
	_CheatsPanel(x, y, width, 20, (GXColor) {34, 42, 65, 255});
	if(length > 0) {
		_CheatsPanel(x + start, y, length, 20, (GXColor) {196, 177, 255, 255});
	}
}

/* alpha dims a toggle Settings can't change. */
static void _CheatsToggle(int x, int y, bool enabled, u8 alpha)
{
	_CheatsPanel(x, y, 62, 24, _HintAlpha(enabled ? (GXColor){26, 93, 103, 255} :
		(GXColor){34, 42, 65, 255}, alpha));
	drawStringMedium(x + 31, y + 12, enabled ? "ON" : "OFF", 0.60f,
		ALIGN_CENTER, _HintAlpha(enabled ? (GXColor){151, 250, 246, 255} :
		(GXColor){184, 192, 215, 255}, alpha));
}

static void _DrawCheats(uiDrawObj_t *evt)
{
	drawCheatsEvent_t *data = evt->data;
	const uiCheatsSnapshot_t *s = &data->snapshot;
	const GXColor primary = {241, 244, 255, 255};
	const GXColor secondary = {173, 187, 216, 255};
	const GXColor accent = {115, 234, 234, 255};
	uiMotionMode_t motion = _CurrentMotionMode();
	float target = s->advanced ? 264.0f : (float)(148 + s->focusRow * 40);
	int focusY;
	int i;
	if(!data->focusInitialized || data->lastAdvanced != s->advanced) {
		UIMotion_SpringInit(&data->focusY, target, 25.0f);
		data->focusInitialized = true;
	}
	data->lastAdvanced = s->advanced;
	UIMotion_SpringRetarget(&data->focusY, target, motion);
	focusY = (int)lrintf(UIMotion_SpringUpdate(&data->focusY,
		UIAnim_Delta(), motion));

	_PagePanel(-6, -6, 652, 492, (GXColor){8, 12, 27, 255});
	_CheatsPanel(40, 30, 40, 3, accent);
	drawStringMedium(40, 63, "Cheats", 1.05f, ALIGN_LEFT, primary);
	drawStringMedium(600, 63, s->enabledText, 0.54f, ALIGN_RIGHT, accent);
	drawStringMedium(40, 98, s->title, 0.64f, ALIGN_LEFT, secondary);
	_CheatsPanel(40, 115, 560, 1, (GXColor){43, 52, 80, 255});

	if(s->advanced) {
		drawStringMedium(40, 132, "ADVANCED", 0.42f, ALIGN_LEFT, accent);
		drawStringMedium(52, 166, "Selected cheat", 0.48f, ALIGN_LEFT, secondary);
		for(i = 0; i < 3; ++i) {
			drawStringMedium(52, 192 + i * 21, s->selectedName[i], 0.58f,
				ALIGN_LEFT, primary);
		}
		_CheatsPanel(40, focusY, 560, 44, (GXColor){38, 37, 78, 255});
		_CheatsPanel(40, focusY + 6, 3, 32, accent);
		drawStringMedium(52, 286, "WiiRD Debug", 0.66f, ALIGN_LEFT, primary);
		_CheatsToggle(526, 274, s->debug, 255);
		drawStringMedium(52, 327, "For compatible debugging tools.", 0.48f,
			ALIGN_LEFT, secondary);
		drawStringMedium(52, 356, s->memoryText, 0.54f, ALIGN_LEFT, secondary);
		_CheatsPanel(52, 376, 512, 4, (GXColor){34, 42, 65, 255});
		if(s->memoryWidth > 0) _CheatsPanel(52, 376, s->memoryWidth, 4, accent);
	}
	else {
		drawStringMedium(40, 132, s->enabledOnly ? "ENABLED ONLY" : "ALL CHEATS",
			0.42f, ALIGN_LEFT, accent);
		drawStringMedium(600, 132, s->positionText, 0.46f, ALIGN_RIGHT, secondary);
		for(i = 0; i < s->rowCount; ++i) {
			_CheatsPanel(40, 148 + i * 40, 560, 34, (GXColor){16, 23, 43, 255});
		}
		if(s->rowCount > 0) {
			_CheatsPanel(40, focusY, 560, 34, (GXColor){38, 37, 78, 255});
			_CheatsPanel(40, focusY + 4, 3, 26, accent);
		}
		for(i = 0; i < s->rowCount; ++i) {
			drawStringMedium(52, 165 + i * 40, s->rows[i], 0.66f,
				ALIGN_LEFT, primary);
			_CheatsToggle(526, 153 + i * 40, s->enabled[i], 255);
		}
		if(s->count > UI_CHEATS_VISIBLE_ROWS) {
			int thumb = 234 * UI_CHEATS_VISIBLE_ROWS / s->count;
			int travel;
			if(thumb < 12) thumb = 12;
			travel = (int)((int64_t)(234 - thumb) * s->first /
				(s->count - UI_CHEATS_VISIBLE_ROWS));
			_CheatsPanel(608, 148, 2, 234, (GXColor){34, 42, 65, 255});
			_CheatsPanel(608, 148 + travel, 2, thumb, accent);
		}
		if(s->rowCount == 0) {
			drawStringMedium(320, 236, s->enabledOnly ?
				"No cheats enabled" : "No cheats available", 0.76f,
				ALIGN_CENTER, primary);
			_DrawHintText(320, 269, s->enabledOnly ?
				"X  Browse all cheats" : "B  Return to your game",
				0.54f, ALIGN_CENTER, secondary);
		}
	}
	if(s->warning[0]) drawStringMedium(40, 400, s->warning, 0.48f,
		ALIGN_LEFT, (GXColor){255, 207, 139, 255});
	_CheatsPanel(40, 413, 560, 1, (GXColor){43, 52, 80, 255});
	if(s->advanced) {
		_DrawHintText(40, 435, "A  Toggle debug", 0.48f, ALIGN_LEFT, primary);
		_DrawHintText(600, 435, "B  Back", 0.48f, ALIGN_RIGHT, secondary);
	}
	else {
		_DrawHintText(40, 435, "A  Toggle", 0.48f, ALIGN_LEFT, primary);
		_DrawHintText(169, 435, "B  Done", 0.48f, ALIGN_LEFT, secondary);
		_DrawHintText(290, 435, s->enabledOnly ? "X  Show all" : "X  Enabled only",
			0.48f, ALIGN_LEFT, secondary);
		_DrawHintText(600, 435, "Z  Advanced", 0.48f, ALIGN_RIGHT, secondary);
	}
	drawInit();
}

static u32 _CheatsElapsed(u32 *lastRetrace)
{
	u32 retrace = VIDEO_GetRetraceCount();
	u32 count = retrace - *lastRetrace;
	float rate = VIDEO_GetRetraceRate();
	float elapsed;
	*lastRetrace = retrace;
	if(!isfinite(rate) || rate < 1.0f) rate = 60.0f;
	elapsed = (float)count * 1000000.0f / rate;
	return elapsed >= (float)UI_MENU_INPUT_MAX_ELAPSED_US ?
		UI_MENU_INPUT_MAX_ELAPSED_US : (u32)elapsed;
}

void DrawCheatsSelector(const char *fileName)
{
	const u32 directions = BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT |
		BUTTON_RIGHT | BUTTON_L | BUTTON_R;
	const u32 buttons = directions | BUTTON_A | BUTTON_B | BUTTON_X | BUTTON_Z;
	CheatEntries *cheats = getCheats();
	uiDrawObj_t *event = calloc(1, sizeof(*event));
	drawCheatsEvent_t *data = calloc(1, sizeof(*data));
	uiMenuInputState_t menuInput;
	int selection = 0;
	bool enabledOnly = false;
	bool advanced = false;
	bool dirty = true;
	const char *warning = "";
	u32 previous = padsButtonsHeld() & buttons;
	u32 lastRetrace = VIDEO_GetRetraceCount();
	u32 repeatHeld = 0u;
	u32 repeatTime = 0u;
	bool repeated = false;
	if(event == NULL || data == NULL) { free(event); free(data); return; }
	UIMenuInput_Init(&menuInput);
	event->type = EV_CHEATS;
	event->data = data;
	_CheatsSnapshot(&data->snapshot, fileName, cheats, selection,
		enabledOnly, advanced, warning);
	DrawPublish(event);
	while(1) {
		u32 held;
		u32 pressed;
		u32 elapsed;
		u32 heldDirection;
		uiMenuInputDirection_t analog;
		int count;
		int index;
		if(dirty) {
			uiCheatsSnapshot_t snapshot;
			_CheatsSnapshot(&snapshot, fileName, cheats, selection,
				enabledOnly, advanced, warning);
			LWP_MutexLock(_videomutex);
			data->snapshot = snapshot;
			LWP_MutexUnlock(_videomutex);
			dirty = false;
		}
		VIDEO_WaitVSync();
		held = padsButtonsHeld() & buttons;
		pressed = held & ~previous;
		previous = held;
		elapsed = _CheatsElapsed(&lastRetrace);
		analog = padsMenuInputPoll(&menuInput, elapsed,
			UI_MENU_INPUT_AXIS_VERTICAL | UI_MENU_INPUT_REPEAT, held != 0u);
		heldDirection = held & directions;
		if(heldDirection != repeatHeld) {
			repeatHeld = heldDirection;
			repeatTime = 0u;
			repeated = false;
		}
		else if(heldDirection != 0u && (held & ~directions) == 0u) {
			repeatTime += elapsed;
			if(repeatTime >= (repeated ? UI_MENU_INPUT_REPEAT_US :
					UI_MENU_INPUT_INITIAL_REPEAT_US)) {
				pressed |= heldDirection;
				repeatTime = 0u;
				repeated = true;
			}
		}
		if(analog == UI_MENU_INPUT_UP) pressed |= BUTTON_UP;
		if(analog == UI_MENU_INPUT_DOWN) pressed |= BUTTON_DOWN;
		if(pressed == 0u) continue;
		count = UICheats_Count(cheats->num_cheats, enabledOnly, _CheatsEnabled, cheats);
		index = UICheats_Index(cheats->num_cheats, enabledOnly, selection,
			_CheatsEnabled, cheats);
		warning = "";
		if(pressed & BUTTON_B) {
			if(!advanced) break;
			advanced = false;
		}
		else if(pressed & BUTTON_Z) advanced = !advanced;
		else if(!advanced && (pressed & BUTTON_X)) {
			enabledOnly = !enabledOnly;
			selection = UICheats_Preserve(cheats->num_cheats, enabledOnly,
				index, _CheatsEnabled, cheats);
		}
		else if(pressed & BUTTON_A) {
			if(advanced) {
				if(swissSettings.wiirdDebug) swissSettings.wiirdDebug = 0;
				else if(cheatsCanEnableDebug()) swissSettings.wiirdDebug = 1;
				else warning = "Turn off a cheat to make room for debugging.";
			}
			else if(index >= 0) {
				cheats->cheat[index].enabled ^= 1;
				if(getEnabledCheatsSize() > kenobi_get_maxsize()) {
					cheats->cheat[index].enabled = 0;
					warning = "Not enough room. Turn off another cheat first.";
				}
				selection = UICheats_Preserve(cheats->num_cheats, enabledOnly,
					index, _CheatsEnabled, cheats);
			}
		}
		else if(!advanced) {
			if(pressed & (BUTTON_UP | BUTTON_LEFT | BUTTON_L))
				selection = UICheats_Move(selection, count, -1,
					(pressed & (BUTTON_LEFT | BUTTON_L)) != 0u);
			else if(pressed & (BUTTON_DOWN | BUTTON_RIGHT | BUTTON_R))
				selection = UICheats_Move(selection, count, 1,
					(pressed & (BUTTON_RIGHT | BUTTON_R)) != 0u);
		}
		dirty = true;
	}
	DrawDispose(event);
}


/* ------------------------------------------------------------------------
 * A game's manual (manual.c): the page, at full width or whole, and a bar
 * under it with the page's number and the buttons. The page's texels are
 * the manual's own: at full width on a 640 x 480 screen, one texel a pixel.
 * --------------------------------------------------------------------- */
typedef struct {
	uiManualView_t view;
	float hintScale;
} drawManualEvent_t;

static void _ManualPage(const uiManualView_t *v)
{
	uiManualPage_t page = {0u, v->pageWidth, v->pageHeight};
	uiManualRect_t r = UIManual_Place(&page, v->whole, v->scroll, v->zoom);
	GXRModeObj *mode = getVideoMode();
	u16 width = v->whole ? UI_MANUAL_WIDTH / 2u : UI_MANUAL_WIDTH;
	u32 rows = v->whole ? v->height / 2u : v->height;	/* the texture's */
	u32 shown = v->whole ? (v->pageHeight + 1u) / 2u : v->pageHeight;
	float across = (float)(v->whole ? (v->pageWidth + 1u) / 2u : v->pageWidth) /
		(float)width;
	const u8 *texels = v->whole ? v->texels + v->pageBytes : v->texels;
	/* One texel a pixel is sharpest point-sampled; anything scaled blends. */
	u8 filter = !v->whole && v->zoom == 1.0f && UIStage_PixelWidth() == 1.0f &&
		mode->fbWidth == 640 && mode->efbHeight == 480 ? GX_NEAR : GX_LINEAR;
	float perRow = r.height / (float)shown;
	u32 top;

	/* Drawn as it is: no tint, no blend (an I4 page's alpha is its gray). */
	GX_SetNumTevStages(1);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GX_InvalidateTexAll();
	/* GX takes 1024 rows of a texture at most: a taller page draws in two. */
	for(top = 0u; top < shown; top += UI_MANUAL_STRIP_ROWS) {
		u32 strip = rows - top < UI_MANUAL_STRIP_ROWS ? rows - top :
			UI_MANUAL_STRIP_ROWS;
		u32 drawn = shown - top < strip ? shown - top : strip;
		float y0 = r.y + (float)top * perRow;
		float y1 = y0 + (float)drawn * perRow;
		float t1 = (float)drawn / (float)strip;
		GXTexObj texture;

		if(y1 <= 0.0f || y0 >= 480.0f) {
			continue;
		}
		/* 4 bits a texel in 8 x 8 tiles: 8 rows are width * 4 bytes. */
		GX_InitTexObj(&texture, (void *)(texels + top * width / 2u), width,
			(u16)strip, v->format, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GX_InitTexObjLOD(&texture, filter, filter, 0.0f, 0.0f, 0.0f,
			GX_FALSE, GX_FALSE, GX_ANISO_1);
		GX_LoadTexObj(&texture, GX_TEXMAP0);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32(r.x, y0, 0.0f);
			GX_Color4u8(255, 255, 255, 255);
			GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32(r.x + r.width, y0, 0.0f);
			GX_Color4u8(255, 255, 255, 255);
			GX_TexCoord2f32(across, 0.0f);
			GX_Position3f32(r.x + r.width, y1, 0.0f);
			GX_Color4u8(255, 255, 255, 255);
			GX_TexCoord2f32(across, t1);
			GX_Position3f32(r.x, y1, 0.0f);
			GX_Color4u8(255, 255, 255, 255);
			GX_TexCoord2f32(0.0f, t1);
		GX_End();
	}
	drawInit();
}

static void _DrawManual(uiDrawObj_t *evt)
{
	const drawManualEvent_t *data = evt->data;
	const uiManualView_t *v = &data->view;
	const int bar = (int)UI_MANUAL_BAR;

	_PagePanel(-6, -6, 652, 492, (GXColor){8, 12, 27, 255});
	if(v->texels != NULL) {
		_ManualPage(v);
	}
	else {
		drawStringMedium(320, 225, v->message, 0.66f, ALIGN_CENTER,
			(GXColor){241, 244, 255, 255});
	}
	_PagePanel(-6, 480 - bar, 652, bar + 6, (GXColor){8, 12, 27, 236});
	drawStringMedium(20, 480 - bar / 2, v->counter, 0.48f, ALIGN_LEFT,
		(GXColor){115, 234, 234, 255});
	_DrawHintText(620, 480 - bar / 2, v->hints, data->hintScale, ALIGN_RIGHT,
		(GXColor){173, 187, 216, 255});
	drawInit();
}

uiDrawObj_t* DrawManual(const uiManualView_t *view)
{
	uiDrawObj_t *event = calloc(1, sizeof(*event));
	drawManualEvent_t *data = calloc(1, sizeof(*data));

	if(event == NULL || data == NULL) {
		free(event);
		free(data);
		return NULL;
	}
	data->view = *view;
	data->hintScale = GetHintScaleToFitInWidthWithMax(view->hints, 480, 0.46f);
	event->type = EV_MANUAL;
	event->data = data;
	return event;
}

void DrawUpdateManual(uiDrawObj_t *page, const uiManualView_t *view)
{
	float hintScale = GetHintScaleToFitInWidthWithMax(view->hints, 480, 0.46f);

	if(page == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(!page->disposed && page->type == EV_MANUAL && page->data != NULL) {
		drawManualEvent_t *data = page->data;
		data->view = *view;
		data->hintScale = hintScale;
	}
	LWP_MutexUnlock(_videomutex);
}

/* ------------------------------------------------------------------------
 * Settings, in the cheat browser's language (_DrawCheats): the page, the
 * value list and the help card. settings.c prepares every string on the menu
 * thread; these only draw. The page is one event for the whole Settings
 * session, and its focus card springs from row to row inside it.
 * --------------------------------------------------------------------- */
static const GXColor settingsInk = {241, 244, 255, 255};
static const GXColor settingsQuiet = {173, 187, 216, 255};
static const GXColor settingsDim = {104, 112, 142, 255};
/* Indigo's lilac. Menu Color turns it with the rest of the page. */
static const GXColor settingsAccent = {196, 177, 255, 255};
static const GXColor settingsBack = {8, 12, 27, 255};
static const GXColor settingsCard = {16, 23, 43, 255};
static const GXColor settingsFocus = {38, 37, 78, 255};
static const GXColor settingsRule = {43, 52, 80, 255};
static const GXColor settingsTrack = {34, 42, 65, 255};
static const GXColor settingsValue = {27, 35, 62, 255};
static const GXColor settingsField = {10, 14, 31, 255};
static const GXColor settingsSwatch = {122, 104, 224, 255};
static const GXColor settingsScrim = {4, 3, 15, 184};

typedef struct {
	uiSetPageSnapshot_t snapshot;
	uiSettingsFocusState_t focus;
	int focusView;
	/* The current tab's cell, its left edge and width, sliding between
	 * tabs; placed when the page first draws. */
	uiMotionSpring_t tabX;
	uiMotionSpring_t tabW;
	bool tabPlaced;
} drawSettingsEvent_t;

typedef struct {
	uiSetHelpLayout_t layout;
	char title[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	char line[UI_SETLAYOUT_HELP_LINES][80];
	char hint[UI_SETLAYOUT_HINT_CAPACITY];
	float titleScale;
	float lineScale[UI_SETLAYOUT_HELP_LINES];
	float hintScale;
} drawSettingsHelpEvent_t;

static void _SettingsBox(const uiSetLayoutRect_t *rect, GXColor color)
{
	_CheatsPanel(rect->x, rect->y, rect->w, rect->h, color);
}

/* The focus card and its accent bar, the cheat browser's. */
static void _SettingsFocusCard(int x, int y, int width, int height)
{
	_CheatsPanel(x, y, width, height, settingsFocus);
	_CheatsPanel(x, y + 4, 3, height - 8, settingsAccent);
}

/* A choice steps with Left and Right: a small arrow each side of its value. */
static void _SettingsArrow(float x, float y, float direction, GXColor color)
{
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
		_putFlatVertex(x + 3.0f * direction, y, color);
		_putFlatVertex(x - 2.0f * direction, y - 5.0f, color);
		_putFlatVertex(x - 2.0f * direction, y + 5.0f, color);
	GX_End();
	drawInit();
}

/* A color row's swatch: saturated Indigo turns to the color its layer shows
 * on the screen, which is the one its row names. The hint icons' disc,
 * feathered one pixel, draws it round; those keep the controller's own
 * colors, so the swatch is recolored here. */
static void _SettingsSwatch(float cx, float cy, int layer)
{
	GXColor color = settingsSwatch;

	UIColor_Select(frameColors[layer]);
	UIColor_Apply(&color.r, &color.g, &color.b);
	UIColor_Select(frameColors[UI_COLOR_LAYER_MENU]);
	drawInit();
	_SetupRasterColor();
	_HintDisc(cx, cy, (float)UI_SETLAYOUT_SWATCH * 0.5f, color);
	drawInit();
}

static void _SettingsChip(int right, int y, const char *text)
{
	int x = right - UI_SETLAYOUT_CHIP_W;

	_CheatsPanel(x, y - UI_SETLAYOUT_CHIP_H / 2, UI_SETLAYOUT_CHIP_W,
		UI_SETLAYOUT_CHIP_H, _HintAlpha(settingsAccent, 56));
	drawStringMedium(x + UI_SETLAYOUT_CHIP_W / 2, y, text,
		UI_SETLAYOUT_CHIP_SCALE, ALIGN_CENTER, settingsAccent);
}

static void _SettingsRow(const uiSetLayout_t *layout, int slot,
	const uiSetPageRow_t *row, bool focused)
{
	int y = layout->rowTextY[slot];
	int top = y - UI_SETLAYOUT_PILL_H / 2;
	int right = layout->rowValueX;
	int left = right;
	u8 alpha = row->enabled ? 255 : 110;
	GXColor ink = row->enabled ? settingsInk : settingsDim;

	drawStringMedium(layout->rowLabelX, y, row->label, row->labelScale,
		ALIGN_LEFT, row->kind == UI_SETLAYOUT_ROW_ACTION && row->enabled ?
		settingsAccent : ink);
	switch(row->kind) {
		case UI_SETLAYOUT_ROW_TOGGLE:
			left = right - UI_SETLAYOUT_TOGGLE_W;
			_CheatsToggle(left, top, row->on, alpha);
			break;
		case UI_SETLAYOUT_ROW_CHOICE: {
			bool arrows = focused && row->enabled;
			int swatch = row->swatch ?
				UI_SETLAYOUT_SWATCH + UI_SETLAYOUT_SWATCH_GAP : 0;
			int width = row->valueWidth + swatch + UI_SETLAYOUT_PILL_PAD * 2 +
				(arrows ? UI_SETLAYOUT_ARROW_W * 2 : 0);
			int text;

			left = right - width;
			text = left + (width - row->valueWidth - swatch) / 2;
			_CheatsPanel(left, top, width, UI_SETLAYOUT_PILL_H,
				_HintAlpha(settingsValue, alpha));
			if(arrows) {
				_SettingsArrow((float)(left + 12), (float)y, -1.0f,
					settingsAccent);
				_SettingsArrow((float)(right - 12), (float)y, 1.0f,
					settingsAccent);
			}
			if(row->swatch) {
				_SettingsSwatch((float)text + UI_SETLAYOUT_SWATCH * 0.5f,
					(float)y, row->swatch - 1);
			}
			drawStringMedium(text + swatch, y, row->value, row->valueScale,
				ALIGN_LEFT, ink);
			break;
		}
		case UI_SETLAYOUT_ROW_TEXT:
			left = right - UI_SETLAYOUT_FIELD_W;
			_CheatsPanel(left, top, UI_SETLAYOUT_FIELD_W, UI_SETLAYOUT_PILL_H,
				_HintAlpha(settingsField, alpha));
			_CheatsPanel(left + 6, top + UI_SETLAYOUT_PILL_H - 2,
				UI_SETLAYOUT_FIELD_W - 12, 1, focused ? settingsAccent :
				_HintAlpha(settingsRule, alpha));
			drawStringMedium(left + UI_SETLAYOUT_PILL_PAD, y, row->value,
				row->valueScale, ALIGN_LEFT,
				row->placeholder ? settingsDim : ink);
			break;
		case UI_SETLAYOUT_ROW_LINK:
			drawStringMedium(right - 16, y, row->value, row->valueScale,
				ALIGN_RIGHT, settingsQuiet);
			drawStringMedium(right, y, "\233", 0.90f, ALIGN_RIGHT,
				focused ? settingsAccent : settingsQuiet);
			break;
		default:
			break;
	}
	if(row->custom) {
		_SettingsChip(left - UI_SETLAYOUT_CHIP_GAP, y, "CUSTOM");
	}
}

static void _DrawSettingsPage(uiDrawObj_t *evt)
{
	drawSettingsEvent_t *data = (drawSettingsEvent_t*)evt->data;
	const uiSetPageSnapshot_t *s = &data->snapshot;
	const uiSetLayout_t *l = &s->layout;
	uiMotionMode_t motion = _CurrentMotionMode();
	uiSettingsFocusFrame_t frame;
	GXColor back = settingsBack;
	int focusSlot = l->selectedRow >= 0 ?
		l->selectedRow - l->firstVisibleRow : -1;
	int focusLeft;
	int focusTop;
	int i;

	/* A new view snaps the focus card; within one it springs. */
	if(!data->focus.initialized || data->focusView != s->view) {
		UISettingsFocus_Init(&data->focus, &l->focusRect);
		data->focusView = s->view;
	}
	else {
		UISettingsFocus_Retarget(&data->focus, &l->focusRect, motion);
	}
	UISettingsFocus_Update(&data->focus, UIAnim_Delta(), motion, &frame);

	back.a = UI_SETLAYOUT_PAGE_ALPHA;
	_PagePanel(UI_SETLAYOUT_PAGE_X, UI_SETLAYOUT_PAGE_Y, UI_SETLAYOUT_PAGE_W,
		UI_SETLAYOUT_PAGE_H, back);
	_SettingsBox(&l->accentBar, settingsAccent);
	drawStringMedium(l->titleX, l->titleY, s->title, s->titleScale,
		ALIGN_LEFT, settingsInk);
	if(l->tabCount > 0) {
		const uiSetLayoutRect_t *cell = &l->tabCell[l->currentTab];
		int cellLeft;

		if(!data->tabPlaced) {
			UIMotion_SpringInit(&data->tabX, (float)cell->x, 25.0f);
			UIMotion_SpringInit(&data->tabW, (float)cell->w, 25.0f);
			data->tabPlaced = true;
		}
		UIMotion_SpringRetarget(&data->tabX, (float)cell->x, motion);
		UIMotion_SpringRetarget(&data->tabW, (float)cell->w, motion);
		UIMotion_SpringUpdate(&data->tabX, UIAnim_Delta(), motion);
		UIMotion_SpringUpdate(&data->tabW, UIAnim_Delta(), motion);
		/* Whole-pixel edges: each moves one way as the cell slides. */
		cellLeft = (int)lrintf(data->tabX.value);
		_SettingsBox(&l->tabTrack, settingsCard);
		_CheatsPanel(cellLeft, cell->y,
			(int)lrintf(data->tabX.value + data->tabW.value) - cellLeft,
			cell->h, settingsFocus);
		for(i = 0; i < l->tabCount; i++) {
			drawStringMedium(l->tabLabelCenterX[i], l->tabLabelY, s->tab[i],
				s->tabScale[i], ALIGN_CENTER,
				i == l->currentTab ? settingsAccent : settingsQuiet);
		}
		_DrawHintText(l->tabLeftGlyphX, l->tabLabelY, "L",
			UI_SETLAYOUT_TAB_SCALE, ALIGN_LEFT, settingsQuiet);
		_DrawHintText(l->tabRightGlyphX, l->tabLabelY, "R",
			UI_SETLAYOUT_TAB_SCALE, ALIGN_RIGHT, settingsQuiet);
	}
	else {
		drawStringMedium(l->badgeX, l->badgeY, s->badge, s->badgeScale,
			ALIGN_RIGHT, settingsAccent);
	}
	drawStringMedium(l->subtitleX, l->subtitleY, s->subtitle,
		s->subtitleScale, ALIGN_LEFT, settingsQuiet);
	_SettingsBox(&l->topDivider, settingsRule);
	drawStringMedium(l->sectionX, l->sectionY, s->section,
		UI_SETLAYOUT_SECTION_SCALE, ALIGN_LEFT, settingsAccent);
	drawStringMedium(l->positionX, l->positionY, s->position,
		UI_SETLAYOUT_POSITION_SCALE, ALIGN_RIGHT, settingsQuiet);

	for(i = 0; i < l->visibleRowCount; i++) {
		_SettingsBox(&l->rowRect[i], settingsCard);
	}
	for(i = 0; i < l->actionCount; i++) {
		_SettingsBox(&l->actionRect[i], settingsCard);
	}
	/* Whole-pixel edges: each moves one way as the card slides. */
	focusLeft = (int)lrintf(frame.x);
	focusTop = (int)lrintf(frame.y);
	_SettingsFocusCard(focusLeft, focusTop,
		(int)lrintf(frame.x + frame.w) - focusLeft,
		(int)lrintf(frame.y + frame.h) - focusTop);
	for(i = 0; i < l->visibleRowCount; i++) {
		_SettingsRow(l, i, &s->rows[i], i == focusSlot);
	}
	if(l->scrollVisible) {
		_SettingsBox(&l->scrollTrack, settingsTrack);
		_SettingsBox(&l->scrollThumb, settingsAccent);
	}

	drawStringMedium(l->descriptionX, l->descriptionY, s->description,
		s->descriptionScale, ALIGN_LEFT, settingsQuiet);
	_SettingsBox(&l->bottomDivider, settingsRule);
	for(i = 0; i < s->hintCount; i++) {
		_DrawHintText(s->hintX[i], l->hintY, s->hint[i],
			UI_SETLAYOUT_HINT_SCALE, ALIGN_LEFT,
			i == 0 ? settingsInk : settingsQuiet);
	}
	for(i = 0; i < l->actionCount; i++) {
		drawStringMedium(l->actionRect[i].x + l->actionRect[i].w / 2, l->hintY,
			s->action[i], s->actionScale[i], ALIGN_CENTER,
			i == l->selectedAction ? settingsInk : settingsQuiet);
	}
	drawInit();
}

uiDrawObj_t* DrawSettingsPage(const uiSetPageSnapshot_t *snapshot)
{
	drawSettingsEvent_t *data = calloc(1, sizeof(*data));
	uiDrawObj_t *event = calloc(1, sizeof(*event));

	if(data == NULL || event == NULL) {
		free(data);
		free(event);
		return NULL;
	}
	data->snapshot = *snapshot;
	event->type = EV_SETTINGS;
	event->data = data;
	return event;
}

/* The copy and the colors it was built with change in one step, so the page
 * never shows a label in another color. Settings changes a value on the
 * press but draws on the release, and its value list steps the live value
 * through every choice while it builds, so the screen follows the page, not
 * swissSettings. */
void DrawUpdateSettingsPage(uiDrawObj_t *page,
	const uiSetPageSnapshot_t *snapshot, const int colors[UI_COLOR_LAYERS])
{
	if(page == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(!page->disposed && page->type == EV_SETTINGS && page->data != NULL) {
		((drawSettingsEvent_t*)page->data)->snapshot = *snapshot;
		menuColorPage = page;
		for(int i = 0; i < UI_COLOR_LAYERS; i++) {
			menuColorPinned[i] = colors[i];
			menuColorPreview[i] = -1;
		}
	}
	LWP_MutexUnlock(_videomutex);
}

static void _DrawSettingsCard(const uiSetLayoutRect_t *card,
	const uiSetLayoutRect_t *accentBar, const uiSetLayoutRect_t *topDivider,
	const uiSetLayoutRect_t *bottomDivider)
{
	_CheatsPanel(UI_SETLAYOUT_PAGE_X, UI_SETLAYOUT_PAGE_Y, UI_SETLAYOUT_PAGE_W,
		UI_SETLAYOUT_PAGE_H, settingsScrim);
	_SettingsBox(card, settingsCard);
	_SettingsBox(accentBar, settingsAccent);
	_SettingsBox(topDivider, settingsRule);
	_SettingsBox(bottomDivider, settingsRule);
}

static void _DrawSettingsList(uiDrawObj_t *evt)
{
	const uiSetListSnapshot_t *s = (const uiSetListSnapshot_t*)evt->data;
	const uiSetListLayout_t *l = &s->layout;
	int focus = l->focus - l->first;
	int i;

	_DrawSettingsCard(&l->card, &l->accentBar, &l->topDivider,
		&l->bottomDivider);
	drawStringMedium(l->titleX, l->titleY, s->title, s->titleScale,
		ALIGN_LEFT, settingsInk);
	for(i = 0; i < l->visibleCount; i++) {
		_SettingsBox(&l->rowRect[i], settingsBack);
	}
	/* Drawn where it is: the list's focus never slides. */
	_SettingsFocusCard(l->focusRect.x, l->focusRect.y, l->focusRect.w,
		l->focusRect.h);
	for(i = 0; i < l->visibleCount; i++) {
		drawStringMedium(l->rowTextX, l->rowTextY[i], s->value[i],
			s->valueScale[i], ALIGN_LEFT,
			i == focus ? settingsInk : settingsQuiet);
		if(i == s->current) {
			_SettingsChip(l->chipRight, l->rowTextY[i], "CURRENT");
		}
	}
	if(l->scrollVisible) {
		_SettingsBox(&l->scrollTrack, settingsTrack);
		_SettingsBox(&l->scrollThumb, settingsAccent);
	}
	_DrawHintText(l->hintX, l->hintY, s->hint[0], s->hintScale[0], ALIGN_LEFT,
		settingsInk);
	_DrawHintText(l->hintRightX, l->hintY, s->hint[1], s->hintScale[1],
		ALIGN_RIGHT, settingsQuiet);
	drawInit();
}

uiDrawObj_t* DrawSettingsList(const uiSetListSnapshot_t *snapshot)
{
	uiSetListSnapshot_t *data = calloc(1, sizeof(*data));
	uiDrawObj_t *event = calloc(1, sizeof(*event));

	if(data == NULL || event == NULL) {
		free(data);
		free(event);
		return NULL;
	}
	*data = *snapshot;
	event->type = EV_SETTINGSLIST;
	event->data = data;
	return event;
}

void DrawUpdateSettingsList(uiDrawObj_t *list,
	const uiSetListSnapshot_t *snapshot, int previewLayer, int previewColor)
{
	if(list == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(!list->disposed && list->type == EV_SETTINGSLIST && list->data != NULL) {
		*(uiSetListSnapshot_t*)list->data = *snapshot;
		if(previewLayer >= 0 && previewLayer < UI_COLOR_LAYERS) {
			menuColorPreview[previewLayer] = previewColor;
		}
	}
	LWP_MutexUnlock(_videomutex);
}

static void _DrawSettingsHelp(uiDrawObj_t *evt)
{
	const drawSettingsHelpEvent_t *data =
		(const drawSettingsHelpEvent_t*)evt->data;
	const uiSetHelpLayout_t *l = &data->layout;
	int i;

	_DrawSettingsCard(&l->card, &l->accentBar, &l->topDivider,
		&l->bottomDivider);
	drawStringMedium(l->titleX, l->titleY, data->title, data->titleScale,
		ALIGN_LEFT, settingsInk);
	for(i = 0; i < l->lineCount; i++) {
		drawStringMedium(l->lineX, l->lineY0 + i * l->linePitch,
			data->line[i], data->lineScale[i], ALIGN_LEFT, settingsQuiet);
	}
	_DrawHintText(l->hintX, l->hintY, data->hint, data->hintScale, ALIGN_LEFT,
		settingsInk);
	drawInit();
}

static float _SettingsHelpFit(char *out, size_t capacity, const char *text,
	size_t length, int width, float scale, float floor,
	uiSetLayoutTextMeasureFn measure)
{
	uiSetLayoutTextFit_t fit;

	if(!UISetLayout_PrepareText(text, length, length,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, 0, 1, width,
		scale, floor, measure, out, capacity, &fit)) {
		out[0] = '\0';
		return floor;
	}
	return fit.scale;
}

/* The help's first line is its title; the lines after the blank one under it
 * are drawn as written. Built on the menu thread, drawn as it is. */
uiDrawObj_t* DrawSettingsHelp(const char *help)
{
	drawSettingsHelpEvent_t *data = calloc(1, sizeof(*data));
	uiDrawObj_t *event = calloc(1, sizeof(*event));
	const char *cursor = help != NULL ? help : "";
	const char *end = strchr(cursor, '\n');
	char title[UI_SETLAYOUT_TEXT_BUFFER_SIZE];
	size_t length;
	int count = 0;

	if(data == NULL || event == NULL) {
		free(data);
		free(event);
		return NULL;
	}
	/* The widths don't depend on the number of lines. */
	UISetLayout_ComputeHelp(0, &data->layout);
	if(end == NULL) {
		end = cursor + strlen(cursor);
	}
	length = MIN((size_t)(end - cursor), sizeof(title) - 1u);
	memcpy(title, cursor, length);
	title[length] = '\0';
	length = UISetLayout_Label(title, title, sizeof(title));
	data->titleScale = _SettingsHelpFit(data->title, sizeof(data->title),
		title, length, data->layout.titleMaxWidth,
		UI_SETLAYOUT_CARD_TITLE_SCALE, UI_SETLAYOUT_ROW_TEXT_FLOOR,
		GetTextSizeInPixels);
	/* Y or B closes it, as before. */
	data->hintScale = _SettingsHelpFit(data->hint, sizeof(data->hint),
		"B  Close", 8u, data->layout.lineMaxWidth, UI_SETLAYOUT_HINT_SCALE,
		UI_SETLAYOUT_HINT_SCALE, GetHintSizeInPixels);
	cursor = *end == '\n' ? end + 1 : end;
	while(*cursor == '\n') {
		cursor++;
	}
	while(*cursor != '\0' && count < UI_SETLAYOUT_HELP_LINES) {
		end = strchr(cursor, '\n');
		if(end == NULL) {
			end = cursor + strlen(cursor);
		}
		/* A blank line keeps its place as the gap between paragraphs. */
		data->lineScale[count] = end > cursor ?
			_SettingsHelpFit(data->line[count], sizeof(data->line[count]),
				cursor, (size_t)(end - cursor), data->layout.lineMaxWidth,
				UI_SETLAYOUT_HELP_SCALE, UI_SETLAYOUT_HELP_SCALE,
				GetTextSizeInPixels) :
			UI_SETLAYOUT_HELP_SCALE;
		count++;
		cursor = *end == '\n' ? end + 1 : end;
	}
	UISetLayout_ComputeHelp(count, &data->layout);
	event->type = EV_SETTINGSHELP;
	event->data = data;
	return event;
}

/* Memory Cards folder identity: graph paper and its native blue panel.
 * Every path/content line is prepared on the menu thread. */
static void _SaveCubesBackdrop(float paper, float handover);
static void _SaveCubesBox(float x, float y, float width, float height,
	GXColor top, GXColor bottom, GXColor edge, float line);
static void _SaveCubesVertex(float x, float y, GXColor color, float s, float t);

static void _DrawMemoryCardFolder(uiDrawObj_t *event)
{
	const uiFolderSnapshot_t *s = (const uiFolderSnapshot_t*)event->data;
	GXColor ink = {255, 255, 255, 255};
	GXColor quiet = {190, 205, 231, 255};
	GXColor color = {0, 0, 0, 255};
	char position[32];
	_SaveCubesBackdrop(1.0f, 0.0f);
	_SaveCubesBox(24, 52, 592, 394, (GXColor){39, 53, 153, 250},
		(GXColor){58, 31, 127, 250}, (GXColor){196, 186, 255, 255}, 2.0f);
	drawStringMedium(48, 78, "FOLDER", 0.72f, ALIGN_LEFT, ink);
	drawStringMedium(48, 112, "FULL PATH", 0.42f, ALIGN_LEFT, quiet);
	for(u32 i = 0u; i < UI_FOLDER_VISIBLE_LINES && s->firstLine + i < s->lineCount; ++i)
		drawStringMedium(48, 136 + (int)i * 22, s->lines[s->firstLine + i],
			0.46f, ALIGN_LEFT, ink);
	if(s->lineCount > UI_FOLDER_VISIBLE_LINES) {
		snprintf(position, sizeof(position), "%u-%u of %u", (unsigned)s->firstLine + 1u,
			(unsigned)MIN(s->firstLine + UI_FOLDER_VISIBLE_LINES, s->lineCount),
			(unsigned)s->lineCount);
		drawStringMedium(584, 264, position, 0.40f, ALIGN_RIGHT, quiet);
		_DrawHintText(48, 264, "UP/DOWN  Scroll path", 0.42f, ALIGN_LEFT, quiet);
	}
	drawStringMedium(48, 288, s->summary, 0.40f, ALIGN_LEFT, quiet);
	for(int i = 0; i < 2; ++i)
		drawStringMedium(48, 307 + i * 19, s->contents[i], 0.42f, ALIGN_LEFT, ink);
	UIFolder_ColorRGB(s->color, &color.r, &color.g, &color.b);
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	_SaveCubesVertex(48, 350, color, 0, 0); _SaveCubesVertex(70, 350, color, 0, 0);
	_SaveCubesVertex(70, 372, color, 0, 0); _SaveCubesVertex(48, 372, color, 0, 0);
	GX_End();
	drawStringMedium(88, 352, "COLOR", 0.46f, ALIGN_LEFT, quiet);
	drawStringMedium(240, 352, UIFolder_ColorName(s->color), 0.55f, ALIGN_LEFT, ink);
	drawStringMedium(48, 379, s->status[0] ? s->status :
		"Left/Right previews a color; Default keeps the original folder cube.",
		0.36f, ALIGN_LEFT, quiet);
	_DrawHintText(320, 412, "LEFT/RIGHT  Color   Y  Reset   A  Save   B  Cancel",
		0.44f, ALIGN_CENTER, ink);
	drawInit();
}

uiDrawObj_t* DrawMemoryCardFolder(const uiFolderSnapshot_t *snapshot)
{
	uiDrawObj_t *event = calloc(1, sizeof(*event));
	uiFolderSnapshot_t *data = malloc(sizeof(*data));
	if(event == NULL || data == NULL) { free(event); free(data); return NULL; }
	*data = *snapshot;
	event->type = EV_MEMORY_FOLDER;
	event->data = data;
	return event;
}

void DrawUpdateMemoryCardFolder(uiDrawObj_t *event, const uiFolderSnapshot_t *snapshot)
{
	if(event == NULL || snapshot == NULL) return;
	LWP_MutexLock(_videomutex);
	if(!event->disposed && event->type == EV_MEMORY_FOLDER && event->data != NULL)
		*(uiFolderSnapshot_t*)event->data = *snapshot;
	LWP_MutexUnlock(_videomutex);
}

/* ------------------------------------------------------------------------
 * Memory Cards' folder chooser (saves.c), in the same language: a row per
 * folder, and the focus card springing from row to row as the cheat list's
 * does.
 * --------------------------------------------------------------------- */
typedef struct {
	uiSavesPageSnapshot_t snapshot;
	uiMotionSpring_t focusY;
	bool focusInitialized;
	u32 focusList;
} drawSavesEvent_t;

/* A folder: its tab and body. */
static void _SavesFolder(int x, int y, GXColor color)
{
	_CheatsPanel(x + 31, y + 5, 14, 6, color);
	_CheatsPanel(x + 31, y + 9, 34, 19, color);
}

static void _DrawSaves(uiDrawObj_t *evt)
{
	drawSavesEvent_t *data = (drawSavesEvent_t*)evt->data;
	const uiSavesPageSnapshot_t *s = &data->snapshot;
	uiMotionMode_t motion = _CurrentMotionMode();
	float target = (float)(148 + s->focusRow * 40);
	int focusY;
	int i;

	/* Another folder snaps the focus; within one it slides. */
	if(!data->focusInitialized || data->focusList != s->list) {
		UIMotion_SpringInit(&data->focusY, target, 25.0f);
		data->focusInitialized = true;
		data->focusList = s->list;
	}
	UIMotion_SpringRetarget(&data->focusY, target, motion);
	focusY = (int)lrintf(UIMotion_SpringUpdate(&data->focusY,
		UIAnim_Delta(), motion));

	_PagePanel(-6, -6, 652, 492, settingsBack);
	_CheatsPanel(40, 30, 40, 3, settingsAccent);
	drawStringMedium(40, 63, s->title, 1.05f, ALIGN_LEFT, settingsInk);
	drawStringMedium(600, 63, s->status, 0.54f, ALIGN_RIGHT, settingsAccent);
	_CheatsPanel(40, 115, 560, 1, settingsRule);
	drawStringMedium(40, 132, s->section, 0.42f, ALIGN_LEFT, settingsAccent);
	drawStringMedium(600, 132, s->position, 0.46f, ALIGN_RIGHT, settingsQuiet);
	for(i = 0; i < s->rowCount; i++) {
		_CheatsPanel(40, 148 + i * 40, 560, 34, settingsCard);
	}
	if(s->rowCount > 0) {
		_CheatsPanel(40, focusY, 560, 34, settingsFocus);
		_CheatsPanel(40, focusY + 4, 3, 26, settingsAccent);
	}
	for(i = 0; i < s->rowCount; i++) {
		const uiSavesPageRow_t *row = &s->rows[i];
		bool focused = i == s->focusRow;
		int y = 148 + i * 40;

		_SavesFolder(52, y + 1, focused ? settingsAccent : settingsSwatch);
		drawStringMedium(160, y + 17, row->title, 0.62f, ALIGN_LEFT,
			focused ? settingsInk : settingsQuiet);
		drawStringMedium(588, y + 17, row->blocks, 0.50f, ALIGN_RIGHT,
			settingsQuiet);
	}
	if(s->count > UI_SAVES_PAGE_ROWS) {
		int thumb = 234 * UI_SAVES_PAGE_ROWS / s->count;
		int travel;

		if(thumb < 12) thumb = 12;
		travel = (int)((int64_t)(234 - thumb) * s->first /
			(s->count - UI_SAVES_PAGE_ROWS));
		_CheatsPanel(608, 148, 2, 234, settingsTrack);
		_CheatsPanel(608, 148 + travel, 2, thumb, settingsAccent);
	}
	if(s->empty[0][0] != '\0') {
		drawStringMedium(320, 236, s->empty[0], 0.76f, ALIGN_CENTER,
			settingsInk);
		drawStringMedium(320, 269, s->empty[1], 0.54f, ALIGN_CENTER,
			settingsQuiet);
	}
	_CheatsPanel(40, 413, 560, 1, settingsRule);
	_DrawHintText(40, 435, s->hint[0], 0.48f, ALIGN_LEFT, settingsInk);
	_DrawHintText(600, 435, s->hint[1], 0.48f, ALIGN_RIGHT, settingsQuiet);
	drawInit();
}

uiDrawObj_t* DrawSavesPage(const uiSavesPageSnapshot_t *snapshot)
{
	drawSavesEvent_t *data = memalign(32, sizeof(*data));
	uiDrawObj_t *event = calloc(1, sizeof(*event));

	if(data == NULL || event == NULL) {
		free(data);
		free(event);
		return NULL;
	}
	memset(data, 0, sizeof(*data));
	data->snapshot = *snapshot;
	event->type = EV_SAVES;
	event->data = data;
	return event;
}

void DrawUpdateSavesPage(uiDrawObj_t *page,
	const uiSavesPageSnapshot_t *snapshot)
{
	if(page == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(!page->disposed && page->type == EV_SAVES && page->data != NULL) {
		((drawSavesEvent_t*)page->data)->snapshot = *snapshot;
	}
	LWP_MutexUnlock(_videomutex);
}

/* ------------------------------------------------------------------------
 * Memory Cards' cube screen (saves.c): two stacks of save cubes over graph
 * paper, as the IPL's Memory Card screen shows Slot A and Slot B, with the
 * focused save's banner and comment below. ui_save_cubes.c lays the cubes
 * out, moves them and projects their faces on the CPU; here they go through
 * the 2D pipeline every page uses, Z off and nothing culled, so a frame
 * asks nothing new of GX.
 * --------------------------------------------------------------------- */

/* The backdrop: the graph paper as much as it shows, and the Home cube while
 * it hands over to the cubes or back, as _DrawBackground would draw it. */
static void _SaveCubesBackdrop(float paper, float handover)
{
	int icons[UI_HOME_FACE_COUNT];

	_HomeFaceIcons(icons);
	IndigoBackground_DrawSavesBackdrop(paper, handover, UIAnim_Seconds(),
		_CurrentMotionMode() == UI_MOTION_FULL, UIScene_Frame(),
		&systemInstrument.clock, icons);
}

/* What one frame's cubes need. */
typedef struct {
	uiSaveCube_t cubes[UI_SAVE_CUBES_OUT];
	uiSaveCubesQuad_t quads[UI_SAVE_CUBES_OUT * UI_SAVE_CUBES_QUADS];
	uiSaveCubesQuad_t coverage[UI_SAVE_CUBES_OUT * UI_SAVE_CUBES_COVERAGE];
	u16 quadEnd[UI_SAVE_CUBES_OUT];
	u16 coverageEnd[UI_SAVE_CUBES_OUT];
	GXColor shades[UI_SAVE_CUBES_SHADES][UI_SAVE_CUBES_ROLES];
	GXTexObj icon;
	bool invalidated;	/* the texture cache, this frame */
} saveCubesDraw_t;

/* Each shade of each part of a cube, through Menu Color once a frame. */
static void _SaveCubesShades(saveCubesDraw_t *draw)
{
	int shade, role;

	for(shade = 0; shade < UI_SAVE_CUBES_SHADES; shade++) {
		for(role = 0; role < UI_SAVE_CUBES_ROLES; role++) {
			u8 rgba[4];

			UISaveCubes_Colour(shade, role, rgba);
			UIColor_Apply(&rgba[0], &rgba[1], &rgba[2]);
			draw->shades[shade][role] = (GXColor) {rgba[0], rgba[1], rgba[2], rgba[3]};
		}
	}
}

static GXColor _SaveCubesColour(const saveCubesDraw_t *draw, const uiSaveCube_t *cube, int role)
{
	u8 rgba[4];
	if(UISaveCubes_FolderColour(cube, role, rgba))
		return (GXColor){rgba[0], rgba[1], rgba[2], rgba[3]};
	return draw->shades[cube->shade][role];
}

static void _SaveCubesVertex(float x, float y, GXColor color, float s, float t)
{
	GX_Position3f32(x, y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(s, t);
}

/* Cubes first .. end - 1: all their faces in one batch, then each icon on
 * its quad in the game's own colors, blended by its alpha (drawInit's blend
 * would add a clear texel's color). The texture cache is cleared before the
 * frame's first icon only, not per icon as _DrawTexObjNow would: the loader
 * writes only slots no published cube names. */
static void _SaveCubesEmit(saveCubesDraw_t *draw, int first, int end)
{
	static const float s[4] = {0.0f, 1.0f, 1.0f, 0.0f};
	static const float t[4] = {0.0f, 0.0f, 1.0f, 1.0f};
	float left = UIStage_Left(), right = UIStage_Right();
	int quads = 0, coverage = 0, bodies = 0, i, k, v;
	bool icons = false;

	for(i = first; i < end; i++) {
		int n = UISaveCubes_Faces(&draw->cubes[i], left, right, draw->quads + quads);
		int edges = UISaveCubes_Coverage(draw->quads + quads, n,
			UIStage_PixelWidth(), draw->coverage + coverage);

		quads += n;
		coverage += edges;
		bodies += n + edges - (n > 0 && draw->quads[quads - 1].role == UI_SAVE_CUBES_ROLE_ICON);
		draw->quadEnd[i] = (u16)quads;
		draw->coverageEnd[i] = (u16)coverage;
	}
	if(bodies > 0) {
		drawInit();
		_SetupRasterColor();
		GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(4 * bodies));
		for(i = first; i < end; i++) {
			const uiSaveCube_t *cube = &draw->cubes[i];

			for(k = i > first ? draw->quadEnd[i - 1] : 0; k < draw->quadEnd[i]; k++) {
				const uiSaveCubesQuad_t *quad = &draw->quads[k];
				GXColor color;

				if(quad->role == UI_SAVE_CUBES_ROLE_ICON) {
					continue;
				}
				color = _SaveCubesColour(draw, cube, quad->role);
				color.a = (u8)(color.a * cube->alpha / 255);
				for(v = 0; v < 4; v++) {
					_SaveCubesVertex(quad->x[v], quad->y[v], color, 0.0f, 0.0f);
				}
			}
			for(k = i > first ? draw->coverageEnd[i - 1] : 0; k < draw->coverageEnd[i]; k++) {
				const uiSaveCubesQuad_t *quad = &draw->coverage[k];
				GXColor color = _SaveCubesColour(draw, cube, quad->role);

				color.a = (u8)(color.a * cube->alpha / 255);
				for(v = 0; v < 4; v++) {
					GXColor faded = color;
					if(v >= 2) faded.a = 0;
					_SaveCubesVertex(quad->x[v], quad->y[v], faded, 0.0f, 0.0f);
				}
			}
		}
		GX_End();
	}
	for(i = first; i < end; i++) {
		const uiSaveCubesQuad_t *quad;
		GXColor white = {255, 255, 255, draw->cubes[i].alpha};

		if(draw->quadEnd[i] == (i > first ? draw->quadEnd[i - 1] : 0)) {
			continue;
		}
		quad = &draw->quads[draw->quadEnd[i] - 1];
		if(quad->role != UI_SAVE_CUBES_ROLE_ICON) {
			continue;
		}
		if(!icons) {
			drawInit();
			GX_SetNumTevStages(1);
			GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
				GX_LO_CLEAR);
			if(!draw->invalidated) {
				GX_InvalidateTexAll();
				draw->invalidated = true;
			}
			icons = true;
		}
		GX_InitTexObj(&draw->icon, (void *)draw->cubes[i].icon, 32, 32,
			GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GX_LoadTexObj(&draw->icon, GX_TEXMAP0);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		for(v = 0; v < 4; v++) {
			_SaveCubesVertex(quad->x[v], quad->y[v], white, s[v], t[v]);
		}
		GX_End();
	}
	if(icons) {
		drawInit();
	}
}

typedef struct {
	uiSaveCubesPageSnapshot_t snapshot;
	uiSaveCubesMotion_t motion;
	saveCubesDraw_t draw;
	GXTexObj picture;	/* the info bar's banner, or its icon */
	/* The last box and message shown, drawn while they fade out. */
	uiSaveCubesMenu_t menu;
	u8 menuFocus;
	char message[96];
} drawSaveCubesEvent_t;

static GXColor _SaveCubesFaded(GXColor color, float alpha)
{
	color.a = (u8)((float)color.a * alpha + 0.5f);
	return color;
}

/* A box of fill shading top to bottom, inside an edge line px wide. */
static void _SaveCubesBox(float x, float y, float width, float height,
	GXColor top, GXColor bottom, GXColor edge, float line)
{
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 20);
		_putFlatVertex(x, y, top);
		_putFlatVertex(x + width, y, top);
		_putFlatVertex(x + width, y + height, bottom);
		_putFlatVertex(x, y + height, bottom);
		_putFlatRect(x, y, width, line, edge);
		_putFlatRect(x, y + height - line, width, line, edge);
		_putFlatRect(x, y + line, line, height - 2.0f * line, edge);
		_putFlatRect(x + width - line, y + line, line, height - 2.0f * line, edge);
	GX_End();
}

/* A stack's header, "A  Open" and its free blocks in a box, or the SD
 * card's open folder; the arrows when rows lie above or below the window;
 * or, with no grid, why. */
static void _SaveCubesHeader(const uiSaveCubesStack_t *stack,
	const uiSaveCubesStackText_t *text, float middle, float alpha)
{
	const GXColor white = _SaveCubesFaded((GXColor) {255, 255, 255, 255}, alpha);
	const GXColor shadow = _SaveCubesFaded((GXColor) {0, 0, 0, 200}, alpha);
	const GXColor quiet = _SaveCubesFaded(settingsQuiet, alpha);
	int rows = stack->cells / UI_SAVE_CUBES_COLUMNS;
	int i;

	_SaveCubesBox(middle - 112.0f, 28.0f, 224.0f, 27.0f,
		_SaveCubesFaded((GXColor) {18, 27, 91, 180}, alpha), shadow, quiet, 1.0f);
	drawStringMedium((int)middle, 42, text->control, 0.5f, ALIGN_CENTER, white);
	drawStringMedium((int)middle - 100, 74, text->name, 1.5f, ALIGN_LEFT, white);
	if(text->free[0] != '\0') {
		drawStringMedium((int)middle - 66, 80, "Open", 0.5f, ALIGN_LEFT, white);
		_SaveCubesBox(middle - 20.0f, 60.0f, 56.0f, 28.0f, shadow, shadow, white, 2.0f);
		drawStringMedium((int)middle + 8, 74, text->free, 0.6f, ALIGN_CENTER, white);
	}
	else {
		drawStringMedium((int)middle - 44, 78, text->path, 0.42f, ALIGN_LEFT,
			quiet);
	}
	if(stack->cells <= 0) {
		drawStringMedium((int)middle, 220, text->note[0], text->noteScale[0],
			ALIGN_CENTER, white);
		drawStringMedium((int)middle, 248, text->note[1], text->noteScale[1],
			ALIGN_CENTER, quiet);
		return;
	}
	for(i = 0; i < 2; i++) {
		float tip = i ? 354.5f : 99.5f, base = i ? 345.5f : 108.5f;

		if(i ? stack->first + UI_SAVE_CUBES_ROWS >= rows : stack->first <= 0) {
			continue;
		}
		drawInit();
		_SetupRasterColor();
		GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
			_putFlatVertex(middle - 7.0f, base, white);
			_putFlatVertex(middle + 7.0f, base, white);
			_putFlatVertex(middle, tip, white);
		GX_End();
	}
}

/* A 96x32 banner or a 32x32 icon frame, RGB5A3, at (x, y). */
static void _SaveCubesPicture(GXTexObj *picture, const u8 *texels, int x,
	int y, int width)
{
	memset(picture, 0, sizeof(*picture));
	GX_InitTexObj(picture, (void *)texels, width, 32, GX_TF_RGB5A3, GX_CLAMP,
		GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(picture, GX_LINEAR, GX_NEAR);
	drawInit();
	_DrawTexObjNow(picture, x, y, width, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
	drawInit();
}

/* A flat bar, x .. x + width across, y .. y + height down. */
static void _SaveCubesBar(float x, float y, float width, float height,
	GXColor color)
{
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		_putFlatRect(x, y, width, height, color);
	GX_End();
}

/* The box beside the focused cube, as the IPL's: dimmed items grey, the
 * focus's bar sliding to its item. It opens from 0.92x and fades, about its
 * middle. */
static void _SaveCubesMenu(const uiSaveCubesMenu_t *menu, int focus,
	const uiSaveCubesGrid_t *grid, const uiSaveCubesMotion_t *motion)
{
	const float alpha = motion->menuAlpha, scale = motion->menuScale;
	const GXColor fill = _SaveCubesFaded((GXColor) {18, 27, 91, 230}, alpha);
	const GXColor edge = _SaveCubesFaded((GXColor) {196, 186, 255, 255}, alpha);
	uiSaveCubesBox_t box;
	float cubeX, cubeY, middleX, middleY, top;
	bool titled = menu->title[0] != '\0';
	int i;

	if(!(alpha > 0.0f) || menu->count == 0 || grid->focusStack < 0 ||
		grid->focusStack >= UI_SAVE_CUBES_STACKS) {
		return;
	}
	UISaveCubes_Where(grid->focusStack, grid->focusCell,
		(float)grid->stack[grid->focusStack].first, &cubeX, &cubeY);
	UISaveCubes_MenuBox(cubeX, cubeY, (float)menu->width, menu->count, titled, &box);
	top = titled ? box.titleY : box.y;
	middleX = box.x + 0.5f * box.width;
	middleY = 0.5f * (top + box.y + box.height);
#define MENU_X(x) (middleX + ((x) - middleX) * scale)
#define MENU_Y(y) (middleY + ((y) - middleY) * scale)
	if(titled) {
		_SaveCubesBox(MENU_X(box.x), MENU_Y(box.titleY), box.width * scale,
			UI_SAVE_CUBES_MENU_TITLE * scale, fill, fill, edge, 2.0f);
		drawStringMedium((int)MENU_X(box.x + 12.0f),
			(int)MENU_Y(box.titleY + 0.5f * UI_SAVE_CUBES_MENU_TITLE), menu->title,
			0.56f * scale, ALIGN_LEFT, _SaveCubesFaded((GXColor) {255, 255, 255, 255}, alpha));
	}
	_SaveCubesBox(MENU_X(box.x), MENU_Y(box.y), box.width * scale, box.height * scale,
		fill, fill, edge, 2.0f);
	_SaveCubesBar(MENU_X(box.x + 4.0f),
		MENU_Y(box.y + 8.0f + UI_SAVE_CUBES_MENU_PITCH * motion->menuItem),
		(box.width - 8.0f) * scale, UI_SAVE_CUBES_MENU_PITCH * scale,
		_SaveCubesFaded((GXColor) {70, 92, 200, 230}, alpha));
	for(i = 0; i < menu->count && i < 4; i++) {
		GXColor ink = (menu->dim >> i) & 1u ? (GXColor) {120, 120, 140, 255} :
			i == focus ? (GXColor) {255, 236, 170, 255} : (GXColor) {255, 255, 255, 255};

		drawStringMedium((int)MENU_X(box.x + 16.0f), (int)MENU_Y(box.y + 8.0f +
			UI_SAVE_CUBES_MENU_PITCH * ((float)i + 0.5f)), menu->item[i],
			0.56f * scale, ALIGN_LEFT, _SaveCubesFaded(ink, alpha));
	}
#undef MENU_X
#undef MENU_Y
}

/* The IPL's maroon message over the middle of the stage. */
static void _SaveCubesMessage(const char *text, float alpha)
{
	float width = (float)GetTextSizeInPixels(text) * 0.56f + 48.0f;

	if(!(alpha > 0.0f) || text[0] == '\0') {
		return;
	}
	if(width < 320.0f) {
		width = 320.0f;
	}
	_SaveCubesBox(320.0f - 0.5f * width, 200.0f, width, 50.0f,
		_SaveCubesFaded((GXColor) {120, 16, 36, 235}, alpha),
		_SaveCubesFaded((GXColor) {120, 16, 36, 235}, alpha),
		_SaveCubesFaded((GXColor) {255, 210, 220, 255}, alpha), 2.0f);
	drawStringMedium(320, 225, text, 0.56f, ALIGN_CENTER,
		_SaveCubesFaded((GXColor) {255, 255, 255, 255}, alpha));
}

static void _DrawSaveCubes(uiDrawObj_t *evt)
{
	drawSaveCubesEvent_t *data = (drawSaveCubesEvent_t*)evt->data;
	const uiSaveCubesPageSnapshot_t *s = &data->snapshot;
	const uiSaveCubesGrid_t *grid = &s->grid;
	uiMotionMode_t motion = _CurrentMotionMode();
	float left = UIStage_Left() + 40.0f, right = UIStage_Right() - 40.0f;
	float chrome;
	int count, floating, i;

	/* The cubes first: their motion says how much of the rest shows. */
	count = UISaveCubes_Frame(&data->motion, grid, UIAnim_Delta(), motion,
		data->draw.cubes, &floating);
	chrome = data->motion.chrome;
	_SaveCubesBackdrop(data->motion.paper, data->motion.handover);
	drawInit();
	_SaveCubesShades(&data->draw);
	data->draw.invalidated = false;
	for(i = 0; i < UI_SAVE_CUBES_STACKS && chrome > 0.0f; i++) {
		_SaveCubesHeader(&grid->stack[i], &s->stack[i],
			UI_SAVE_CUBES_STACK_X + (float)i * UI_SAVE_CUBES_STACK_GAP, chrome);
	}
	_SaveCubesEmit(&data->draw, 0, floating);
	/* The info bar: the focused save's banner (or its icon), its comment
	 * and its size, as the IPL's. Empty for a free cell. */
	if(chrome > 0.0f) {
		const GXColor white = _SaveCubesFaded((GXColor) {255, 255, 255, 255}, chrome);
		const GXColor box = _SaveCubesFaded((GXColor) {0, 0, 0, 200}, chrome);

		_SaveCubesBox(left, 362.0f, right - left, 70.0f,
			_SaveCubesFaded((GXColor) {39, 53, 153, 220}, chrome),
			_SaveCubesFaded((GXColor) {58, 31, 127, 220}, chrome),
			_SaveCubesFaded((GXColor) {196, 186, 255, 255}, chrome), 2.0f);
		if(s->info) {
			int blocks = 0;

			/* Pictures can't fade: they come with the words' second half. */
			bool pictures = chrome >= 0.5f;

			if(pictures && s->banner != NULL) {
				_SaveCubesPicture(&data->picture, s->banner, 56, 381, 96);
			}
			else if(pictures && s->folder) {
				_SavesFolder(56, 381, settingsSwatch);
			}
			else if(pictures && grid->focusStack >= 0) {
				const uiSaveCubesStack_t *stack = &grid->stack[grid->focusStack];
				int k = grid->focusCell - (stack->first - 1) * UI_SAVE_CUBES_COLUMNS;
				const u8 *icon = k >= 0 && k < UI_SAVE_CUBES_DRAWN ?
					UISaveCubes_Icon(&stack->cell[k], data->motion.seconds, motion) : NULL;

				if(icon != NULL) {
					_SaveCubesPicture(&data->picture, icon, 88, 381, 32);
				}
			}
			drawStringMedium(168, 388, s->line[0], 0.62f, ALIGN_LEFT, white);
			if(s->blocks[0] != '\0') {
				blocks = (int)((float)GetTextSizeInPixels(s->blocks) * 0.5f) + 16;
				if(blocks < 48) {
					blocks = 48;
				}
				_SaveCubesBox(168.0f, 398.0f, (float)blocks, 24.0f, box, box, white, 2.0f);
				drawStringMedium(168 + blocks / 2, 410, s->blocks, 0.5f, ALIGN_CENTER,
					white);
			}
			/* A dimmed item's reason takes the second line, in amber. */
			drawStringMedium(180 + blocks, 410, s->line[1], 0.5f, ALIGN_LEFT,
				_SaveCubesFaded(s->warn ? (GXColor) {255, 190, 80, 255} : settingsQuiet,
				chrome));
		}
	}
	for(i = floating; i < count; i++) {
		_SaveCubesEmit(&data->draw, i, i + 1);
	}
	if(grid->menu) {
		data->menu = s->menu;
		data->menuFocus = grid->menuFocus;
	}
	_SaveCubesMenu(&data->menu, data->menuFocus, grid, &data->motion);
	if(grid->message) {
		memcpy(data->message, s->message, sizeof(data->message));
		data->message[sizeof(data->message) - 1] = '\0';
	}
	_SaveCubesMessage(data->message, data->motion.messageAlpha);
	if(chrome > 0.0f) {
		_DrawHintText(40, 454, s->hint[0], 0.46f, ALIGN_LEFT,
			_SaveCubesFaded(settingsInk, chrome));
		_DrawHintText(600, 454, s->hint[1], 0.46f, ALIGN_RIGHT,
			_SaveCubesFaded(settingsQuiet, chrome));
	}
	drawInit();
}

uiDrawObj_t* DrawSaveCubesPage(const uiSaveCubesPageSnapshot_t *snapshot)
{
	drawSaveCubesEvent_t *data = memalign(32, sizeof(*data));
	uiDrawObj_t *event = calloc(1, sizeof(*event));

	if(data == NULL || event == NULL) {
		free(data);
		free(event);
		return NULL;
	}
	memset(data, 0, sizeof(*data));
	data->snapshot = *snapshot;
	event->type = EV_SAVE_CUBES;
	event->data = data;
	return event;
}

void DrawUpdateSaveCubesPage(uiDrawObj_t *page,
	const uiSaveCubesPageSnapshot_t *snapshot)
{
	if(page == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(!page->disposed && page->type == EV_SAVE_CUBES && page->data != NULL) {
		((drawSaveCubesEvent_t*)page->data)->snapshot = *snapshot;
	}
	LWP_MutexUnlock(_videomutex);
}

/* ------------------------------------------------------------------------
 * The File Browser (swiss.c's renderFileList): two panes over Memory Cards'
 * graph paper, each a folder's rows with a cube for what each entry is, the
 * focused entry's banner and name in the info bar below. swiss.c writes a
 * uiFilesSnapshot_t with every string fitted; this only draws it. The page
 * covers the stage, so the Home cube shows only as Memory Cards' backdrop
 * hands it over.
 * --------------------------------------------------------------------- */
typedef struct {
	uiFilesSnapshot_t snapshot;	/* first: its banner stays aligned */
	float seconds;			/* since it opened */
	float leave;			/* since B, or below 0 */
	uiMotionSpring_t focus[UI_FILES_PANES];	/* each focus bar, in rows */
	uiMotionSpring_t emphasis;	/* 0 the left pane focused, 1 the right */
	uiMotionSpring_t menuBar;	/* the box's focus, in items */
	float menuSince;		/* since the box opened or closed */
	uint16_t menuSerial;
	bool menuOpen;
	float content;			/* what the panes hold: Y fades it out and in */
	float messageSince;		/* since the message came */
	uint16_t messageSerial;
	uint16_t listing[UI_FILES_PANES];
	bool started;
	GXTexObj picture;
} drawFilesEvent_t;

/* A flat vertex in exactly color: the cubes' colors that keep their hue
 * under every Menu Color go through here, the others through
 * _FilesColor first. */
static void _FilesVertex(float x, float y, GXColor color)
{
	GX_Position3f32(x, y, 0.0f);
	GX_Color4u8(color.r, color.g, color.b, color.a);
	GX_TexCoord2f32(0.0f, 0.0f);
}

static void _FilesQuad(float x0, float y0, float x1, float y1, float x2,
	float y2, float x3, float y3, GXColor color)
{
	_FilesVertex(x0, y0, color);
	_FilesVertex(x1, y1, color);
	_FilesVertex(x2, y2, color);
	_FilesVertex(x3, y3, color);
}

static void _FilesRect(float x, float y, float width, float height, GXColor color)
{
	_FilesQuad(x, y, x + width, y, x + width, y + height, x, y + height, color);
}

/* color scaled toward black (k < 1) or white (k > 1), at alpha, through
 * Menu Color when it follows it. */
static GXColor _FilesColor(GXColor color, float k, float alpha, bool follows)
{
	float c[3] = {color.r, color.g, color.b};
	int i;

	for(i = 0; i < 3; i++) {
		c[i] = k <= 1.0f ? c[i] * k : c[i] + (255.0f - c[i]) * (k - 1.0f);
		c[i] = c[i] > 255.0f ? 255.0f : c[i];
	}
	color.r = (u8)c[0];
	color.g = (u8)c[1];
	color.b = (u8)c[2];
	color.a = (u8)((float)color.a * alpha + 0.5f);
	if(follows) {
		UIColor_Apply(&color.r, &color.g, &color.b);
	}
	return color;
}

/* An eighth of a turn at a time, for the disc's ring. */
static const float filesOctagon[8][2] = {
	{1.0f, 0.0f}, {0.7071f, 0.7071f}, {0.0f, 1.0f}, {-0.7071f, 0.7071f},
	{-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}
};

/* How many vertices each kind's emblem takes: the cube's three faces are
 * 12 more, a halo 4. */
static int _FilesEmblemVertices(int kind)
{
	switch(kind) {
		case UI_FILES_KIND_DISC:
		case UI_FILES_KIND_DISC_COMPRESSED:
			return 32;
		case UI_FILES_KIND_MUSIC:
		case UI_FILES_KIND_TEXT:
			return 12;
		case UI_FILES_KIND_FOLDER:
		case UI_FILES_KIND_PROGRAM_FOLDER:
		case UI_FILES_KIND_FIRMWARE:
		case UI_FILES_KIND_PICTURE:
			return 8;
		default:
			return 4;
	}
}

/* A row's cube, size px across its front, centred at (cx, cy): front, top
 * and side as three flat quads and the kind's emblem on the front, all
 * flat, so no square roots and no texture. Folders and ".." turn with Menu
 * Color; programs stay teal, firmware amber, discs silver with a ring that
 * turns. */
static void _FilesCube(int kind, float cx, float cy, float size, float alpha,
	bool halo)
{
	static const GXColor base[UI_FILES_KINDS] = {
		{150, 160, 210, 255},	/* .. */
		{122, 104, 224, 255},	/* folder */
		{46, 150, 160, 255},	/* program folder */
		{226, 228, 240, 255},	/* disc */
		{226, 228, 240, 150},	/* compressed disc */
		{46, 150, 160, 255},	/* program */
		{240, 176, 72, 255},	/* firmware */
		{236, 120, 170, 255},	/* music */
		{90, 150, 230, 255},	/* picture */
		{232, 232, 238, 255},	/* text */
		{150, 154, 170, 255}	/* other */
	};
	const GXColor light = {245, 242, 255, 255}, dark = {40, 36, 60, 255};
	bool follows = kind == UI_FILES_KIND_PARENT || kind == UI_FILES_KIND_FOLDER ||
		kind == UI_FILES_KIND_PICTURE || kind == UI_FILES_KIND_TEXT ||
		kind == UI_FILES_KIND_OTHER;
	float d = size * 0.3f, u = size / 13.0f;
	float x = cx - 0.5f * (size + d), y = cy - 0.5f * (size - d);
	GXColor front, top, side, mark;
	int i;

	if(kind < 0 || kind >= UI_FILES_KINDS || !(alpha > 0.0f)) {
		return;
	}
	front = _FilesColor(base[kind], kind == UI_FILES_KIND_PARENT ? 0.45f : 1.0f,
		alpha, follows);
	top = _FilesColor(base[kind], 1.35f, alpha, follows);
	side = _FilesColor(base[kind], 0.62f, alpha, follows);
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, (halo ? 4 : 0) + 12 + _FilesEmblemVertices(kind));
	if(halo) {
		_FilesRect(x - 3.0f, y - d - 3.0f, size + d + 6.0f, size + d + 6.0f,
			_FilesColor((GXColor) {255, 255, 255, 46}, 1.0f, alpha, false));
	}
	_FilesRect(x, y, size, size, front);
	_FilesQuad(x, y, x + d, y - d, x + size + d, y - d, x + size, y, top);
	_FilesQuad(x + size, y, x + size + d, y - d, x + size + d, y + size - d,
		x + size, y + size, side);
	switch(kind) {
		case UI_FILES_KIND_PARENT:
			mark = _FilesColor(light, 1.0f, alpha, true);
			_FilesQuad(x + 6.5f * u, y + 2.5f * u, x + 10.5f * u, y + 9.0f * u,
				x + 2.5f * u, y + 9.0f * u, x + 6.5f * u, y + 2.5f * u, mark);
			break;
		case UI_FILES_KIND_FOLDER:
		case UI_FILES_KIND_PROGRAM_FOLDER:
			mark = kind == UI_FILES_KIND_FOLDER ? _FilesColor(light, 1.0f, alpha, false) :
				_FilesColor((GXColor) {210, 250, 255, 255}, 1.0f, alpha, false);
			_FilesRect(x + 2.5f * u, y + 3.0f * u, 3.5f * u, 1.8f * u, mark);
			if(kind == UI_FILES_KIND_FOLDER) {
				_FilesRect(x + 2.5f * u, y + 4.5f * u, 8.0f * u, 5.5f * u, mark);
			}
			else {
				_FilesQuad(x + 4.5f * u, y + 5.0f * u, x + 10.0f * u, y + 7.5f * u,
					x + 4.5f * u, y + 10.0f * u, x + 4.5f * u, y + 5.0f * u, mark);
			}
			break;
		case UI_FILES_KIND_DISC:
		case UI_FILES_KIND_DISC_COMPRESSED:
			mark = _FilesColor((GXColor) {122, 104, 224, base[kind].a}, 1.0f, alpha, true);
			for(i = 0; i < 8; i++) {
				const float *a = filesOctagon[i], *b = filesOctagon[(i + 1) % 8];
				float mx = x + 6.5f * u, my = y + 6.5f * u, ro = 4.6f * u, ri = 1.8f * u;

				_FilesQuad(mx + a[0] * ro, my + a[1] * ro, mx + b[0] * ro, my + b[1] * ro,
					mx + b[0] * ri, my + b[1] * ri, mx + a[0] * ri, my + a[1] * ri, mark);
			}
			break;
		case UI_FILES_KIND_PROGRAM:
			_FilesQuad(x + 4.0f * u, y + 3.0f * u, x + 10.0f * u, y + 6.5f * u,
				x + 4.0f * u, y + 10.0f * u, x + 4.0f * u, y + 3.0f * u,
				_FilesColor(light, 1.0f, alpha, false));
			break;
		case UI_FILES_KIND_FIRMWARE:
			mark = _FilesColor((GXColor) {80, 46, 0, 255}, 1.0f, alpha, false);
			_FilesQuad(x + 7.5f * u, y + 1.8f * u, x + 3.5f * u, y + 7.5f * u,
				x + 6.8f * u, y + 7.5f * u, x + 7.5f * u, y + 1.8f * u, mark);
			_FilesQuad(x + 6.2f * u, y + 5.5f * u, x + 9.5f * u, y + 5.5f * u,
				x + 5.5f * u, y + 11.2f * u, x + 6.2f * u, y + 5.5f * u, mark);
			break;
		case UI_FILES_KIND_MUSIC:
			mark = _FilesColor(light, 1.0f, alpha, false);
			_FilesRect(x + 7.5f * u, y + 3.0f * u, 1.4f * u, 6.5f * u, mark);
			_FilesRect(x + 4.5f * u, y + 8.0f * u, 4.4f * u, 3.0f * u, mark);
			_FilesRect(x + 7.5f * u, y + 3.0f * u, 3.2f * u, 1.4f * u, mark);
			break;
		case UI_FILES_KIND_PICTURE:
			mark = _FilesColor(light, 1.0f, alpha, false);
			_FilesQuad(x + 2.5f * u, y + 10.5f * u, x + 6.0f * u, y + 5.0f * u,
				x + 10.5f * u, y + 10.5f * u, x + 2.5f * u, y + 10.5f * u, mark);
			_FilesRect(x + 8.5f * u, y + 2.5f * u, 2.0f * u, 2.0f * u, mark);
			break;
		case UI_FILES_KIND_TEXT:
			mark = _FilesColor(dark, 1.0f, alpha, false);
			for(i = 0; i < 3; i++) {
				_FilesRect(x + 3.0f * u, y + (3.5f + 2.5f * i) * u, 7.0f * u, 1.2f * u, mark);
			}
			break;
		default:
			_FilesRect(x + 5.5f * u, y + 5.5f * u, 2.0f * u, 2.0f * u,
				_FilesColor(dark, 1.0f, alpha, false));
			break;
	}
	GX_End();
}

/* An outline px wide, as a box's edge without its fill. */
static void _FilesOutline(float x, float y, float width, float height,
	float line, GXColor color)
{
	drawInit();
	_SetupRasterColor();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 16);
		_putFlatRect(x, y, width, line, color);
		_putFlatRect(x, y + height - line, width, line, color);
		_putFlatRect(x, y + line, line, height - 2.0f * line, color);
		_putFlatRect(x + width - line, y + line, line, height - 2.0f * line, color);
	GX_End();
}

#define FILES_CHIP_W UI_FILES_CHIP_W
#define FILES_CHIP_H 16

/* Everything but the words and the banner: the buttons, the boxes, the
 * focus bars, the cubes and the scroll tracks. alpha is the page's chrome;
 * content what of it the panes and the info bar hold (Y fades it out and
 * in); emphasis 0 when the left pane has the focus, 1 the right; focusY
 * each focus bar's row, sprung; lit the loading cell that is lit. */
static void _FilesShapes(const uiFilesSnapshot_t *s, const uiFilesLayout_t *layout,
	float alpha, float content, float emphasis, const float focusY[UI_FILES_PANES],
	int lit, float ghost, float flash)
{
	const float inner = alpha * content;
	const GXColor white = _SaveCubesFaded((GXColor) {255, 255, 255, 255}, inner);
	const GXColor shadow = _SaveCubesFaded((GXColor) {0, 0, 0, 200}, inner);
	const GXColor quiet = _SaveCubesFaded((GXColor) {173, 187, 216, 255}, alpha);
	const GXColor chip = _SaveCubesFaded((GXColor) {196, 177, 255, 56}, inner);
	int p, i;

	for(p = 0; p < UI_FILES_PANES; p++) {
		const uiFilesPaneSnapshot_t *pane = &s->pane[p];
		const uiFilesRect_t *box = &layout->pane[p];
		const uiFilesRect_t *button = &layout->button[p];
		float active = p == UI_FILES_RIGHT ? emphasis : 1.0f - emphasis;
		float fill = (200.0f + 36.0f * active) / 255.0f;
		float edge = 0.45f + 0.55f * active;
		bool track = pane->count > UI_FILES_ROWS;

		_SaveCubesBox(button->x0, button->y0, button->x1 - button->x0,
			button->y1 - button->y0,
			_SaveCubesFaded((GXColor) {18, 27, 91, 180}, alpha),
			_SaveCubesFaded((GXColor) {0, 0, 0, 200}, alpha), quiet, 1.0f);
		if(pane->source) {
			_SaveCubesBar(box->x0 + 2 + pane->deviceWidth + 10, UI_FILES_DEVICE_Y - FILES_CHIP_H / 2,
				FILES_CHIP_W, FILES_CHIP_H, chip);
		}
		if(pane->free[0] != '\0') {
			_SaveCubesBox(box->x1 - pane->freeWidth, UI_FILES_FREE_TOP, pane->freeWidth,
				UI_FILES_FREE_BOTTOM - UI_FILES_FREE_TOP, shadow, shadow, white, 2.0f);
		}
		if(pane->autoload) {
			_SaveCubesBar(box->x0 + 2 + pane->pathWidth + 8, UI_FILES_PATH_Y - 7,
				66, 14, chip);
		}
		_SaveCubesBox(box->x0, box->y0, box->x1 - box->x0, box->y1 - box->y0,
			_SaveCubesFaded((GXColor) {39, 53, 153, 255}, alpha * fill),
			_SaveCubesFaded((GXColor) {58, 31, 127, 255}, alpha * fill),
			_SaveCubesFaded((GXColor) {196, 186, 255, 255}, alpha * edge), 2.0f);
		if(pane->focusRow >= 0 && pane->rows > 0) {
			uiFilesRect_t row = UIFiles_RowRect(layout, p, 0);
			float y = row.y0 + UI_FILES_ROW_PITCH * focusY[p];

			if(active > 0.0f) {
				_SaveCubesBar(row.x0, y, row.x1 - row.x0, UI_FILES_ROW_HEIGHT,
					_SaveCubesFaded((GXColor) {70, 92, 200, 230}, inner * active));
			}
			if(active < 1.0f) {
				_FilesOutline(row.x0, y, row.x1 - row.x0, UI_FILES_ROW_HEIGHT, 1.0f,
					_SaveCubesFaded((GXColor) {196, 186, 255, 170}, inner * (1.0f - active)));
			}
		}
		for(i = 0; i < pane->rows && i < UI_FILES_ROWS; i++) {
			const uiFilesRowSnapshot_t *row = &pane->row[i];
			bool focused = (row->flags & UI_FILES_ROW_FOCUS) != 0u;
			float rowAlpha = inner * ((row->flags & UI_FILES_ROW_HIDDEN) ? 0.5f : 1.0f) *
				(pane->reading ? 0.6f : 1.0f);
			uiFilesRect_t rect = UIFiles_RowRect(layout, p, i);

			/* Where a copy will land, pulsing, and a copy that just
			 * landed, flashing: a light fill with a white edge, unlike
			 * the focus's outline. */
			if(row->flags & (UI_FILES_ROW_GHOST | UI_FILES_ROW_FLASH)) {
				float lit = (row->flags & UI_FILES_ROW_GHOST) ? ghost : flash;

				if(row->flags & UI_FILES_ROW_GHOST) {
					rowAlpha *= ghost;
				}
				if(lit > 0.0f) {
					_SaveCubesBox(rect.x0, rect.y0, rect.x1 - rect.x0, UI_FILES_ROW_HEIGHT,
						_SaveCubesFaded((GXColor) {196, 186, 255, 108}, inner * lit),
						_SaveCubesFaded((GXColor) {196, 186, 255, 108}, inner * lit),
						_SaveCubesFaded((GXColor) {255, 255, 255, 215}, inner * lit), 2.0f);
				}
			}
			_FilesCube(row->kind, UIFiles_CubeX(layout, p),
				0.5f * (rect.y0 + rect.y1), focused ? 16.0f : 13.0f, rowAlpha,
				focused && active > 0.5f);
		}
		if(track) {
			const uiFilesRect_t *t = &layout->track[p];
			int height = t->y1 - t->y0;
			int thumb = height * UI_FILES_ROWS / pane->count;
			int travel;

			if(thumb < 12) thumb = 12;
			travel = (int)((int64_t)(height - thumb) * pane->first /
				(pane->count - UI_FILES_ROWS));
			_SaveCubesBar(t->x0, t->y0, t->x1 - t->x0, height,
				_SaveCubesFaded((GXColor) {34, 42, 65, 255}, inner));
			_SaveCubesBar(t->x0, t->y0 + travel, t->x1 - t->x0, thumb,
				_SaveCubesFaded((GXColor) {196, 177, 255, 255}, inner));
		}
		if(pane->reading) {
			/* Three cells lit one after another under the words, on a band
			 * of the box's own blue. With the chrome, not the contents: a
			 * read after Y shows while the swapped-out rows are gone. */
			_SaveCubesBar(box->x0 + 4, UI_FILES_MESSAGE_Y - 18, box->x1 - box->x0 - 8, 52,
				_SaveCubesFaded((GXColor) {18, 27, 91, 230}, alpha));
			for(i = 0; i < 3; i++) {
				_SaveCubesBar(layout->mid[p] - 17 + 12 * i, UI_FILES_MESSAGE_Y + 18, 10, 6,
					_SaveCubesFaded((GXColor) {196, 177, 255, 255},
					alpha * (i == lit ? 1.0f : 0.35f)));
			}
		}
	}
	_SaveCubesBox(layout->info.x0, layout->info.y0, layout->info.x1 - layout->info.x0,
		layout->info.y1 - layout->info.y0,
		_SaveCubesFaded((GXColor) {39, 53, 153, 220}, alpha),
		_SaveCubesFaded((GXColor) {58, 31, 127, 220}, alpha),
		_SaveCubesFaded((GXColor) {196, 186, 255, 255}, alpha), 2.0f);
	if(!s->hasBanner) {
		_FilesCube(s->infoKind, 0.5f * (layout->picture.x0 + layout->picture.x1),
			0.5f * (layout->picture.y0 + layout->picture.y1), 26.0f, inner, false);
	}
	if(s->chip[0] != '\0') {
		_SaveCubesBar(layout->infoTextX + s->titleWidth + 10, 381 - FILES_CHIP_H / 2,
			FILES_CHIP_W, FILES_CHIP_H, chip);
	}
	if(s->size[0] != '\0') {
		_SaveCubesBox(layout->sizeBox.x0, layout->sizeBox.y0, s->sizeWidth,
			layout->sizeBox.y1 - layout->sizeBox.y0, shadow, shadow, white, 2.0f);
	}
}

/* The words: what the shapes leave for text. alpha and content as the
 * shapes'. The pane without the focus says its words in one pass, the
 * light weight, which halves what they cost. */
static void _FilesWords(const uiFilesSnapshot_t *s, const uiFilesLayout_t *layout,
	float alpha, float content, float emphasis, float ghost)
{
	const float inner = alpha * content;
	const GXColor white = _SaveCubesFaded((GXColor) {255, 255, 255, 255}, inner);
	const GXColor quiet = _SaveCubesFaded(settingsQuiet, inner);
	const GXColor accent = _SaveCubesFaded(settingsAccent, inner);
	int p, i, x;

	for(p = 0; p < UI_FILES_PANES; p++) {
		const uiFilesPaneSnapshot_t *pane = &s->pane[p];
		const uiFilesRect_t *box = &layout->pane[p];
		float active = p == UI_FILES_RIGHT ? emphasis : 1.0f - emphasis;
		GXColor name = active >= 0.5f ? white : quiet;
		bool track = pane->count > UI_FILES_ROWS;
		void (*text)(int, int, const char *, float, int, GXColor) =
			active >= 0.5f ? drawStringMedium : drawString;

		_DrawHintText(layout->mid[p], UI_FILES_BUTTON_Y + UI_FILES_BUTTON_HEIGHT / 2,
			pane->button, 0.46f, ALIGN_CENTER, _SaveCubesFaded((GXColor) {255, 255, 255, 255},
			alpha));
		text(box->x0 + 2, UI_FILES_DEVICE_Y, pane->device, pane->deviceScale,
			ALIGN_LEFT, name);
		if(pane->source) {
			drawStringMedium(box->x0 + 2 + pane->deviceWidth + 10 + FILES_CHIP_W / 2,
				UI_FILES_DEVICE_Y, "SOURCE", 0.34f, ALIGN_CENTER, accent);
		}
		if(pane->free[0] != '\0') {
			int middle = (UI_FILES_FREE_TOP + UI_FILES_FREE_BOTTOM) / 2;

			text(box->x1 - pane->freeWidth / 2, middle, pane->free, 0.50f,
				ALIGN_CENTER, white);
			if(!pane->readOnly) {
				text(box->x1 - pane->freeWidth - 8, middle + 1, "free", 0.42f,
					ALIGN_RIGHT, quiet);
			}
		}
		text(box->x0 + 2, UI_FILES_PATH_Y, pane->path, 0.46f, ALIGN_LEFT,
			accent);
		if(pane->autoload) {
			drawStringMedium(box->x0 + 2 + pane->pathWidth + 8 + 33, UI_FILES_PATH_Y,
				"AUTOLOAD", 0.34f, ALIGN_CENTER, accent);
		}
		text(box->x1 - 2, UI_FILES_PATH_Y, pane->counter, 0.46f,
			ALIGN_RIGHT, quiet);
		for(i = 0; i < pane->rows && i < UI_FILES_ROWS; i++) {
			const uiFilesRowSnapshot_t *row = &pane->row[i];
			uiFilesRect_t rect = UIFiles_RowRect(layout, p, i);
			int middle = (rect.y0 + rect.y1) / 2;
			float rowAlpha;
			GXColor ink;

			/* Rows under "Reading..."'s band are hidden, as their cubes are. */
			if(pane->reading && rect.y1 > UI_FILES_MESSAGE_Y - 18 &&
					rect.y0 < UI_FILES_MESSAGE_Y + 34) {
				continue;
			}
			rowAlpha = ((row->flags & UI_FILES_ROW_HIDDEN) ? 0.5f : 1.0f) *
				(pane->reading ? 0.6f : 1.0f) * ((row->flags & UI_FILES_ROW_GHOST) ? ghost : 1.0f);
			ink = (row->flags & UI_FILES_ROW_FOCUS) && active >= 0.5f ?
				(GXColor) {255, 236, 170, 255} : (GXColor) {255, 255, 255, 255};

			text(UIFiles_NameX(layout, p), middle, row->name, row->scale,
				ALIGN_LEFT, _SaveCubesFaded(ink, inner * rowAlpha));
			if(row->meta[0] != '\0') {
				text(UIFiles_MetaRight(layout, p, track), middle, row->meta,
					0.44f, ALIGN_RIGHT, _SaveCubesFaded(settingsQuiet, inner * rowAlpha));
			}
		}
		if(pane->message[0][0] != '\0') {
			/* "Reading..." with its band, through a swap's fade. */
			float said = pane->reading ? alpha : inner;

			drawStringMedium(layout->mid[p], UI_FILES_MESSAGE_Y, pane->message[0], 0.56f,
				ALIGN_CENTER, _SaveCubesFaded((GXColor) {255, 255, 255, 255}, said));
			for(i = 1; i < UI_FILES_MESSAGE_LINES; i++) {
				drawStringMedium(layout->mid[p], UI_FILES_MESSAGE_Y + 24 * i,
					pane->message[i], 0.46f, ALIGN_CENTER, _SaveCubesFaded(settingsQuiet, said));
			}
		}
	}
	drawStringMedium(layout->infoTextX, 381, s->title, s->titleScale, ALIGN_LEFT, white);
	if(s->chip[0] != '\0') {
		drawStringMedium(layout->infoTextX + s->titleWidth + 10 + FILES_CHIP_W / 2, 381,
			s->chip, 0.34f, ALIGN_CENTER, accent);
	}
	x = layout->infoTextX;
	if(s->size[0] != '\0') {
		drawStringMedium(x + s->sizeWidth / 2, (layout->sizeBox.y0 + layout->sizeBox.y1) / 2,
			s->size, 0.50f, ALIGN_CENTER, white);
		x += s->sizeWidth + 12;
	}
	drawStringMedium(x, UI_FILES_LINE1_Y, s->line[0], 0.46f, ALIGN_LEFT, white);
	drawStringMedium(x, UI_FILES_LINE2_Y, s->line[1], 0.46f, ALIGN_LEFT,
		_SaveCubesFaded(s->warn ? (GXColor) {255, 190, 80, 255} : settingsQuiet, inner));
	_DrawHintText(layout->hintLeft, UI_FILES_HINT_Y, s->hint[0], 0.46f, ALIGN_LEFT,
		_SaveCubesFaded(settingsInk, alpha));
	_DrawHintText(layout->hintRight, UI_FILES_HINT_Y, s->hint[1], 0.46f, ALIGN_RIGHT,
		_SaveCubesFaded(settingsQuiet, alpha));
}

/* A box beside a row of its pane (a storage menu, Actions, a question):
 * Memory Cards' look, any number of items up to UI_FILES_MENU_MAX, greyed
 * ones grey, each item's letter in a chip at its right, the focus's bar at
 * item (sprung); Delete's edge is rose. It opens from scale about its
 * middle and fades. */
static void _FilesMenu(const uiFilesMenu_t *menu, const uiFilesLayout_t *layout,
	float alpha, float scale, float item)
{
	/* Opaque: it sits over rows of text. */
	const GXColor fill = _SaveCubesFaded((GXColor) {18, 27, 91, 255}, alpha);
	const GXColor edge = _SaveCubesFaded(menu->rose ? (GXColor) {255, 170, 186, 255} :
		(GXColor) {196, 186, 255, 255}, alpha);
	uiFilesRect_t row = UIFiles_RowRect(layout, menu->pane, menu->row);
	uiFilesBox_t box;
	float middleX, middleY, top;
	bool titled = menu->title[0] != '\0';
	int i;

	if(!(alpha > 0.0f) || menu->count == 0) {
		return;
	}
	UIFiles_MenuBox(layout, &row, menu->pane, menu->count, titled, menu->width, &box);
	top = (float)(titled ? box.titleY : box.y);
	middleX = (float)box.x + 0.5f * (float)box.width;
	middleY = 0.5f * (top + (float)(box.y + box.height));
#define MENU_X(x) (middleX + ((float)(x) - middleX) * scale)
#define MENU_Y(y) (middleY + ((float)(y) - middleY) * scale)
	if(titled) {
		_SaveCubesBox(MENU_X(box.x), MENU_Y(box.titleY), (float)box.width * scale,
			UI_FILES_MENU_TITLE * scale, fill, fill, edge, 2.0f);
		drawStringMedium((int)MENU_X(box.x + 12), (int)MENU_Y(box.titleY +
			UI_FILES_MENU_TITLE / 2), menu->title, 0.56f * scale, ALIGN_LEFT,
			_SaveCubesFaded((GXColor) {255, 255, 255, 255}, alpha));
	}
	_SaveCubesBox(MENU_X(box.x), MENU_Y(box.y), (float)box.width * scale,
		(float)box.height * scale, fill, fill, edge, 2.0f);
	_SaveCubesBar(MENU_X(box.x + 4), MENU_Y((float)box.y + 8.0f + UI_FILES_MENU_PITCH * item),
		(float)(box.width - 8) * scale, UI_FILES_MENU_PITCH * scale,
		_SaveCubesFaded((GXColor) {70, 92, 200, 230}, alpha));
	for(i = 0; i < menu->count && i < UI_FILES_MENU_MAX; i++) {
		GXColor ink = (menu->dim >> i) & 1u ? (GXColor) {120, 120, 140, 255} :
			i == menu->focus ? (GXColor) {255, 236, 170, 255} : (GXColor) {255, 255, 255, 255};

		drawStringMedium((int)MENU_X(box.x + 16), (int)MENU_Y((float)box.y + 8.0f +
			UI_FILES_MENU_PITCH * ((float)i + 0.5f)), menu->item[i], 0.56f * scale,
			ALIGN_LEFT, _SaveCubesFaded(ink, alpha));
		if(menu->letter[i] != '\0') {
			char letter[2] = {menu->letter[i], '\0'};
			float x = (float)(box.x + box.width - 30), y = (float)box.y + 12.0f +
				UI_FILES_MENU_PITCH * (float)i;

			_SaveCubesBar(MENU_X(x), MENU_Y(y), 18.0f * scale, 16.0f * scale,
				_SaveCubesFaded((GXColor) {70, 80, 130, 255}, alpha));
			drawStringMedium((int)MENU_X(x + 9.0f), (int)MENU_Y(y + 8.0f), letter,
				0.40f * scale, ALIGN_CENTER, _SaveCubesFaded(ink, alpha));
		}
	}
#undef MENU_X
#undef MENU_Y
}

/* The result of an operation: Memory Cards' maroon box over the middle of
 * the stage, as wide as measured (at least 320), with a second, smaller
 * line when there is one. */
static void _FilesMessage(const char message[2][UI_FILES_TEXT_CAPACITY], int measured,
	float alpha)
{
	float width = measured > 320 ? (float)measured : 320.0f;
	bool two = message[1][0] != '\0';

	_SaveCubesBox(320.0f - 0.5f * width, two ? 194.0f : 200.0f, width, two ? 62.0f : 50.0f,
		_SaveCubesFaded((GXColor) {120, 16, 36, 255}, alpha),
		_SaveCubesFaded((GXColor) {120, 16, 36, 255}, alpha),
		_SaveCubesFaded((GXColor) {255, 210, 220, 255}, alpha), 2.0f);
	drawStringMedium(320, two ? 215 : 225, message[0], 0.56f, ALIGN_CENTER,
		_SaveCubesFaded((GXColor) {255, 255, 255, 255}, alpha));
	if(two) {
		drawStringMedium(320, 238, message[1], 0.46f, ALIGN_CENTER,
			_SaveCubesFaded((GXColor) {255, 220, 228, 255}, alpha));
	}
}

static void _DrawFiles(uiDrawObj_t *evt)
{
	drawFilesEvent_t *data = (drawFilesEvent_t*)evt->data;
	const uiFilesSnapshot_t *s = &data->snapshot;
	uiMotionMode_t motion = _CurrentMotionMode();
	uiFilesLayout_t layout;
	uiFilesStage_t stage;
	float focusY[UI_FILES_PANES], emphasis, ghost, flash, content;
	int p, lit;

	if(!data->started) {
		for(p = 0; p < UI_FILES_PANES; p++) {
			UIMotion_SpringInit(&data->focus[p], s->pane[p].focusRow > 0 ?
				(float)s->pane[p].focusRow : 0.0f, 25.0f);
			data->listing[p] = s->pane[p].listing;
		}
		UIMotion_SpringInit(&data->emphasis, (float)s->active, 25.0f);
		UIMotion_SpringInit(&data->menuBar, (float)s->menu.focus, 25.0f);
		data->menuOpen = s->menu.open != 0u;
		data->menuSerial = s->menu.serial;
		data->menuSince = data->menuOpen ? 0.0f : 1.0f;
		data->messageSerial = s->messageSerial;
		data->messageSince = 1.0f;
		data->content = 1.0f;
		data->leave = -1.0f;
		data->started = true;
	}
	else {
		data->seconds += UIAnim_Delta();
		data->menuSince += UIAnim_Delta();
		data->messageSince += UIAnim_Delta();
	}
	if(s->messageSerial != data->messageSerial) {
		data->messageSerial = s->messageSerial;
		data->messageSince = 0.0f;
	}
	/* Y: the panes' contents go, and come back with the new sides. */
	data->content = content = UIFiles_SwapStep(data->content, s->swapping != 0u,
		UIAnim_Delta(), motion);
	/* The ghost row pulses (steady with less motion). */
	ghost = motion == UI_MOTION_FULL ? 0.84f + 0.16f * sinf(3.0f * data->seconds) :
		motion == UI_MOTION_REDUCED ? 0.9f : 1.0f;
	/* The loading cells: five steps a second, two with UI Motion Reduced,
	 * still with it Off, as the presentation card's. */
	lit = motion == UI_MOTION_OFF ? 0 :
		(int)(data->seconds * (motion == UI_MOTION_REDUCED ? 2.0f : 5.0f)) % 3;
	/* A copy that just landed flashes as its message comes. */
	flash = s->messageLeaving ? 0.0f : UIFiles_Flash(data->messageSince, motion);
	/* Another box, or this one closing: from now. */
	if((s->menu.open != 0u) != data->menuOpen ||
			(s->menu.open && s->menu.serial != data->menuSerial)) {
		if(s->menu.open) {
			UIMotion_SpringSnap(&data->menuBar, (float)s->menu.focus);
		}
		data->menuOpen = s->menu.open != 0u;
		data->menuSerial = s->menu.serial;
		data->menuSince = 0.0f;
	}
	if(s->leaving) {
		data->leave = data->leave < 0.0f ? 0.0f : data->leave + UIAnim_Delta();
	}
	for(p = 0; p < UI_FILES_PANES; p++) {
		float row = s->pane[p].focusRow > 0 ? (float)s->pane[p].focusRow : 0.0f;

		/* Another folder: the bar starts on its row. */
		if(data->listing[p] != s->pane[p].listing) {
			data->listing[p] = s->pane[p].listing;
			UIMotion_SpringSnap(&data->focus[p], row);
		}
		UIMotion_SpringRetarget(&data->focus[p], row, motion);
		focusY[p] = UIMotion_SpringUpdate(&data->focus[p], UIAnim_Delta(), motion);
	}
	UIMotion_SpringRetarget(&data->emphasis, (float)s->active, motion);
	emphasis = UIMotion_SpringUpdate(&data->emphasis, UIAnim_Delta(), motion);
	emphasis = emphasis < 0.0f ? 0.0f : emphasis > 1.0f ? 1.0f : emphasis;
	UIFiles_Stage(data->seconds, data->leave, motion, &stage);
	UIFiles_Layout(UIStage_Left(), UIStage_Right(), &layout);
	_SaveCubesBackdrop(stage.paper, stage.handover);
	if(stage.chrome > 0.0f) {
		_FilesShapes(s, &layout, stage.chrome, content, emphasis, focusY, lit,
			ghost, flash);
		/* Pictures can't fade: the banner comes with the words' second
		 * half, as Memory Cards' does. */
		if(s->hasBanner && stage.chrome * content >= 0.5f) {
			_SaveCubesPicture(&data->picture, s->banner, layout.picture.x0,
				layout.picture.y0, 96);
		}
		_FilesWords(s, &layout, stage.chrome, content, emphasis, ghost);
		if(s->menu.count > 0) {
			float menuAlpha, menuScale;

			UIMotion_SpringRetarget(&data->menuBar, (float)s->menu.focus, motion);
			UIFiles_MenuMotion(data->menuSince, data->menuOpen, motion, &menuAlpha,
				&menuScale);
			_FilesMenu(&s->menu, &layout, menuAlpha * stage.chrome, menuScale,
				UIMotion_SpringUpdate(&data->menuBar, UIAnim_Delta(), motion));
		}
		if(s->message[0][0] != '\0') {
			/* In over 0.10 s, out over 0.15 s, at once with UI Motion Off. */
			_FilesMessage(s->message, s->messageWidth, stage.chrome * (motion == UI_MOTION_OFF ? 1.0f :
				s->messageLeaving ? fmaxf(0.0f, 1.0f - data->messageSince / 0.15f) :
				fminf(1.0f, data->messageSince / 0.10f)));
		}
	}
	drawInit();
}

uiDrawObj_t* DrawFiles(const uiFilesSnapshot_t *snapshot)
{
	drawFilesEvent_t *data = memalign(32, sizeof(*data));
	uiDrawObj_t *event = calloc(1, sizeof(*event));

	if(data == NULL || event == NULL) {
		free(data);
		free(event);
		return NULL;
	}
	memset(data, 0, sizeof(*data));
	data->snapshot = *snapshot;
	DCFlushRange(data->snapshot.banner, sizeof(data->snapshot.banner));
	event->type = EV_FILES;
	event->data = data;
	return event;
}

uiDrawObj_t* DrawFilesSettled(const uiFilesSnapshot_t *snapshot)
{
	uiDrawObj_t *page = DrawFiles(snapshot);

	if(page != NULL) {
		/* Past every step of the opening. */
		((drawFilesEvent_t*)page->data)->seconds = 60.0f;
	}
	return page;
}

bool DrawUpdateFiles(uiDrawObj_t *page, const uiFilesSnapshot_t *snapshot)
{
	bool updated = false;

	if(page == NULL) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	if(!page->disposed && page->type == EV_FILES && page->data != NULL) {
		drawFilesEvent_t *data = (drawFilesEvent_t*)page->data;

		data->snapshot = *snapshot;
		DCFlushRange(data->snapshot.banner, sizeof(data->snapshot.banner));
		updated = true;
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

/* Before a folder is read: "Reading..." over that pane's dimmed rows, the
 * other pane as it was. False when page isn't the File Browser. */
bool DrawUpdateFilesReading(uiDrawObj_t *page, int which)
{
	bool updated = false;

	if(page == NULL) {
		return false;
	}
	LWP_MutexLock(_videomutex);
	if(!page->disposed && page->type == EV_FILES && page->data != NULL) {
		uiFilesPaneSnapshot_t *pane =
			&((drawFilesEvent_t*)page->data)->snapshot.pane[which];

		pane->reading = 1;
		snprintf(pane->message[0], sizeof(pane->message[0]), "Reading\205");
		pane->message[1][0] = '\0';
		updated = true;
	}
	LWP_MutexUnlock(_videomutex);
	return updated;
}

void DrawGetTextEntry(int mode, const char *label, void *src, int size) {
	
	print_debug("DrawGetTextEntry Modes: Alpha [%s] Numeric [%s] IP [%s] Masked [%s] File [%s]\n", mode & ENTRYMODE_ALPHA ? "Y":"N", mode & ENTRYMODE_NUMERIC ? "Y":"N",
																	mode & ENTRYMODE_IP ? "Y":"N", mode & ENTRYMODE_MASKED ? "Y":"N", mode & ENTRYMODE_FILE ? "Y":"N");
	char *text = calloc(1, size + 1);
	if(mode & (ENTRYMODE_ALPHA|ENTRYMODE_IP)) {
		strncpy(text, src, size);
	}
	else {
		u16 *src_int = (u16*)src;
		itoa(*src_int, text, 10);
	}
	print_debug("Text is [%s] size %i\n", text, size);
	
	int caret = strlen(text);
	int cur_row = 0;
	int cur_col = 0;
	int num_rows = 0;
	int num_per_row[5] = {0,0,0,0,0};	// number of keys per row
	int pos_for_row[5] = {0,0,0,0,0};	// X pos to start drawing keys from
	int grid_gap = 45;
	int num_txt_modes = 0;	// Number of modes the text entry chars will have, e.g. upper case, lowercase etc.
	char *txt_modes_str[] = {"lowercase", "UPPERCASE"};
	// char arrays to grab from, exact order is important
	char *ip_mode_chars = "123456789.0\b";
	char *num_mode_chars = "1234567890\b";
	char *txt_mode_chars_lower = "1234567890-=\bqwertyuiop[]\\asdfghjkl;'zxcvbnm,./`!@\a#$%";
	char *txt_mode_chars_upper = "1234567890_+\bQWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?^&*\a()~";
	char *txt_mode_file_chars_lower = "1234567890-=\bqwertyuiop[]asdfghjkl;'zxcvbnm.`!\a#$%";
	char *txt_mode_file_chars_upper = "1234567890_+\bQWERTYUIOP{}ASDFGHJKL^&ZXCVBNM,@~\a()%";
	
	int cur_txt_mode = 0;
	char *gridText = NULL;
	
	// IP mode
	if(mode & ENTRYMODE_IP) {
		// 1  2  3
		// 4  5  6
		// 7  8  9
		//[.] 0  [backspace]
		num_rows = 4;
		grid_gap = 15;
		num_per_row[0] = 3;
		pos_for_row[0] = 240;
		num_per_row[1] = 3;
		pos_for_row[1] = 240;
		num_per_row[2] = 3;
		pos_for_row[2] = 240;
		num_per_row[3] = 3;
		pos_for_row[3] = 240;
		gridText = ip_mode_chars;
	}
	
	// Number only
	if((mode & ENTRYMODE_NUMERIC) && !(mode & ENTRYMODE_ALPHA)) {
		// 1  2  3
		// 4  5  6
		// 7  8  9
		// 0  [backspace]
		num_rows = 4;
		grid_gap = 15;
		num_per_row[0] = 3;
		pos_for_row[0] = 240;
		num_per_row[1] = 3;
		pos_for_row[1] = 240;
		num_per_row[2] = 3;
		pos_for_row[2] = 240;
		num_per_row[3] = 2;
		pos_for_row[3] = 240;
		gridText = num_mode_chars;
	}
	
	// Alpha only
	else if(!(mode & ENTRYMODE_NUMERIC) && (mode & ENTRYMODE_ALPHA)) {
		// TODO if we ever have to.
	}
	
	// Alphanumeric (not file)
	else if((mode & (ENTRYMODE_NUMERIC | ENTRYMODE_ALPHA)) && !(mode & ENTRYMODE_IP) && !(mode & ENTRYMODE_FILE)) {
		/* Mode 0:
		 1234567890-=<\b aka backspace>
		 qwertyuiop[]\
		 asdfghjkl;'
		 zxcvbnm,./
		 `!@<\a aka space>#$%
		
		 Mode 1:
		 1234567890_+<\b aka backspace>
		 QWERTYUIOP{}|
		 ASDFGHJKL:"
		 ZXCVBNM<>?
		 ^&*<\a aka space>()~
		*/
		
		num_txt_modes = 2;
		num_rows = 5;
		grid_gap = 10;
		num_per_row[0] = 13;
		pos_for_row[0] = 40;
		num_per_row[1] = 13;
		pos_for_row[1] = 60;
		num_per_row[2] = 11;
		pos_for_row[2] = 100;
		num_per_row[3] = 10;
		pos_for_row[3] = 120;
		num_per_row[4] = 7;
		pos_for_row[4] = 160;
	}
	
	// Alphanumeric (file)
	else if(mode & ENTRYMODE_FILE) {
		/* Mode 0:
		 1234567890-=<\b aka backspace>
		 qwertyuiop[]\
		 asdfghjkl;'
		 zxcvbnm
		 .`!<\a aka space>#$%
		
		 Mode 1:
		 1234567890_+<\b aka backspace>
		 QWERTYUIOP{}
		 ASDFGHJKL^&
		 ZXCVBNM
		 ,@~<\a aka space>()%
		*/
		
		num_txt_modes = 2;
		num_rows = 5;
		grid_gap = 10;
		num_per_row[0] = 13;
		pos_for_row[0] = 40;
		num_per_row[1] = 12;
		pos_for_row[1] = 60;
		num_per_row[2] = 11;
		pos_for_row[2] = 100;
		num_per_row[3] = 7;
		pos_for_row[3] = 120;
		num_per_row[4] = 7;
		pos_for_row[4] = 160;
	}
	
	// Wait for any A or Left/Right presses to finish
	while ((padsButtonsHeld() & (BUTTON_A|BUTTON_LEFT|BUTTON_RIGHT))){ VIDEO_WaitVSync (); }
	uiDrawObj_t *container = NULL;
	while(1) {
		// Double box for extra darkness
		uiDrawObj_t *newPanel = DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 440);
		DrawAddChild(newPanel, DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 440));
		sprintf(txtbuffer, "%s - Please enter a value", label);
		DrawAddChild(newPanel, DrawStyledLabel(25, 74, txtbuffer, GetTextScaleToFitInWidth(txtbuffer, getVideoMode()->fbWidth-50), ALIGN_LEFT, defaultColor));

		// Draw the text entry box (TODO: mask chars if the mode says to do so)
		DrawAddChild(newPanel, DrawEmptyBox(40, 100, getVideoMode()->fbWidth-40, 140));
		DrawAddChild(newPanel, DrawStyledLabelWithCaret(320, 120, text, GetTextScaleToFitInWidth(text, getVideoMode()->fbWidth-90), ALIGN_CENTER, defaultColor, caret));
		DrawAddChild(newPanel, DrawHintLabel(320, 160, "L/R  Cursor    START  Accept    B  Discard", 0.75f, ALIGN_CENTER, defaultColor));

		// Alphanumeric has a little "mode" hint at the bottom (upper/lower case set switching)
		if(mode & ENTRYMODE_ALPHA) {
			gridText = cur_txt_mode == 0 ? 
				(mode & ENTRYMODE_FILE ? txt_mode_file_chars_lower : txt_mode_chars_lower)
				: 
				(mode & ENTRYMODE_FILE ? txt_mode_file_chars_upper : txt_mode_chars_upper);
			sprintf(txtbuffer, "X  Change to [%s], current mode is [%s]",
													txt_modes_str[(cur_txt_mode + 1 >= num_txt_modes ? 0 : cur_txt_mode + 1)], txt_modes_str[cur_txt_mode]);
			DrawAddChild(newPanel, DrawHintLabel(25, 427, txtbuffer, 0.65f, ALIGN_LEFT, defaultColor));
		}
		
		// Draw the grid
		int y = 200, dx = 0, dy = 0, i = 0;
		int button_height = 30;
		for(dy = 0; dy < num_rows; dy++) {
			int x = pos_for_row[dy];
			for(dx = 0; dx < num_per_row[dy]; dx++) {
				// Space and Backspace are special, draw them in double the width with smaller fonts
				bool isSpecial = (gridText[i] == '\a' || gridText[i] == '\b');
				int button_width = isSpecial ? (60 + grid_gap) : 30;
				DrawAddChild(newPanel, DrawEmptyColouredBox(x, y, x+button_width, y+button_height, cur_col == dx && cur_row == dy ? (GXColor) {96,107,164,GUI_MSGBOX_ALPHA} : (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}));
				float fontSize = isSpecial ? 0.65f : 1.0f;
				if(isSpecial)
					sprintf(txtbuffer, "%s", gridText[i] == '\a' ? "Space" : "Y  Back");
				else
					sprintf(txtbuffer, "%c", gridText[i]);
				GXColor keyColor = cur_col == dx && cur_row == dy ? defaultColor : deSelectedColor;
				if(gridText[i] == '\b')
					DrawAddChild(newPanel, DrawHintLabel(x+(button_width/2), y+(button_height/2), txtbuffer, fontSize, ALIGN_CENTER, keyColor));
				else
					DrawAddChild(newPanel, DrawStyledLabel(x+(button_width/2), y+(button_height/2), txtbuffer, fontSize, ALIGN_CENTER, keyColor));
				x += (grid_gap + button_width);
				i++;
			}
			y += (grid_gap + button_height);
		}
		
		container = DrawRepublish(container, newPanel);
		
		while (!(padsButtonsHeld() & (BUTTON_L|BUTTON_R|BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_B|BUTTON_A|BUTTON_X|BUTTON_Y|BUTTON_START)))
			{ VIDEO_WaitVSync (); }
		u32 btns = padsButtonsHeld();
		// Key nav
		if(btns & BUTTON_DOWN) {
			cur_row = (cur_row + 1 >= num_rows ? 0 : cur_row + 1);
		}
		if(btns & BUTTON_UP) {
			cur_row = (cur_row == 0 ? num_rows - 1 : cur_row - 1);
		}
		if(btns & BUTTON_LEFT) {
			cur_col = (cur_col == 0 ? num_per_row[cur_row] - 1 : cur_col - 1);
		}
		if(btns & BUTTON_RIGHT) {
			cur_col = (cur_col + 1 >= num_per_row[cur_row] ? 0 : cur_col + 1);
		}
		// If we went off the end due to a row that has less than another
		if(cur_col >= num_per_row[cur_row]) cur_col = num_per_row[cur_row] - 1;
		// Key press handling
		if((btns & BUTTON_A) || (btns & BUTTON_Y)) {
			int char_pos = cur_col;
			for(i = 0; i < cur_row; i++)
				char_pos += num_per_row[i];
			char pressed = gridText[char_pos];
			// Handle normal character presses
			if(pressed != '\b' && !(btns & BUTTON_Y)) {
				if(caret < size && strlen(text) < size) {
					//print_debug("Pressed [%c]\n", pressed);
					if(pressed == '\a')
						pressed = ' ';
					// Shuffle everything forward (don't want overwrite functionality)
					for(i = size-1; i > caret; i--) {
						text[i] = text[i-1];
					}
					text[caret] = pressed;
					caret++;
				}
			}
			// Handle deletes via Y button or "backspace" button
			else if((btns & BUTTON_Y) || (pressed == '\b')) {
				// Delete a character from the caret
				if(caret-1 >= 0) {
					for(i = caret-1; i < size; i++) {
						text[i] = text[i+1];
						text[i+1] = '\0';
					}
					text[size] = '\0';
					if(caret > 0)
						caret --;
				}
			}
		}
		// Mode switching if the set allows it (only alpha does for upper/lower)
		if(btns & BUTTON_X) {
			if(num_txt_modes > 0) {
				cur_txt_mode = (cur_txt_mode + 1 >= num_txt_modes ? 0 : cur_txt_mode + 1);
			}
		}
		if(btns & BUTTON_L) {
			if(caret > 0) caret--;
		}
		if(btns & BUTTON_R) {
			if(caret < strlen(text)) caret++;
		}
		if(btns & BUTTON_B) {
			break;
		}
		if(btns & BUTTON_START) {
			if(mode & (ENTRYMODE_ALPHA|ENTRYMODE_IP)) {
				strcpy(src, text);
			}
			else {
				// TODO fix this stuff at some point, we're checking based on text size rather than max val of the data type.
				u16 *src_int = (u16*)src;
				*src_int = (u16)atoi(text);
			}
			break;
		}
		while (padsButtonsHeld() & (BUTTON_L|BUTTON_R|BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_B|BUTTON_A|BUTTON_X|BUTTON_Y|BUTTON_START))
			{ VIDEO_WaitVSync (); }
	}
	if(text) free(text);
	DrawDispose(container);
}


static void videoDrawEvent(uiDrawObj_t *videoEvent) {
	//print_debug("Draw event: %08X (type %s)\n", (u32)videoEvent, typeStrings[videoEvent->type]);
	drawInit();
	switch(videoEvent->type) {
		case EV_TEXOBJ:
			_DrawTexObj(videoEvent);
			break;
		case EV_IMAGE:
			_DrawImage(videoEvent);
			break;
		case EV_BACKGROUND:
			_DrawBackground(videoEvent);
			break;
		case EV_MSGBOX:
			_DrawMessageBox(videoEvent);
			break;
		case EV_PROGRESS:
			_DrawProgressBar(videoEvent);
			break;
		case EV_SELECTABLEBUTTON:
			_DrawSelectableButton(videoEvent);
			break;
		case EV_EMPTYBOX:
			_DrawEmptyBox(videoEvent);
			break;
		case EV_TRANSPARENTBOX:
			_DrawTransparentBox(videoEvent);
			break;
		case EV_VERTSCROLLBAR:
			_DrawVertScrollBar(videoEvent);
			break;
		case EV_STYLEDLABEL:
			_DrawStyledLabel(videoEvent);
			break;
		case EV_HOME:
			_DrawHome(videoEvent);
			break;
		case EV_DEVICESELECTOR:
			_DrawDeviceSelector(videoEvent);
			break;
		case EV_TOOLTIP:
			_DrawTooltip(videoEvent);
			break;
		case EV_TITLEBAR:
			_DrawTitleBar(videoEvent);
			break;
		case EV_GAMEFLOW:
			_DrawGameflow(videoEvent);
			break;
		case EV_SAVE_DETAILS:
			_DrawSaveDetails(videoEvent);
			break;
		case EV_PRESENTATION:
			_DrawPresentation(videoEvent);
			break;
		case EV_CHEATS:
			_DrawCheats(videoEvent);
			break;
		case EV_SETTINGS:
			_DrawSettingsPage(videoEvent);
			break;
		case EV_SETTINGSLIST:
			_DrawSettingsList(videoEvent);
			break;
		case EV_MEMORY_FOLDER:
			_DrawMemoryCardFolder(videoEvent);
			break;
		case EV_SETTINGSHELP:
			_DrawSettingsHelp(videoEvent);
			break;
		case EV_SAVES:
			_DrawSaves(videoEvent);
			break;
		case EV_SAVE_CUBES:
			_DrawSaveCubes(videoEvent);
			break;
		case EV_FILES:
			_DrawFiles(videoEvent);
			break;
		case EV_MANUAL:
			_DrawManual(videoEvent);
			break;
		default:
			break;
	}
	if(videoEvent->child != NULL) {
		videoDrawEvent(videoEvent->child);
	}
}

static void markDisposed(uiDrawObj_t *evt)
{
	if(evt && evt->child && !evt->child->disposed) {
		markDisposed(evt->child);
	}
	if(evt) {
		evt->disposed = true;
	}
}

static void copyDisplayFrame(void *framebuffer)
{
	/* Copy-clear obeys the EFB write masks. The 2D widget pipeline disables
	 * depth writes; leaving that mask in place preserves the previous cube
	 * depth and punches old silhouettes into its next animated position. */
	GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_TRUE);
	GX_SetColorUpdate(GX_ENABLE);
	GX_CopyDisp(framebuffer, GX_TRUE);
}

/* Each layer shows the color its list has focused, else the one its page
 * was drawn with, else its setting. The backdrop and the waves follow the
 * menus' color unless they are set to one of their own. */
static void _SelectFrameColors(void)
{
	int setting[UI_COLOR_LAYERS] = {swissSettings.uiColor,
		swissSettings.uiBackdropColor, swissSettings.uiWaveColor};

	for(int i = 0; i < UI_COLOR_LAYERS; i++) {
		if(menuColorPreview[i] >= 0) {
			setting[i] = menuColorPreview[i];
		}
		else if(menuColorPinned[i] >= 0) {
			setting[i] = menuColorPinned[i];
		}
	}
	frameColors[UI_COLOR_LAYER_MENU] = setting[UI_COLOR_LAYER_MENU];
	for(int i = UI_COLOR_LAYER_BACKDROP; i < UI_COLOR_LAYERS; i++) {
		frameColors[i] = UIColor_Layer(setting[i], setting[UI_COLOR_LAYER_MENU]);
	}
	UIColor_Select(frameColors[UI_COLOR_LAYER_MENU]);
	IndigoBackground_SetColors(frameColors);
}

/* Settings, the cheats and Memory Cards are opaque pages over the whole
 * stage (_PagePanel, or the cube screen's own backdrop), so while one is up
 * nothing drawn before it shows. */
static bool _FrameCovered(uiDrawObjQueue_t *queue)
{
	for(; queue != NULL; queue = queue->next) {
		const uiDrawObj_t *event = queue->event;
		if(!event->disposed && (event->type == EV_SETTINGS ||
			event->type == EV_CHEATS || event->type == EV_SAVES ||
			event->type == EV_SAVE_CUBES || event->type == EV_FILES ||
			event->type == EV_MANUAL)) {
			return true;
		}
	}
	return false;
}

static void *videoUpdate(void *videoEventQueue) {
	GX_SetCurrentGXThread();
	
	//int frames = 0;
	//int framerate = 0;
	//u32 lasttime = gettick();
	while(video_thread == LWP_GetSelf()) {
		whichfb ^= 1;
		UIAnim_BeginFrame();
		UI_PERF_BEGIN(frameWorkStart);
		//frames++;
		LWP_MutexLock(_videomutex);
#if UI_PERF_CAPTURE
		_PerfGpuFrameStart();
#endif
		/* One color per layer a frame, taken with the page it goes with:
		 * every emitter recolors with the menus', the backdrop and its waves
		 * with their own. */
		_SelectFrameColors();
		UIScene_Update(UIAnim_Delta(), _CurrentMotionMode());
		/* Sample once before EV_BACKGROUND so the cube and later title bar read
		 * the exact same numeric civil-time frame. */
		_UpdateSystemInstrument();
		/* The Library emblem mirrors this frame's controller. PAD reads only
		 * copy the last post-retrace scan; no SI transfer happens here. */
		padInstrument = (indigoPadFrame_t) {
			true, padsStickX(), padsStickY(), padsSubStickX(), padsSubStickY(),
			padsButtonsHeld()
		};
		// Mark events recursively as disposed
		uiDrawObjQueue_t *videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
		while(videoEventQueueEntry != NULL) {
			uiDrawObj_t *videoEvent = videoEventQueueEntry->event;
			if(videoEvent->disposed) {
				markDisposed(videoEvent);
			}
			videoEventQueueEntry = videoEventQueueEntry->next;
		}
		// Free events
		videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
		while(videoEventQueueEntry != NULL) {
			uiDrawObj_t *videoEvent = videoEventQueueEntry->event;
			// Remove any video events marked for disposal
			if(videoEvent->disposed) {
				disposeEvent(videoEvent);
				videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
				continue;
			}
			videoEventQueueEntry = videoEventQueueEntry->next;
		}
		
		backgroundCovered = _FrameCovered((uiDrawObjQueue_t*)videoEventQueue);
		GXRModeObj *vmode = getVideoMode();
		if(vmode->field_rendering) {
			GX_SetViewportJitter(0.0f, 0.0f, vmode->fbWidth, vmode->efbHeight, 0.0f, 1.0f, VIDEO_GetNextField());
		}
		/* One shape for the whole frame, whatever Settings does meanwhile. */
		UIStage_SetWide(swissSettings.menuWidescreen);
		// Draw out every event
		videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
		while(videoEventQueueEntry != NULL) {
			uiDrawObj_t *videoEvent = videoEventQueueEntry->event;
			videoDrawEvent(videoEvent);
			videoEventQueueEntry = videoEventQueueEntry->next;
		}
		if(launchFading) {
			_DrawLaunchFade();
		}
		/* During the short boot reveal, veil the already-published legacy widgets
		 * and redraw the cube above them. Animations Off skips this pass entirely. */
		if(sceneRenderingEnabled && UIScene_Frame()->introProgress < 1.0f) {
			int icons[UI_HOME_FACE_COUNT];

			_HomeFaceIcons(icons);
			drawInit();
			IndigoBackground_DrawBootOverlay(UIAnim_Seconds(),
				_CurrentMotionMode() == UI_MOTION_FULL, UIScene_Frame(),
				&systemInstrument.clock, icons);
			drawInit();
		}
#if UI_PERF_CAPTURE
		else if(sceneRenderingEnabled) {
			_DrawPerfOverlay();
		}
#endif
		
		//Copy EFB->XFB
		if(vmode->copy_interlaced == GX_COPY_INTERLACED) {
			GX_SetDispCopyFrame2Field(GX_COPY_INTERLACED ^ VIDEO_GetNextField());
		}
		u16 width = vmode->fbWidth;
		u16 height = GX_SetDispCopyYScale(getYScaleFactor(vmode->efbHeight, vmode->xfbHeight));
		copyDisplayFrame(xfb[whichfb]);
		GX_DrawDone();
		UI_PERF_END(UI_PERF_METRIC_FRAME_WORK, frameWorkStart);
#if UI_PERF_CAPTURE
		_PerfGpuFrameEnd();
#endif

		LWP_MutexUnlock(_videomutex);
		VIDEO_SetNextFramebuffer(xfb[whichfb]);
		VIDEO_ConfigurePan(0, 0, width, height);
		VIDEO_Flush();
		VIDEO_WaitForFlush();
	}
	return NULL;
}

void DrawAddChild(uiDrawObj_t *parent, uiDrawObj_t *child)
{
	LWP_MutexLock(_videomutex);
	//print_debug("Added a new child event %08X (type %s)\n", (u32)child, typeStrings[child->type]);
	uiDrawObj_t *current = parent;
    while (current->child != NULL) {
        current = current->child;
    }
	current->child = child;
	child->disposed = false;
	//print_debug("Add child %08X (type %s) to parent %08X (type %s)\n",
	//	(u32)child, typeStrings[child->type], (u32)parent, typeStrings[parent->type]);
	LWP_MutexUnlock(_videomutex);
}

void DrawWithVideoLocked(void (*change)(void *context), void *context)
{
	LWP_MutexLock(_videomutex);
	change(context);
	LWP_MutexUnlock(_videomutex);
}

uiDrawObj_t* DrawPublish(uiDrawObj_t *evt)
{
	LWP_MutexLock(_videomutex);
	uiDrawObj_t* event = addVideoEvent(evt);
	LWP_MutexUnlock(_videomutex);
	return event;
}

uiDrawObj_t* DrawRepublish(uiDrawObj_t *old, uiDrawObj_t *new)
{
	LWP_MutexLock(_videomutex);
	if (old) {
		old->disposed = true;
	}
	uiDrawObj_t* event = addVideoEvent(new);
	LWP_MutexUnlock(_videomutex);
	return event;
}

void DrawDiscard(uiDrawObj_t *evt)
{
	if(evt != NULL) {
		evt->disposed = true;
		clearNestedEvent(evt);
	}
}

void DrawDispose(uiDrawObj_t *evt)
{
	if(evt == NULL) {
		return;
	}
	LWP_MutexLock(_videomutex);
	evt->disposed = true;
	if(evt == menuColorPage) {
		menuColorPage = NULL;
		for(int i = 0; i < UI_COLOR_LAYERS; i++) {
			menuColorPinned[i] = -1;
			menuColorPreview[i] = -1;
		}
	}
	LWP_MutexUnlock(_videomutex);
}

void DrawInit(GXRModeObj *videoMode, bool black) {
	setVideoMode(videoMode);
	padsInit();
	init_font();
	init_textures();
	/* Every publish/update path enters this mutex, so it must exist before
	 * the first retained event becomes visible. */
	LWP_MutexInit(&_videomutex, false);
	if(!gameflowResetRegistered) {
		SYS_RegisterResetFunc(&gameflowResetInfo);
		gameflowResetRegistered = true;
	}
	UIAnim_Reset();
	memset(&systemInstrument, 0, sizeof(systemInstrument));
	systemInstrument.coreTemperature = -1;
	memcpy(systemInstrument.timeText, "--:--:--", 9u);
	UIPerf_Reset();
#if UI_PERF_CAPTURE
	GX_SetGPMetric(GX_PERF0_VERTICES, GX_PERF1_TEXELS);
#endif
	UIScene_Reset();
	sceneRenderingEnabled = !black;
	uiDrawObj_t *container = DrawContainer();
	if(!black) {
		DrawAddChild(container, DrawBackground());
		DrawAddChild(container, DrawTitleBar());
		buttonPanel = DrawHome();
		DrawAddChild(container, buttonPanel);
	}
	DrawPublish(container);
	LWP_CreateThread(&video_thread, videoUpdate, videoEventQueue, video_thread_stack, VIDEO_STACK_SIZE, VIDEO_PRIORITY);
}

/* Indigo's own background (indigo_background.c) covers the whole stage, so
 * a custom swiss/backdrop.tpl could never show: nothing is loaded. main.c
 * still calls this, as upstream's does. */
void DrawLoadBackdrop(DEVICEHANDLER_INTERFACE *device) {
	(void)device;
}

void DrawShutdown() {
	mutex_t mutex;
	lwp_t thread = video_thread;

	/* A launch ends on black: the ring completes while the screen fades
	 * out. Animations Off cuts to black as it always did. */
	if(launchActive && thread != LWP_THREAD_NULL &&
		_CurrentMotionMode() != UI_MOTION_OFF) {
		bool faded = false;
		int frames;

		LWP_MutexLock(_videomutex);
		UILaunch_Finish(&launchState);
		launchFading = true;
		LWP_MutexUnlock(_videomutex);
		for(frames = 0; frames < 30 && !faded; ++frames) {
			VIDEO_WaitVSync();
			LWP_MutexLock(_videomutex);
			faded = launchFade >= 1.0f;
			LWP_MutexUnlock(_videomutex);
		}
	}
	/* Cancel while the source device and shared mutex are still live. */
	DrawGameflowCancelPosters();
	if(gameflowResetRegistered) {
		SYS_UnregisterResetFunc(&gameflowResetInfo);
		gameflowResetRegistered = false;
	}
	video_thread = LWP_THREAD_NULL;
	if(thread != LWP_THREAD_NULL) {
		LWP_JoinThread(thread, NULL);
	}
	/* The last frame kept the launch's cover; nothing draws now. */
	launchActive = false;
	launchFading = false;
	launchFade = 0.0f;
	/* Cancellation above unpublished the pack while the mutex was live.
	 * With video readers joined, final arena disposal is intentionally
	 * lock-free and remains safe before the mutex is destroyed. */
	UIAssets_DisposeAfterVideoStop();
	UIStills_DisposeAfterVideoStop();
	UIDiscs_DisposeAfterVideoStop();
	mutex = _videomutex;
	_videomutex = LWP_MUTEX_NULL;
	if(mutex != LWP_MUTEX_NULL) {
		LWP_MutexDestroy(mutex);
	}
	GX_SetCurrentGXThread();
	unsetVideoMode();
}

/* Settings steps a video row's value without switching to it: while
 * deferred, DrawVideoMode leaves the mode as it is. */
static bool videoModeDeferred;

void DrawVideoModeDefer(bool defer)
{
	videoModeDeferred = defer;
}

void DrawVideoMode(GXRModeObj *videoMode)
{
	if(videoModeDeferred) {
		return;
	}
	LWP_MutexLock(_videomutex);
	if(getVideoMode() != videoMode) {
		setVideoMode(videoMode);
	}
	else {
		updateVideoMode(videoMode);
	}
	LWP_MutexUnlock(_videomutex);
}
