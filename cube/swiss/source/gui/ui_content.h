#ifndef UI_CONTENT_H
#define UI_CONTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_update.h"

/* Game content (Settings > Network > Install game content): reading the
 * list of poster, cheat and manual files that the signed content manifest
 * vouches for by its SHA-256, and choosing which of them a card needs. No
 * console calls, so the host tests run it and the fuzzer feeds it
 * (buildtools/ui/tests/test_ui_content.c, buildtools/ui/tests/fuzz/
 * fuzz_content.c). update.c does the network and the card. The format is
 * written down in docs/PACKS.md, "Install from the console". */

#define UI_CONTENT_LIST_MAX (512u * 1024u)          /* a list is never longer */
#define UI_CONTENT_FILES_MAX 4096u                  /* ... nor names more files */
#define UI_CONTENT_FILE_MAX (128u * 1024u * 1024u)  /* posters.pak is 70 MB */
#define UI_CONTENT_PATH_MAX 48u

typedef enum {
	UI_CONTENT_POSTERS = 0, /* swiss/ui: the Library's packs and texts */
	UI_CONTENT_CHEATS,      /* swiss/cheats/<game ID>[_name].txt */
	UI_CONTENT_MANUALS      /* swiss/ui/manuals/<ID4 or ID6>.pak */
} ui_content_kind_t;

typedef struct {
	char path[UI_CONTENT_PATH_MAX]; /* the same on the card and the server */
	uint32_t size;
	uint8_t sha256[32];
	ui_content_kind_t kind;
} ui_content_file_t;

typedef struct {
	const char *text;
	size_t length;
	size_t cursor;
	uint32_t count;          /* the files it lists */
	bool hasServer;          /* Server= and Host=: where the files are */
	ui_update_base_t server;
	char host[64];
} ui_content_list_t;

/* Checks the whole list before anything is installed from it: every line
 * is a comment, Server=, Host= or "<sha256> <size> <path>"; paths are
 * strictly increasing, so none repeats; Server and Host come together.
 * False for anything else, and the list is not used. */
bool UIContent_Open(ui_content_list_t *list, const char *text, size_t length);

/* The next file, in the list's order; false after the last. */
bool UIContent_Next(ui_content_list_t *list, ui_content_file_t *file);

/* Posters and cheats are for every game. A manual is for the games on the
 * card: its name is one of their six-letter IDs, or the first four letters
 * of one (the same game and region from any publisher). */
bool UIContent_Wanted(const ui_content_file_t *file,
	const char (*gameIds)[7], size_t count);

#endif
