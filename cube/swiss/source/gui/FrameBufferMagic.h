/* -----------------------------------------------------------
      FrameBufferMagic.h - Framebuffer routines with GX
	      - by emu_kidid & sepp256

      Version 1.0 11/11/2009
        - Initial Code
   ----------------------------------------------------------- */
   
#ifndef FRAMEBUFFERMAGIC_H
#define FRAMEBUFFERMAGIC_H

#include <gccore.h>
#include "deviceHandler.h"
#include "ui_gameflow_detail.h"
#include "ui_gameflow.h"
#include "ui_home.h"
#include "ui_presentation.h"
#include "ui_color.h"
#include "ui_settings_layout.h"
#include "ui_save_cubes.h"
#include "ui_saves_details.h"
#include "ui_folder.h"
#include "ui_files.h"
#include "ui_manual.h"

#define D_WARN  0
#define D_INFO  1
#define D_FAIL  2
#define D_PASS  3
#define B_NOSELECT 0
#define B_SELECTED 1

#define BUTTON_COLOUR_INNER 0x2088207C
#define BUTTON_COLOUR_OUTER COLOR_SILVER

#define PROGRESS_BOX_WIDTH  600
#define PROGRESS_BOX_HEIGHT 125
#define PROGRESS_BOX_BOTTOMLEFT 0
#define PROGRESS_BOX_TOPLEFT 1
#define PROGRESS_BOX_FILES 2

#include "images_tpl.h"
#include "images.h"
#include "buttons_tpl.h"
#include "buttons.h"

typedef struct uiDrawObj {
    int type;
	void *data;
	struct uiDrawObj *child;
	bool disposed;
} uiDrawObj_t;

/* Enough records for the Grid layout's window, five rows of five
 * (UI_GAMEFLOW_LIBRARY_GRID_WINDOW); the carousels fill 7 of them. */
#define UI_GAMEFLOW_RENDER_SLOTS 25u
#define UI_GAMEFLOW_TITLE_LENGTH 96u
#define UI_GAMEFLOW_COMPANY_LENGTH 64u
#define UI_GAMEFLOW_FACTS_LENGTH 64u
/* Spotlight's description: from the card's descriptions file (up to 300
 * characters), else a disc banner's 128, which need not end in a NUL. */
#define UI_GAMEFLOW_DESCRIPTION_LENGTH 320u
/* ...shown in this many lines of Spotlight's column. */
#define UI_GAMEFLOW_DESCRIPTION_LINES 6u

#define UI_GAMEFLOW_CARD_VALID       (1u << 0)
#define UI_GAMEFLOW_CARD_HAS_BANNER  (1u << 1)
#define UI_GAMEFLOW_CARD_AUTOLOAD    (1u << 2)
#define UI_GAMEFLOW_CARD_HIDDEN      (1u << 3)
#define UI_GAMEFLOW_CARD_PARENT      (1u << 4)
#define UI_GAMEFLOW_CARD_FOLDER      (1u << 5)
#define UI_GAMEFLOW_CARD_CUSTOM      (1u << 6) /* the game has settings of its own */
/* An app (gui/apps.c), not a game: its poster is its own picture, and a
 * snapshot of apps is the Apps screen. */
#define UI_GAMEFLOW_CARD_APP         (1u << 7)

/* Pointer-free menu-thread record. Its fixed 6400-byte stride keeps every
 * inline RGB5A3 banner 32-byte aligned when the snapshot is memalign(32). */
typedef struct {
	u8 banner[BNR_PIXELDATA_LEN];
	char title[UI_GAMEFLOW_TITLE_LENGTH];
	char company[UI_GAMEFLOW_COMPANY_LENGTH];
	char facts[UI_GAMEFLOW_FACTS_LENGTH];
	char gameId[8];
	u64 size;
	u32 libraryIndex;
	/* Place on the ring, or the row in a grid (0 is the focused row). */
	s8 relativeSlot;
	u8 flags;
	u8 column;	/* grid only */
	/* Library Folders: a folder of games, which A opens, not a game. */
	u8 subfolder;
	u8 reserved[7];
} uiGameflowCardSnapshot_t;

typedef struct {
	uiGameflowSelectionSnapshot_t selection;
	u32 recordCount;
	char deviceName[64];
	u8 layout;	/* uiGameflowLayout_t */
	u8 columns;	/* the grid's row length, 0 for a ring */
	u8 reserved[6];
	/* Library Folders: the folder shown, as the heading names it, or empty
	 * at /games. */
	char folder[64];
	/* Spotlight: the selected game's description. Keeps the records at
	 * offset 480, a multiple of 32. */
	char description[UI_GAMEFLOW_DESCRIPTION_LENGTH];
	uiGameflowCardSnapshot_t records[UI_GAMEFLOW_RENDER_SLOTS];
} uiGameflowRenderSnapshot_t;

