#ifndef UI_SAVES_H
#define UI_SAVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Saves' messages are in Menu Language. */
#include "ui_lang.h"

/* Physical slots are 0/1; SD columns keep independent positions at 2/3. */
void UISaves_InitialStorage(bool slotA, bool slotB, bool sd, int stacks[2]);
/* The storage menu lists Slot A, Slot B, then sdDevices SD devices (1, or 2
 * when the Source is another FatFs device than the settings device): choice
 * 2 + n is SD device n. The tab a choice puts in stack's column, or -1 for
 * none: cancelled, not listed, or the slot the other column shows. */
int UISaves_StorageTab(int stack, int choice, int other, int sdDevices);

/* Memory Cards (Home > System > Memory Cards): the pure half. saves.c asks
 * the card and SD drivers for the bytes; these decide what the bytes are and
 * where a save may go. */

#define UI_SAVES_ENTRY_SIZE 64u	/* a card's directory entry, a .gci's header */
#define UI_SAVES_BLOCK_SIZE 8192u
#define UI_SAVES_NAME_LENGTH 32u	/* CARD_FILENAMELEN */

/* The places a save can go: a card, the folder the SD card's stack has
 * open, or another folder, chosen then, or the card image the SD card's
 * stack has open. The first three are also the places a stack shows. */
typedef enum {
	UI_SAVES_PLACE_SLOT_A = 0,
	UI_SAVES_PLACE_SLOT_B,
	UI_SAVES_PLACE_FOLDER,
	UI_SAVES_PLACE_CHOOSE,
	UI_SAVES_PLACE_IMAGE
} uiSavesPlace_t;

/* A .gci is the card's directory entry, then the save's blocks. Action
 * Replay (.sav, "DATELGC_SAVE") and GameShark (.gcs, "GCSAVE") files put
 * their own header first. Copies the entry to entry as a .gci has it and
 * returns where the blocks start, or 0 when the file isn't one whole save:
 * its length must be the entry's block count exactly. */
size_t UISaves_FindEntry(const uint8_t *file, size_t length,
	uint8_t entry[UI_SAVES_ENTRY_SIZE]);

/* FindEntry from the first headLength bytes of a file fileLength long: the
 * first UI_SAVES_HEAD_SIZE bytes hold any wrapper's header and the entry. */
#define UI_SAVES_HEAD_SIZE 0x150u
size_t UISaves_FindEntryPrefix(const uint8_t *head, size_t headLength,
	size_t fileLength, uint8_t entry[UI_SAVES_ENTRY_SIZE]);

/* The blocks the entry says the save has. */
unsigned UISaves_Blocks(const uint8_t entry[UI_SAVES_ENTRY_SIZE]);

/* "01-GALE-SuperSmashBros0110290334.gci", the maker, game code and card
 * name, as Dolphin names a GCI folder's files, so a folder of them can be a
 * Dolphin memory card. A character a FAT name can't hold becomes '_'. */
void UISaves_FileName(char *out, size_t capacity,
	const uint8_t entry[UI_SAVES_ENTRY_SIZE]);

/* "name.gci" as "name_2.gci": the attempt'th name for a copy when the first
 * is taken (attempt 1 is the name itself). */
void UISaves_NumberedName(char *out, size_t capacity, const char *name,
	int attempt);

/* Why a Copy or Move can't go where it would, known before it starts: the
 * checks the copy itself makes afterwards (saves.c keeps them all), and the
 * Move guard. */
typedef enum {
	UI_SAVES_VERDICT_OK = 0,
	UI_SAVES_VERDICT_NO_MOVE,	/* the game won't let its save move */
	UI_SAVES_VERDICT_NO_CARD,	/* nothing usable in the slot */
	UI_SAVES_VERDICT_READ_ONLY,	/* the SD card or card image can't be written */
	UI_SAVES_VERDICT_HAS_IT,	/* the card has a save of this game and name */
	UI_SAVES_VERDICT_FULL,		/* the card holds its 127 saves */
	UI_SAVES_VERDICT_ROOM		/* too few free blocks */
} uiSavesVerdict_t;

#define UI_SAVES_CARD_FILES 127u	/* CARD_MAXFILES */

/* Where a save would go. */
typedef struct {
	const char *name;	/* "Slot B", "The SD card", "The card image" */
	bool card;		/* a memory card or card image; else a folder on the SD card */
	bool ready;		/* a card listed, or a folder read */
	bool writable;		/* a folder's or card image's device takes writes */
	bool hasIt;		/* the card has a save of this game, maker and name */
	int saves;		/* on the card */
	int freeBlocks;
	bool image;		/* a card image: a card that is a file on the SD card */
} uiSavesRoom_t;

/* Whether a save blocks long goes to the room to, moved or copied, from a
 * card (or card image) or a folder, as today's rules have it: a card's save
 * marked no-copy or no-move (entry byte 0x34) doesn't move, and copies
 * always may. A card image takes a save as a card does, when its device
 * writes. Writes the reason in words to why ("" when it may go). */
uiSavesVerdict_t UISaves_Verdict(bool move, bool fromCard, uint8_t permissions,
	unsigned blocks, const uiSavesRoom_t *to, char *why, size_t size);

/* A copy holds its save twice at once (as read, and as the card driver's
 * write buffer or the copy read back) and needs room besides: the card
 * driver's listing of a card's 127 saves, about 1.2 KiB each, and a sector,
 * with some to spare. */
#define UI_SAVES_COPY_MARGIN (192u * 1024u)

/* Whether a copy of a save bytes long as it is read could run short of
 * memory with freeBytes left: less than twice it and UI_SAVES_COPY_MARGIN. */
bool UISaves_CopyCrowded(uint32_t freeBytes, uint32_t bytes);

