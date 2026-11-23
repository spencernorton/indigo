#ifndef UI_APPS_H
#define UI_APPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Apps (Home > Apps): the pure half. apps.c lists /apps on the source and
 * the folders in it; these decide which files are programs, what each is
 * called and which picture it shows, and which of the pictures on screen
 * the poster slots hold. */

#define UI_APPS_MAX 256u
#define UI_APPS_NAME_LENGTH 64u
/* A program's or picture's path under /apps: "gbi.dol", "gbi/gbihf.dol". */
#define UI_APPS_PATH_LENGTH 256u

typedef enum {
	UI_APPS_TYPE_NONE = 0,
	UI_APPS_TYPE_DOL,
	UI_APPS_TYPE_DOL_CLI,
	UI_APPS_TYPE_ELF
} uiAppsType_t;

/* One entry of a folder's listing, as the file system gives it. */
typedef struct {
	const char *name;	/* its own name, no path */
	bool folder;
	bool hidden;		/* the file system's Hidden attribute */
	uint32_t size;
	const void *handle;	/* the device's own entry, carried through */
} uiAppsEntry_t;

typedef struct {
	char name[UI_APPS_NAME_LENGTH];	/* shown: BNR title, folder slug for default.dol, else filename */
	char author[UI_APPS_NAME_LENGTH];	/* from opening.bnr when present, else "" */
	char program[UI_APPS_PATH_LENGTH];
	char picture[UI_APPS_PATH_LENGTH];	/* "" when it has none */
	char banner[UI_APPS_PATH_LENGTH];	/* opening.bnr for default.dol, else "" */
	uint32_t size;
	uint32_t pictureSize;
	uint32_t bannerSize;
	/* The listing's entries for the program, its picture and its opening.bnr
	 * (NULL for none), for reading them as the device reads them; valid while
	 * the listings are. */
	const void *programHandle;
	const void *pictureHandle;
	const void *bannerHandle;
	uint8_t type;		/* uiAppsType_t */
	bool isDefaultDol;	/* folder/default.dol (Dolphin Depot standard) */
	bool pictureIsBnr;	/* picture is an opening.bnr rather than a PNG */
	bool pictureFailed;	/* its picture couldn't be read: not tried again */
} uiApp_t;

/* What a file is: a .dol, .dol+cli or .elf (any case), or NONE for anything
 * else, a dot name (macOS's ._ copies among them) and boot.dol or boot.elf,
 * which are the Wii's Homebrew Channel's: upstream Swiss's mixed
 * GameCube/Wii folders keep the GameCube program under another name. */
uiAppsType_t UIApps_ProgramType(const char *name);

/* A folder Apps looks in: any but a dot name. */
bool UIApps_IsFolder(const char *name);

/* "DOL", "DOL+CLI" or "ELF". */
const char *UIApps_TypeLabel(uiAppsType_t type);

/* Adds the programs in one folder's listing to list, which holds *count and
 * has room for max: /apps itself when folder is "", or one folder in it by
 * its name. Hidden programs are left out. In a folder of /apps, default.dol
 * (and default.dol+cli) follows the Dolphin Depot layout: it names itself
 * after folder until opening.bnr metadata is applied, and records opening.bnr
 * for metadata and banner art. Each program's picture is name.png beside it,
 * else, in a folder of /apps, that folder's icon.png, else opening.bnr,
 * matched ignoring case. A program whose path would not fit is left out.
 * Returns false when list filled up first. */
bool UIApps_AddFolder(uiApp_t *list, size_t *count, size_t max,
	const char *folder, const uiAppsEntry_t *entries, size_t entryCount);

/* Applies parsed opening.bnr title/author to an app (keeping its fallback
 * name if title is empty). */
void UIApps_ApplyBanner(uiApp_t *app, const char *title, const char *author);

/* By name, ignoring case, then by path: the order Apps shows. */
void UIApps_Sort(uiApp_t *list, size_t count);

/* The posters of the apps on screen: which slot holds which app's.
 *
 * The menu thread picks the slot to fill, the poster thread fills it and
 * the video thread draws the READY ones. A slot let go may still be in a
 * frame the GPU is drawing, so its texels are rewritten only
 * UI_APPS_ART_QUARANTINE_MS later (the poster cache's rule). Call them all
 * with the video lock held. */
#define UI_APPS_ART_SLOTS 25u	/* the Library grid's window */
#define UI_APPS_ART_QUARANTINE_MS 40u

typedef enum {
	UI_APPS_ART_EMPTY = 0,
	UI_APPS_ART_WANTED,	/* waiting to be filled; never drawn */
	UI_APPS_ART_READY,
	UI_APPS_ART_NONE	/* no poster could be made for it */
} uiAppsArtState_t;

typedef struct {
	int32_t app;		/* the index in the app list, or -1 */
	uint8_t state;		/* uiAppsArtState_t */
	uint32_t freedMs;	/* when it last stopped being drawn */
} uiAppsArtSlot_t;

typedef struct {
	uiAppsArtSlot_t slots[UI_APPS_ART_SLOTS];
	int32_t window[UI_APPS_ART_SLOTS];	/* wanted, nearest first */
	uint32_t windowCount;
} uiAppsArt_t;

void UIAppsArt_Init(uiAppsArt_t *art, uint32_t nowMs);
/* The apps whose posters are wanted now, nearest the selection first (the
 * first UI_APPS_ART_SLOTS of them). A slot holding any other app lets it
 * go; each wanted app without a slot takes a free one. */
void UIAppsArt_Want(uiAppsArt_t *art, const int32_t *apps, uint32_t count,
	uint32_t nowMs);
/* The slot to fill next, nearest first, or -1: a WANTED one whose
 * quarantine is over. */
int UIAppsArt_Next(const uiAppsArt_t *art, uint32_t nowMs);
/* The poster for app is in slot (ok), or couldn't be made. Taken only while
 * the slot still waits for that app: one that scrolled away while its
 * poster was being made gets nothing. False when not taken. */
bool UIAppsArt_Done(uiAppsArt_t *art, int slot, int32_t app, bool ok);
/* The READY slot with app's poster, or -1. */
int UIAppsArt_Find(const uiAppsArt_t *art, int32_t app);

#endif
