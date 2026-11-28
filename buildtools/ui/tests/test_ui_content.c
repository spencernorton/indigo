#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_content.h"

static unsigned long checks;

#define CHECK(condition) do { checks++; if(!(condition)) { \
	fprintf(stderr, "check failed at %s:%d: %s\n", \
		__FILE__, __LINE__, #condition); exit(1); } } while(0)

#define HASH_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define HASH_B "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

static bool opens(const char *text)
{
	ui_content_list_t list;
	return UIContent_Open(&list, text, strlen(text));
}

/* A file line with the given path, alone in a list. */
static bool pathOpens(const char *path)
{
	char text[256];
	snprintf(text, sizeof(text), HASH_A " 12 %s\n", path);
	return opens(text);
}

static void testList(void)
{
	static const char text[] =
		"# Indigo game content\r\n"
		"Server=203.0.113.7:8080/console/2026-11-26\r\n"
		"Host=content.example.net\r\n"
		"\r\n"
		HASH_A " 5 swiss/cheats/GALE01.txt\r\n"
		HASH_B " 70849024 swiss/cheats/GALE01_v102.txt\r\n"
		HASH_A " 9 swiss/ui/manuals/GALE.pak\r\n"
		HASH_A " 7 swiss/ui/posters.pak";
	ui_content_list_t list;
	ui_content_file_t file;

	CHECK(UIContent_Open(&list, text, strlen(text)));
	CHECK(list.count == 4u);
	CHECK(list.hasServer);
	CHECK(!strcmp(list.server.host, "203.0.113.7"));
	CHECK(list.server.port == 8080u);
	CHECK(!strcmp(list.server.path, "/console/2026-11-26/"));
	CHECK(!strcmp(list.host, "content.example.net"));

	CHECK(UIContent_Next(&list, &file));
	CHECK(!strcmp(file.path, "swiss/cheats/GALE01.txt"));
	CHECK(file.size == 5u && file.kind == UI_CONTENT_CHEATS);
	CHECK(file.sha256[0] == 0xAA && file.sha256[31] == 0xAA);
	CHECK(UIContent_Next(&list, &file));
	CHECK(file.size == 70849024u && file.sha256[0] == 0x01 && file.sha256[31] == 0xEF);
	CHECK(UIContent_Next(&list, &file));
	CHECK(file.kind == UI_CONTENT_MANUALS);
	CHECK(UIContent_Next(&list, &file));
	CHECK(file.kind == UI_CONTENT_POSTERS && !strcmp(file.path, "swiss/ui/posters.pak"));
	CHECK(!UIContent_Next(&list, &file));
	list.cursor = 0;
	CHECK(UIContent_Next(&list, &file) && !strcmp(file.path, "swiss/cheats/GALE01.txt"));

	/* Without Server and Host, the files sit beside the list. */
	CHECK(UIContent_Open(&list, HASH_A " 5 swiss/ui/stills.pak\n",
		strlen(HASH_A " 5 swiss/ui/stills.pak\n")));
	CHECK(list.count == 1u && !list.hasServer);
}

static void testRefused(void)
{
	CHECK(!opens(""));
	CHECK(!opens("# only a comment\n"));
	CHECK(!opens("Server=203.0.113.7/\nHost=a.example\n"));
	/* Server and Host travel together, once each, and say no more. */
	CHECK(!opens("Server=203.0.113.7/\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Host=a.example\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Server=203.0.113.7/\nServer=203.0.113.8/\nHost=a.example\n"
		HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Server=https://203.0.113.7/\nHost=a.example\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Server=content.example.net/\nHost=a.example\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Server=203.0.113.7/\nHost=A.example\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Server=203.0.113.7/\nHost=a example\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Server=203.0.113.7/\nHost=\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("Mirror=203.0.113.7/\n" HASH_A " 5 swiss/ui/discs.pak\n"));

	/* Sorted paths: a repeat or a step back is refused. */
	CHECK(!opens(HASH_A " 5 swiss/ui/discs.pak\n" HASH_A " 5 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " 5 swiss/ui/stills.pak\n" HASH_A " 5 swiss/ui/discs.pak\n"));

	/* The hash, the size and the spaces between. */
	CHECK(!opens("AAAAaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 5 swiss/ui/discs.pak\n"));
	CHECK(!opens("aaaa 5 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A "  5 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " 5  swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " 0 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " 05 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " -5 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " 5\n"));
	CHECK(!opens(HASH_A " 5 \n"));
	CHECK(!opens(HASH_A " 134217729 swiss/ui/discs.pak\n"));
	CHECK(opens(HASH_A " 134217728 swiss/ui/discs.pak\n"));
	CHECK(!opens(HASH_A " 99999999999 swiss/ui/discs.pak\n"));

	/* A NUL inside, or longer than the console reads. */
	CHECK(!UIContent_Open(NULL, "x", 1));
	{
		ui_content_list_t list;
		static const char nul[] = HASH_A " 5 swiss/ui/discs.pak\n\0";
		CHECK(!UIContent_Open(&list, nul, sizeof(nul) - 1u));
		CHECK(!UIContent_Open(&list, nul, UI_CONTENT_LIST_MAX + 1u));
	}
}

/* Only Indigo's own places on the card, by Indigo's own names. */
static void testPaths(void)
{
	CHECK(pathOpens("swiss/ui/posters.pak"));
	CHECK(pathOpens("swiss/ui/stills.pak"));
	CHECK(pathOpens("swiss/ui/discs.pak"));
	CHECK(pathOpens("swiss/ui/descriptions.txt"));
	CHECK(pathOpens("swiss/ui/features.txt"));
	CHECK(!pathOpens("swiss/ui/posters.pak.part"));
	CHECK(!pathOpens("swiss/ui/global.ini"));
	CHECK(!pathOpens("swiss/settings/global.ini"));
	CHECK(!pathOpens("swiss/ui/../posters.pak"));
	CHECK(!pathOpens("/swiss/ui/posters.pak"));
	CHECK(!pathOpens("ipl.dol"));

	CHECK(pathOpens("swiss/cheats/GALE01.txt"));
	CHECK(pathOpens("swiss/cheats/GAFP01_eng.txt"));
	CHECK(pathOpens("swiss/cheats/GALE01_v101.txt"));
	CHECK(!pathOpens("swiss/cheats/gale01.txt"));
	CHECK(!pathOpens("swiss/cheats/GALE0.txt"));
	CHECK(!pathOpens("swiss/cheats/GALE01x.txt"));
	CHECK(!pathOpens("swiss/cheats/GALE01.gct"));
	CHECK(!pathOpens("swiss/cheats/GALE01.chtsel"));
	CHECK(!pathOpens("swiss/cheats/GALE01_../x.txt"));
	CHECK(!pathOpens("swiss/cheats/GALE01_a b.txt"));
	CHECK(!pathOpens("swiss/cheats/../../ipl.txt"));

	CHECK(pathOpens("swiss/ui/manuals/GALE.pak"));
	CHECK(pathOpens("swiss/ui/manuals/GALE01.pak"));
	CHECK(!pathOpens("swiss/ui/manuals/GAL.pak"));
	CHECK(!pathOpens("swiss/ui/manuals/GALE0.pak"));
	CHECK(!pathOpens("swiss/ui/manuals/gale.pak"));
	CHECK(!pathOpens("swiss/ui/manuals/GALE.txt"));
	CHECK(!pathOpens("swiss/ui/manuals/G/LE.pak"));

	/* 47 letters fit the path; 48 do not. */
	CHECK(pathOpens("swiss/cheats/GALE01_abcdefghijklmnopqrstuvw.txt"));
	CHECK(!pathOpens("swiss/cheats/GALE01_abcdefghijklmnopqrstuvwx.txt"));
}

static ui_content_file_t fileAt(const char *path)
{
	ui_content_list_t list;
	ui_content_file_t file;
	char text[128];

	snprintf(text, sizeof(text), HASH_A " 1 %s\n", path);
	CHECK(UIContent_Open(&list, text, strlen(text)));
	CHECK(UIContent_Next(&list, &file));
	return file;
}

static void testWanted(void)
{
	static const char games[][7] = {"GALE01", "GMSP01"};
	ui_content_file_t file;

	file = fileAt("swiss/ui/posters.pak");
	CHECK(UIContent_Wanted(&file, NULL, 0));
	file = fileAt("swiss/cheats/GZLE01.txt");
	CHECK(UIContent_Wanted(&file, games, 2));
	/* A manual for a game on the card, by its whole ID or its first four. */
	file = fileAt("swiss/ui/manuals/GALE.pak");
	CHECK(UIContent_Wanted(&file, games, 2));
	CHECK(!UIContent_Wanted(&file, NULL, 0));
	CHECK(!UIContent_Wanted(&file, games + 1, 1));
	file = fileAt("swiss/ui/manuals/GMSP01.pak");
	CHECK(UIContent_Wanted(&file, games, 2));
	file = fileAt("swiss/ui/manuals/GMSP52.pak");
	CHECK(!UIContent_Wanted(&file, games, 2));
	file = fileAt("swiss/ui/manuals/GMSE.pak");
	CHECK(!UIContent_Wanted(&file, games, 2));
	/* An ID shorter than six (a damaged disc header) matches nothing. */
	{
		static const char shortId[][7] = {"GALE"};
		file = fileAt("swiss/ui/manuals/GALE.pak");
		CHECK(!UIContent_Wanted(&file, shortId, 1));
	}
}

int main(void)
{
	testList();
	testRefused();
	testPaths();
	testWanted();
	printf("test_ui_content: %lu checks passed\n", checks);
	return 0;
}