/* ------------------------------------------------------------------------
 * A save's art: its banner, its animated icon and its comment, where the
 * entry puts them in the save's data (the bytes after the entry), read as
 * Dolphin reads them. Fields come from the entry's own bytes, never through
 * libogc2's offset_icon[] (which steps an RGB5A3 frame 3072 bytes, not 2048)
 * or its CARD_GetIconSpeed and CARD_SetIconSpeed (wrong bits, wrong field).
 * --------------------------------------------------------------------- */
#define UI_SAVES_ICON_FRAMES 8u		/* CARD_MAXICONS */
#define UI_SAVES_ICON_BYTES 2048u	/* a 32x32 frame as RGB5A3 */
#define UI_SAVES_BANNER_BYTES 6144u	/* the 96x32 banner as RGB5A3 */
#define UI_SAVES_ART_STEPS 14u		/* 8 frames, then 6 back in a bounce */
#define UI_SAVES_ART_MAX_END 65536u	/* the most of a save its art is read from */
#define UI_SAVES_ART_TICK_HZ 15u	/* speed 1 holds a frame 4 VSyncs at 60 Hz */
#define UI_SAVES_ART_BANNER (-1)	/* ToRgb5a3's banner, not a frame */
#define UI_SAVES_ART_BLANK 0xFFu	/* a step that shows nothing */

/* A picture's pixels, as the entry's two-bit fields name them. */
typedef enum {
	UI_SAVES_ART_NONE = 0,
	UI_SAVES_ART_CI8_SHARED,	/* the frames' palette follows the last frame */
	UI_SAVES_ART_RGB5A3,
	UI_SAVES_ART_CI8_OWN		/* its own palette follows it, as a CI8 banner's */
} uiSavesArtFormat_t;

typedef struct {
	uint32_t end;		/* the bytes of the save's data the art spans */
	uint32_t bannerAt;
	uint32_t frameAt[UI_SAVES_ICON_FRAMES];
	uint32_t paletteAt;	/* the shared palette, when a frame uses it */
	uint32_t commentAt;
	uint8_t bannerFormat;	/* NONE, RGB5A3 or CI8_OWN */
	uint8_t frameFormat[UI_SAVES_ICON_FRAMES];	/* NONE: shows a later frame */
	uint8_t frames;		/* frames with a speed, up to the first without */
	uint8_t steps;		/* the timeline's steps, 0 when there is no icon */
	uint8_t stepFrame[UI_SAVES_ART_STEPS];	/* a frame with pixels, or BLANK */
	uint8_t stepHold[UI_SAVES_ART_STEPS];	/* in ticks: 1, 2 or 3 */
	uint8_t period;		/* the ticks in one turn of the timeline */
	bool comment;		/* commentAt holds the comment's two 32-byte lines */
} uiSavesArt_t;

/* Where entry's banner, icon frames and comment lie in a save whose data is
 * dataLength bytes. A part that runs past the data, or past
 * UI_SAVES_ART_MAX_END, is left out on its own. An icon is up to 8 frames,
 * ending at the first with no speed; a frame with no pixels shows the next
 * one that has them, or nothing; banner_fmt bit 0x04 plays the frames back
 * again in reverse; and a first frame with no pixels means no icon at all.
 * Returns whether any of it is there to read (art->end bytes). */
bool UISaves_ArtLayout(const uint8_t entry[UI_SAVES_ENTRY_SIZE],
	size_t dataLength, uiSavesArt_t *art);

/* Frame number frame (or UI_SAVES_ART_BANNER) of the save's data, length
 * bytes of it, as 4x4 tiles of RGB5A3 texels, GX_TF_RGB5A3's layout: an
 * RGB5A3 picture as it is, a CI8 one looked up in its palette, so drawing it
 * needs no TLUT. Writes UI_SAVES_ICON_BYTES (UI_SAVES_BANNER_BYTES for the
 * banner); false when the picture has no pixels or lies outside length. */
bool UISaves_ToRgb5a3(const uint8_t *data, size_t length,
	const uiSavesArt_t *art, int frame, uint8_t *out);

/* The frame the icon shows at tick (UI_SAVES_ART_TICK_HZ ticks a second),
 * or -1 for nothing. Every icon runs on the same clock. */
int UISaves_ArtStep(const uiSavesArt_t *art, uint32_t tick);

/* Why the icon does or does not move, from its checked layout. NULL means
 * the header could not be read. UI Motion Off pauses only animated icons. */
const char *UISaves_IconDescription(const uint8_t entry[UI_SAVES_ENTRY_SIZE],
	size_t dataLength, bool motionOff);

/* ------------------------------------------------------------------------
 * The art pool: a fixed number of slots, filled one save at a time while
 * nothing is pressed.
 * --------------------------------------------------------------------- */
#define UI_SAVES_ID_START 2166136261u	/* FNV-1a's offset basis */

/* FNV-1a over length bytes, continuing from hash. A save's id is its place,
 * then its game, maker and name on a card or its path in a folder. Never 0,
 * which marks a free slot. */
uint32_t UISaves_Id(uint32_t hash, const void *bytes, size_t length);

/* A slot to read a save's art into: one never used, else one whose save
 * isn't in want, so nothing on screen draws the slot while it is written.
 * -1 when every slot holds a save in want. */
int UISaves_SlotPick(const uint32_t *tags, int slots, const uint32_t *want,
	int wantCount);

/* The cells of rows firstRow .. firstRow + rows - 1 of a grid columns wide
 * holding count cells (rows before the first or after the last are skipped),
 * nearest the focus cell first: by rows and columns apart, then in order. A
 * focus below 0 keeps them in order. Writes up to rows * columns cells to out
 * and returns how many. */
int UISaves_LoadOrder(int focus, int firstRow, int rows, int columns,
	int count, int *out);

#endif
