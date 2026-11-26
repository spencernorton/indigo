/* Fuzz the game content list the updater installs from (gui/ui_content.c).
 * The bytes are copied to exactly their size, so a read past them is a
 * finding. A list that opens names only files in Indigo's own places (no
 * name in a path starts with a dot, so none climbs out), each within the
 * console's size limit, in strictly increasing order, and as many as it
 * counted; a server it names is a real IPv4 address. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ui_content.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const char games[][7] = {"GALE01", "GMSE01", "GZLP01"};
	char *text = malloc(size + 1);
	char previous[UI_CONTENT_PATH_MAX] = "";
	ui_content_list_t list;
	ui_content_file_t file;
	uint32_t count = 0;

	if(!text)
		abort();
	memcpy(text, data, size);
	text[size] = '\0';
	if(UIContent_Open(&list, text, size)) {
		if(list.hasServer && (list.server.port == 0 ||
			list.server.host[0] == '\0' || list.host[0] == '\0'))
			abort();
		while(UIContent_Next(&list, &file)) {
			if(file.size == 0 || file.size > UI_CONTENT_FILE_MAX ||
				strncmp(file.path, "swiss/", 6) != 0 ||
				strstr(file.path, "/.") != NULL ||
				strcmp(previous, file.path) >= 0)
				abort();
			memcpy(previous, file.path, sizeof(previous));
			(void)UIContent_Wanted(&file, games, 3);
			count++;
		}
		if(count != list.count)
			abort();
	}
	free(text);
	return 0;
}
