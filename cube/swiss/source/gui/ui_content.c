#include <string.h>

#include "ui_content.h"

enum { LINE_SKIP, LINE_SERVER, LINE_HOST, LINE_FILE, LINE_BAD };

static const char *const POSTER_FILES[] = {
	"descriptions.txt", "discs.pak", "features.txt", "posters.pak", "stills.pak"
};

static bool isIdLetter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool isId(const char *text, size_t length)
{
	size_t i;

	for(i = 0; i < length; i++) {
		if(!isIdLetter(text[i]))
			return false;
	}
	return true;
}

static bool startsWith(const char *text, size_t length, const char *prefix,
	size_t *rest)
{
	size_t prefixLength = strlen(prefix);

	if(length < prefixLength || memcmp(text, prefix, prefixLength) != 0)
		return false;
	*rest = prefixLength;
	return true;
}

/* Only the three places Indigo reads game content from, by its own names:
 * a list can never write anywhere else on the card. */
static bool parsePath(const char *text, size_t length, ui_content_file_t *file)
{
	size_t at;
	size_t i;

	if(length == 0 || length >= sizeof(file->path))
		return false;
	memcpy(file->path, text, length);
	file->path[length] = '\0';
	if(startsWith(text, length, "swiss/ui/manuals/", &at)) {
		size_t stem = length - at;
		file->kind = UI_CONTENT_MANUALS;
		return (stem == 8u || stem == 10u) && isId(text + at, stem - 4u) &&
			memcmp(text + length - 4u, ".pak", 4u) == 0;
	}
	if(startsWith(text, length, "swiss/cheats/", &at)) {
		file->kind = UI_CONTENT_CHEATS;
		return length - at >= 10u && isId(text + at, 6u) &&
			(length - at == 10u || text[at + 6u] == '_') &&
			memcmp(text + length - 4u, ".txt", 4u) == 0 &&
			UIUpdate_SafeBasename(file->path + at);
	}
	if(startsWith(text, length, "swiss/ui/", &at)) {
		file->kind = UI_CONTENT_POSTERS;
		for(i = 0; i < sizeof(POSTER_FILES) / sizeof(POSTER_FILES[0]); i++) {
			if(!strcmp(file->path + at, POSTER_FILES[i]))
				return true;
		}
	}
	return false;
}

/* "<64 lower-case hex> <size> <path>", one space apart. */
static bool parseFile(const char *line, size_t length, ui_content_file_t *file)
{
	char hex[65];
	uint64_t size = 0;
	size_t at = 65u;
	size_t digits = 0;

	if(length < at + 2u || line[64] != ' ')
		return false;
	memcpy(hex, line, 64u);
	hex[64] = '\0';
	if(!UIUpdate_HexToBytes32(hex, file->sha256))
		return false;
	while(at < length && line[at] >= '0' && line[at] <= '9' && digits < 10u) {
		size = size * 10u + (uint64_t)(line[at++] - '0');
		digits++;
	}
	/* No leading zero, so never an empty file either. */
	if(digits == 0 || line[65] == '0' || size > UI_CONTENT_FILE_MAX ||
		at >= length || line[at] != ' ')
		return false;
	file->size = (uint32_t)size;
	return parsePath(line + at + 1u, length - at - 1u, file);
}

static int parseLine(const char *line, size_t length, ui_content_file_t *file,
	ui_content_list_t *list)
{
	char value[80];
	size_t at;

	if(length == 0 || line[0] == '#')
		return LINE_SKIP;
	if(startsWith(line, length, "Server=", &at) ||
		startsWith(line, length, "Host=", &at)) {
		bool server = line[0] == 'S';
		size_t i;
		if(length - at == 0 || length - at >= sizeof(value))
			return LINE_BAD;
		memcpy(value, line + at, length - at);
		value[length - at] = '\0';
		if(server) {
			ui_update_base_err_t why;
			return UIUpdate_ParseBase(value, &list->server, &why) ?
				LINE_SERVER : LINE_BAD;
		}
		if(length - at >= sizeof(list->host))
			return LINE_BAD;
		for(i = 0; value[i]; i++) {
			char c = value[i];
			if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == '.' || c == '-'))
				return LINE_BAD;
		}
		memcpy(list->host, value, length - at + 1u);
		return LINE_HOST;
	}
	return parseFile(line, length, file) ? LINE_FILE : LINE_BAD;
}

/* One line from the cursor, without its CR LF or LF; false at the end. */
static bool nextLine(ui_content_list_t *list, const char **line, size_t *length)
{
	size_t start = list->cursor;
	size_t end = start;

	if(start >= list->length)
		return false;
	while(end < list->length && list->text[end] != '\n')
		end++;
	list->cursor = end < list->length ? end + 1u : end;
	if(end > start && list->text[end - 1u] == '\r')
		end--;
	*line = list->text + start;
	*length = end - start;
	return true;
}

bool UIContent_Open(ui_content_list_t *list, const char *text, size_t length)
{
	ui_content_file_t file;
	char previous[UI_CONTENT_PATH_MAX] = "";
	const char *line;
	size_t lineLength;
	unsigned seen = 0;

	if(list == NULL || text == NULL || length == 0 ||
		length > UI_CONTENT_LIST_MAX || memchr(text, '\0', length) != NULL)
		return false;
	memset(list, 0, sizeof(*list));
	list->text = text;
	list->length = length;
	while(nextLine(list, &line, &lineLength)) {
		int type = parseLine(line, lineLength, &file, list);
		if(type == LINE_BAD)
			return false;
		if(type == LINE_SERVER || type == LINE_HOST) {
			unsigned bit = type == LINE_SERVER ? 1u : 2u;
			if(seen & bit)
				return false;
			seen |= bit;
		}
		if(type != LINE_FILE)
			continue;
		if(strcmp(previous, file.path) >= 0 ||
			list->count == UI_CONTENT_FILES_MAX)
			return false;
		memcpy(previous, file.path, sizeof(previous));
		list->count++;
	}
	if(list->count == 0 || (seen != 0 && seen != 3u))
		return false;
	list->hasServer = seen == 3u;
	list->cursor = 0;
	return true;
}

bool UIContent_Next(ui_content_list_t *list, ui_content_file_t *file)
{
	ui_content_list_t scratch;
	const char *line;
	size_t length;

	if(list == NULL || file == NULL)
		return false;
	while(nextLine(list, &line, &length)) {
		/* Open checked every line; Server and Host go to a scratch copy. */
		scratch = *list;
		if(parseLine(line, length, file, &scratch) == LINE_FILE)
			return true;
	}
	return false;
}

bool UIContent_Wanted(const ui_content_file_t *file,
	const char (*gameIds)[7], size_t count)
{
	const char *stem;
	size_t stemLength;
	size_t i;

	if(file == NULL)
		return false;
	if(file->kind != UI_CONTENT_MANUALS)
		return true;
	stem = file->path + strlen("swiss/ui/manuals/");
	stemLength = strlen(stem) - 4u;
	for(i = 0; gameIds != NULL && i < count; i++) {
		if(strlen(gameIds[i]) == 6u && !memcmp(gameIds[i], stem, stemLength))
			return true;
	}
	return false;
}