enum TextureId
{
	TEX_GCDVDSMALL=0,
	TEX_SDSMALL,
	TEX_HDD,
	TEX_QOOB,
	TEX_WODEIMG,
	TEX_MEMCARD,
	TEX_WIIKEY,
	TEX_SYSTEM,
	TEX_USBGECKO,
	TEX_BBA,
	TEX_CHECKED,
	TEX_UNCHECKED,
	TEX_GCLOADER,
	TEX_M2LOADER,
	TEX_ETH2GC,
	TEX_FLIPPY,
	TEX_GCNET,
	TEX_GCODE,
	TEX_KUNAIGC
};

extern GXTexObj ntscjTexObj;
extern GXTexObj ntscuTexObj;
extern GXTexObj palTexObj;
extern GXTexObj dirimgTexObj;
extern GXTexObj dolimgTexObj;
extern GXTexObj dolcliimgTexObj;
extern GXTexObj elfimgTexObj;
extern GXTexObj fileimgTexObj;
extern GXTexObj fpkgimgTexObj;
extern GXTexObj gcmimgTexObj;
extern GXTexObj mp3imgTexObj;
extern GXTexObj tgcimgTexObj;

typedef struct kbBtn_ {
    int supportedEntryMode;
	char *val;
} kbBtn;

#define ENTRYMODE_ALPHA 	(1)
#define ENTRYMODE_NUMERIC 	(1<<1)
#define ENTRYMODE_IP	 	(1<<2)
#define ENTRYMODE_MASKED 	(1<<3)
#define ENTRYMODE_FILE	 	(1<<4)

uiDrawObj_t* DrawImage(int textureId, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered);
uiDrawObj_t* DrawTexObj(GXTexObj *texObj, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered);
uiDrawObj_t* DrawProgressBar(bool indeterminate, int percent, const char *message);
uiDrawObj_t* DrawProgressBarFiles(const char *title, const char *path);
uiDrawObj_t* DrawProgressLoading(int miniModePos);
uiDrawObj_t* DrawContainer();
uiDrawObj_t* DrawMessageBox(int type, const char *message);
uiDrawObj_t* DrawPresentation(const uiPresentationSnapshot_t *snapshot);
uiDrawObj_t* DrawSaveDetails(const uiSaveDetailsSnapshot_t *snapshot);
uiDrawObj_t* DrawSelectableButton(int x1, int y1, int x2, int y2, const char *message, int mode);
uiDrawObj_t* DrawEmptyBox(int x1, int y1, int x2, int y2);
uiDrawObj_t* DrawEmptyColouredBox(int x1, int y1, int x2, int y2, GXColor colour);
uiDrawObj_t* DrawTransparentBox(int x1, int y1, int x2, int y2);
uiDrawObj_t* DrawStyledLabel(int x, int y, const char *string, float size, int align, GXColor color);
uiDrawObj_t* DrawHintLabel(int x, int y, const char *string, float size, int align, GXColor color);
/* A hint line's width at scale 1, button icons included. */
int GetHintSizeInPixels(const char *text);
float GetHintScaleToFitInWidthWithMax(const char *text, int width, float maximum);
uiDrawObj_t* DrawStyledLabelWithCaret(int x, int y, const char *string, float size, int align, GXColor color, int caretPosition);
uiDrawObj_t* DrawLabel(int x, int y, const char *string);
uiDrawObj_t* DrawFadingLabel(int x, int y, const char *string, float size);
uiDrawObj_t* DrawDynamicLabel(int x, int y, const char *(*getString)(void), float size, int align, GXColor color);
uiDrawObj_t* DrawHome(void);
/* The Source picker, and Copy/Move's destination: one event while it is
 * open. Each update copies what the picker shows of every listed device (its
 * name, picture, what it can do, where it plugs in and whether it was
 * detected), so the video thread never reads a device handler. travel is
 * the focus, unwrapped: a step changes it by one and the row slides that
 * way. */
uiDrawObj_t* DrawDeviceSelector(bool destination);
void DrawUpdateDeviceSelector(uiDrawObj_t *selector,
	DEVICEHANDLER_INTERFACE *const *listed, int count, int travel,
	const DEVICEHANDLER_INTERFACE *current,
	const DEVICEHANDLER_INTERFACE *settings, bool showAllDevices,
	bool inAdvanced);
uiDrawObj_t* DrawTooltip(const char *tooltip);
uiDrawObj_t* DrawTitleBar();
/* The CPU temperature the title bar's dial shows, smoothed over its 4 degree
 * steps; below 0 without a sensor. */
int CoreTemperature(void);
uiDrawObj_t* DrawGameflow(const uiGameflowRenderSnapshot_t *snapshot);
void DrawGameflowRequestPosters(DEVICEHANDLER_INTERFACE *device,
	const uiGameflowRenderSnapshot_t *snapshot);
/* Complete one poster before first Detail publication; idle work is stepped. */
bool DrawGameflowPrimePoster(void);
bool DrawGameflowPollPosters(void);
void DrawGameflowCancelPosters(void);
/* Spotlight's description of a game, from swiss/ui/descriptions.txt on
 * device: read whole the first time it is asked for there, and again after
 * DrawGameflowCancelPosters. False, with out empty, when there is none.
 * Menu thread only. */
bool DrawGameflowDescription(DEVICEHANDLER_INTERFACE *device,
	const char *gameId, char *out, size_t capacity);
void DrawUpdateProgressBar(uiDrawObj_t *evt, int percent);
void DrawUpdateProgressBarDetail(uiDrawObj_t *evt, int percent, int speed, int timestart, int timeremain);
void DrawUpdateProgressLoading(uiDrawObj_t *evt, int increment);
bool DrawUpdatePresentation(uiDrawObj_t *evt,
	const uiPresentationSnapshot_t *snapshot);
/* What the UI calls a device: the disc drive is the "Game Disc", not
 * upstream's "DVD". */
const char *DeviceDisplayName(const DEVICEHANDLER_INTERFACE *device);
void DrawUpdateHome(const uiHomeState_t *state,
	uiHomeCapabilities_t capabilities, const char *sourceName);
bool DrawUpdateGameflow(uiDrawObj_t *evt,
	const uiGameflowRenderSnapshot_t *snapshot);
bool DrawSetGameflowMode(uiDrawObj_t *evt, uiGameflowMode_t mode);
/* The same, already there: no motion to it. For an event not yet on screen,
 * so its first frame is the mode's (Detail opened from the File Browser). */
bool DrawSetGameflowModeNow(uiDrawObj_t *evt, uiGameflowMode_t mode);
/* The Detail row the D-pad rests on. */
bool DrawSetGameflowDetailFocus(uiDrawObj_t *evt,
	uiGameflowDetailFocus_t focus);
/* While Launch mode is on, the launch screen shows message as its step and
 * this returns true; otherwise it returns false and the caller shows its own
 * box. DrawProgressBar sends it every looping bar's message. */
bool DrawLaunchStep(const char *message);
bool DrawUpdateGameflowDetail(uiDrawObj_t *evt,
	const uiGameflowDetailSnapshot_t *snapshot);
void DrawClearGameflowDetail(uiDrawObj_t *evt);
void DrawAddChild(uiDrawObj_t *parent, uiDrawObj_t *child);
/* Runs change on the menu thread with the video thread held off, so what a
 * frame draws never changes half way (Apps' posters). change must not
 * block, draw or call anything that takes the video lock. */
void DrawWithVideoLocked(void (*change)(void *context), void *context);
uiDrawObj_t* DrawPublish(uiDrawObj_t *evt);
uiDrawObj_t* DrawRepublish(uiDrawObj_t *old, uiDrawObj_t *new);
void DrawDispose(uiDrawObj_t *evt);
/* Frees an object that was never published. */
void DrawDiscard(uiDrawObj_t *evt);
/* Settings: one page event for the whole session, drawn from a snapshot in
 * the cheat browser's language. Each update copies the snapshot and keeps
 * the screen in the colors it was built with (colors: the Menu, Backdrop and
 * Wave Color settings, one per UI_COLOR_LAYER_), in one step, until the page
 * is disposed. */
uiDrawObj_t* DrawSettingsPage(const uiSetPageSnapshot_t *snapshot);
void DrawUpdateSettingsPage(uiDrawObj_t *page,
	const uiSetPageSnapshot_t *snapshot, const int colors[UI_COLOR_LAYERS]);
/* The value list over the page. With previewLayer >= 0 the screen shows
 * previewColor, a value of that layer's setting, while the list has it
 * focused; the page's next update ends it. */
uiDrawObj_t* DrawSettingsList(const uiSetListSnapshot_t *snapshot);
void DrawUpdateSettingsList(uiDrawObj_t *list,
	const uiSetListSnapshot_t *snapshot, int previewLayer, int previewColor);
/* A row's help (its tooltip) as a card over the page. */
uiDrawObj_t* DrawSettingsHelp(const char *help);
uiDrawObj_t* DrawMemoryCardFolder(const uiFolderSnapshot_t *snapshot);
void DrawUpdateMemoryCardFolder(uiDrawObj_t *event, const uiFolderSnapshot_t *snapshot);

/* Memory Cards' folder chooser (saves.c): one page event, drawn from this
 * snapshot in the cheat browser's language, a row for each folder. */
#define UI_SAVES_PAGE_ROWS 6

typedef struct {
	char title[64];
	char blocks[24];	/* "Folder", or "" for the way back up */
} uiSavesPageRow_t;

typedef struct {
	uiSavesPageRow_t rows[UI_SAVES_PAGE_ROWS];
	char title[32];
	char status[48];
	char section[96];
	char position[16];
	char empty[2][96];
	char hint[2][64];	/* left, in ink, and right */
	s32 count;
	s32 first;
	s8 rowCount;
	s8 focusRow;
	u32 list;		/* changes with the list shown: the focus snaps */
} uiSavesPageSnapshot_t;

uiDrawObj_t* DrawSavesPage(const uiSavesPageSnapshot_t *snapshot);
void DrawUpdateSavesPage(uiDrawObj_t *page,
	const uiSavesPageSnapshot_t *snapshot);
/* Memory Cards' cube screen (saves.c): one page event, drawn from this
 * snapshot, which each update copies in whole. Its cubes and banner point
 * at the art saves.c reads into its pool: only at saves on screen, which
 * the loader never reads over, and the pool outlives the page. */
typedef struct {
	char control[32];	/* permanent L/R storage chooser */
	char name[4];		/* "A", "B" or "SD" */
	char free[8];		/* a card's free blocks, "" for the SD card */
	char path[64];		/* the SD card's open folder */
	char note[2][96];	/* why a stack with no grid has none */
	float noteScale[2];
} uiSaveCubesStackText_t;

/* The box beside the focused cube (grid.menu says it is open): the IPL's
 * Move / Copy / Erase, or a question with its title above. */
typedef struct {
	char title[48];		/* "" for none */
	char item[4][40];
	u8 count;
	u8 dim;			/* the items that can't be chosen, a bit each */
	u16 width;		/* in px: its widest item, or its title */
} uiSaveCubesMenu_t;

typedef struct {
	uiSaveCubesGrid_t grid;
	uiSaveCubesStackText_t stack[UI_SAVE_CUBES_STACKS];
	const u8 *banner;	/* the focused save's 96x32 RGB5A3 banner, or NULL */
	char line[2][48];	/* its comment's lines, or its name */
	char blocks[16];	/* its size in blocks, or "Folder" */
	char hint[2][96];	/* left, and right */
	u8 info;		/* the info bar shows the focused save or folder */
	u8 folder;		/* it is a folder */
	u8 warn;		/* line[1] says why the focused item can't be chosen */
	uiSaveCubesMenu_t menu;
	char message[96];	/* "Finished copying.", while grid.message */
} uiSaveCubesPageSnapshot_t;

uiDrawObj_t* DrawSaveCubesPage(const uiSaveCubesPageSnapshot_t *snapshot);
/* The File Browser's two panes (swiss.c's renderFileList). DrawUpdateFiles
 * and DrawUpdateFilesReading are false when page isn't one. */
uiDrawObj_t* DrawFiles(const uiFilesSnapshot_t *snapshot);
/* The page as it stands once opened: no hand-over, no fade (coming back
 * from a game's Detail). */
uiDrawObj_t* DrawFilesSettled(const uiFilesSnapshot_t *snapshot);
bool DrawUpdateFiles(uiDrawObj_t *page, const uiFilesSnapshot_t *snapshot);
bool DrawUpdateFilesReading(uiDrawObj_t *page, int pane);
void DrawUpdateSaveCubesPage(uiDrawObj_t *page,
	const uiSaveCubesPageSnapshot_t *snapshot);
uiDrawObj_t* DrawVertScrollBar(int x, int y, int width, int height, float scrollPercent, int scrollHeight);
void DrawArgsSelector(const char *fileName);
void DrawCheatsSelector(const char *fileName);
/* A game's manual (manual.c): the page shown and the bar under it. */
uiDrawObj_t* DrawManual(const uiManualView_t *view);
void DrawUpdateManual(uiDrawObj_t *page, const uiManualView_t *view);
void DrawGetTextEntry(int entryMode, const char *label, void *src, int size);
void DrawInit(GXRModeObj *videoMode, bool black);
void DrawLoadBackdrop(DEVICEHANDLER_INTERFACE *device);
void DrawShutdown();
void DrawVideoMode(GXRModeObj *videoMode);
void DrawVideoModeDefer(bool defer);

#endif
