#include <string.h>

#include "ui_update.h"

/* The manifest's keys, in the order a reader meets them; the bits remember
 * which ones a manifest has given, so a repeated key and a missing one are
 * both caught at the end. */
enum {
	KEY_FORMAT,
	KEY_CHANNEL,
	KEY_VERSION,
	KEY_COMMIT,
	KEY_DATE,
	KEY_BUILD_TIME,
	KEY_FILE,
	KEY_SIZE,
	KEY_SHA256,
	KEY_APP_FILE,
	KEY_APP_SIZE,
	KEY_APP_SHA256,
	KEY_MIN_VERSION,
	KEY_MIN_BUILD_TIME,
	KEY_NOTES_URL,
	KEY_COUNT
};

static const char *const KEY_NAMES[KEY_COUNT] = {
	"FormatVersion", "Channel", "Version", "Commit", "Date", "BuildTime",
	"File", "Size", "SHA256", "ApploaderFile", "ApploaderSize",
	"ApploaderSHA256", "MinVersion", "MinBuildTime", "NotesURL"
};

/* A decimal number, nothing else: no sign, no spaces, at most ten digits. */
static bool parseU32(const char *text, size_t length, uint32_t *out)
{
	uint64_t value = 0;
	size_t i;

	if(length == 0 || length > 10u)
		return false;
	for(i = 0; i < length; i++) {
		if(text[i] < '0' || text[i] > '9')
			return false;
		value = value * 10u + (uint64_t)(text[i] - '0');
	}
	if(value > 0xFFFFFFFFu)
		return false;
	*out = (uint32_t)value;
	return true;
}

/* Seconds since 1970, as git writes them: ten digits for now, room for
 * eleven; never before the GameCube, never absurd. */
static bool parseEpoch(const char *text, size_t length, int64_t *out)
{
	uint32_t value;

	if(!parseU32(text, length, &value))
		return false;
	if(value < 946684800u) /* 2000-01-01 */
		return false;
	*out = (int64_t)value;
	return true;
}

static bool isHexLower(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static bool spanEquals(const char *text, size_t length, const char *word)
{
	size_t wordLength = strlen(word);

	return length == wordLength && memcmp(text, word, length) == 0;
}

/* Copies a value that must fit, whole: a value too long for its home is a
 * broken manifest, never a shortened one. */
static bool copyValue(char *out, size_t outSize, const char *text, size_t length)
{
	if(length >= outSize)
		return false;
	memcpy(out, text, length);
	out[length] = '\0';
	return true;
}

bool UIUpdate_SafeBasename(const char *name)
{
	size_t length;
	size_t i;

	if(name == NULL)
		return false;
	length = strlen(name);
	if(length == 0 || length > 64u || name[0] == '.')
		return false;
	for(i = 0; i < length; i++) {
		char c = name[i];
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
		if(!ok)
			return false;
	}
	return true;
}

bool UIUpdate_HexToBytes32(const char *hex, uint8_t out[32])
{
	size_t i;

	if(hex == NULL || strlen(hex) != 64u)
		return false;
	for(i = 0; i < 32u; i++) {
		uint8_t byte = 0;
		int half;
		for(half = 0; half < 2; half++) {
			char c = hex[i * 2u + (size_t)half];
			uint8_t digit;
			if(c >= '0' && c <= '9')
				digit = (uint8_t)(c - '0');
			else if(c >= 'a' && c <= 'f')
				digit = (uint8_t)(c - 'a' + 10);
			else
				return false;
			byte = (uint8_t)((byte << 4) | digit);
		}
		out[i] = byte;
	}
	return true;
}

void UIUpdate_Hex16(const uint8_t hash[32], char out[17])
{
	static const char hexdigits[] = "0123456789abcdef";
	size_t i;

	for(i = 0; i < 8u; i++) {
		out[i * 2u] = hexdigits[hash[i] >> 4];
		out[i * 2u + 1u] = hexdigits[hash[i] & 0x0Fu];
	}
	out[16] = '\0';
}

const char *UIUpdate_ChannelName(int channel)
{
	return channel == 1 ? "rc" : "stable";
}

ui_update_decision_t UIUpdate_Decide(const ui_update_manifest_t *manifest,
	const char *runningCommit7, int64_t runningBuildTime)
{
	/* Unknown (0, a build without git) is before every floor: the
	 * stepping stone goes on from a computer first, as it safely can. */
	if(manifest->hasMinBuild && runningBuildTime < manifest->minBuildTime)
		return UI_UPDATE_DECIDE_MIN_BUILD;
	if(runningCommit7 != NULL && (strcmp(manifest->commit, runningCommit7) == 0 ||
		(strlen(runningCommit7) == 7 && strncmp(manifest->commit, runningCommit7, 7) == 0)))
		return UI_UPDATE_DECIDE_CURRENT;
	/* Unknown is never "newer": offered as older/different, warned. */
	if(runningBuildTime > 0 && manifest->buildTime > runningBuildTime)
		return UI_UPDATE_DECIDE_NEWER;
	return UI_UPDATE_DECIDE_OLDER;
}


/* One manifest value into its field. False is a broken manifest; the two
 * special answers travel through `special`. */
static bool applyValue(ui_update_manifest_t *out, int key, const char *value,
	size_t length, const char *channel, ui_update_err_t *special)
{
	char text[130];
	uint32_t number;

	switch(key) {
		case KEY_FORMAT:
			if(!spanEquals(value, length, "1")) {
				*special = UI_UPDATE_FORMAT;
				return false;
			}
			return true;
		case KEY_CHANNEL:
			if(!spanEquals(value, length, channel)) {
				*special = UI_UPDATE_CHANNEL;
				return false;
			}
			return true;
		case KEY_VERSION: {
			size_t i;
			if(length < 6u || value[0] != 'v' ||
				value[1] < '0' || value[1] > '9')
				return false;
			for(i = 0; i < length; i++) {
				char c = value[i];
				if(c <= ' ' || c >= 0x7F)
					return false;
			}
			return copyValue(out->version, sizeof(out->version), value, length);
		}
		case KEY_COMMIT: {
			size_t i;
			if(length != 40u)
				return false;
			for(i = 0; i < length; i++) {
				if(!isHexLower(value[i]))
					return false;
			}
			return copyValue(out->commit, sizeof(out->commit), value, length);
		}
		case KEY_DATE:
			if(length != 10u || value[4] != '-' || value[7] != '-')
				return false;
			return copyValue(out->date, sizeof(out->date), value, length);
		case KEY_BUILD_TIME:
			return parseEpoch(value, length, &out->buildTime);
		case KEY_FILE:
			if(!copyValue(out->file, sizeof(out->file), value, length))
				return false;
			return UIUpdate_SafeBasename(out->file);
		case KEY_SIZE:
			if(!parseU32(value, length, &number) ||
				number == 0 || number > UI_UPDATE_DOL_MAX)
				return false;
			out->size = number;
			return true;
		case KEY_SHA256:
			if(!copyValue(text, sizeof(text), value, length))
				return false;
			return UIUpdate_HexToBytes32(text, out->sha256);
		case KEY_APP_FILE:
			if(!copyValue(out->apploaderFile, sizeof(out->apploaderFile), value, length))
				return false;
			return UIUpdate_SafeBasename(out->apploaderFile);
		case KEY_APP_SIZE:
			if(!parseU32(value, length, &number) ||
				number == 0 || number > UI_UPDATE_APPLOADER_MAX)
				return false;
			out->apploaderSize = number;
			return true;
		case KEY_APP_SHA256:
			if(!copyValue(text, sizeof(text), value, length))
				return false;
			return UIUpdate_HexToBytes32(text, out->apploaderSha256);
		case KEY_MIN_VERSION:
			return copyValue(out->minVersion, sizeof(out->minVersion), value, length);
		case KEY_MIN_BUILD_TIME:
			return parseEpoch(value, length, &out->minBuildTime);
		case KEY_NOTES_URL:
			return copyValue(out->notesUrl, sizeof(out->notesUrl), value, length);
	}
	return false;
}

ui_update_err_t UIUpdate_ParseManifest(const char *text, size_t length,
	const char *channel, ui_update_manifest_t *out)
{
	uint32_t seen = 0;
	size_t pos = 0;
	size_t i;

	if(text == NULL || channel == NULL || out == NULL ||
		length == 0 || length > UI_UPDATE_MANIFEST_MAX)
		return UI_UPDATE_INVALID;
	memset(out, 0, sizeof(*out));
	for(i = 0; i < length; i++) {
		if(text[i] == '\0')
			return UI_UPDATE_INVALID;
	}
	while(pos < length) {
		size_t lineStart = pos;
		size_t lineEnd;
		size_t equal;
		int key = -1;
		int k;

		while(pos < length && text[pos] != '\n')
			pos++;
		lineEnd = pos;
		if(pos < length)
			pos++; /* past the newline */
		if(lineEnd > lineStart && text[lineEnd - 1] == '\r')
			lineEnd--;
		if(lineEnd - lineStart > UI_UPDATE_LINE_MAX)
			return UI_UPDATE_INVALID;
		if(lineEnd == lineStart)
			continue; /* a blank line says nothing */
		equal = lineStart;
		while(equal < lineEnd && text[equal] != '=')
			equal++;
		if(equal == lineStart || equal >= lineEnd)
			return UI_UPDATE_INVALID;
		for(k = 0; k < KEY_COUNT; k++) {
			if(spanEquals(text + lineStart, equal - lineStart, KEY_NAMES[k])) {
				key = k;
				break;
			}
		}
		if(key < 0)
			continue; /* a later manifest's line: not ours to read */
		if(seen & (1u << (unsigned)key))
			return UI_UPDATE_INVALID;
		seen |= 1u << (unsigned)key;
		{
			ui_update_err_t special = UI_UPDATE_OK;
			if(!applyValue(out, key, text + equal + 1, lineEnd - equal - 1,
				channel, &special))
				return special != UI_UPDATE_OK ? special : UI_UPDATE_INVALID;
		}
	}
	/* What a manifest must say, and what must travel together. */
	{
		static const int required[] = {
			KEY_FORMAT, KEY_CHANNEL, KEY_VERSION, KEY_COMMIT, KEY_DATE,
			KEY_BUILD_TIME, KEY_FILE, KEY_SIZE, KEY_SHA256
		};
		uint32_t trio;
		for(i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
			if(!(seen & (1u << (unsigned)required[i])))
				return UI_UPDATE_INVALID;
		}
		trio = (seen >> KEY_APP_FILE) & 7u;
		if(trio != 0u && trio != 7u)
			return UI_UPDATE_INVALID;
		out->hasApploader = trio == 7u;
		out->hasMinBuild = (seen & (1u << KEY_MIN_BUILD_TIME)) != 0;
	}
	return UI_UPDATE_OK;
}

/* One dotted-quad part: one to three digits, worth at most 255. Leading
 * zeros are read as written (`010` is ten), where inet_aton would read
 * octal: the address the text shows is the address used. */
static bool parseOctet(const char **cursor, uint32_t *out)
{
	const char *p = *cursor;
	uint32_t value = 0;
	int digits = 0;

	while(*p >= '0' && *p <= '9') {
		if(digits == 3)
			return false;
		value = value * 10u + (uint32_t)(*p - '0');
		digits++;
		p++;
	}
	if(digits == 0 || value > 255u)
		return false;
	*cursor = p;
	*out = value;
	return true;
}

bool UIUpdate_ParseBase(const char *text, ui_update_base_t *out,
	ui_update_base_err_t *why)
{
	const char *p;
	uint32_t octet[4];
	int part;

	if(why != NULL)
		*why = UI_UPDATE_BASE_BAD;
	if(text == NULL || out == NULL)
		return false;
	memset(out, 0, sizeof(*out));
	out->port = 80;
	out->path[0] = '/';
	out->path[1] = '\0';
	p = text;
	if(strncmp(p, "https://", 8) == 0) {
		if(why != NULL)
			*why = UI_UPDATE_BASE_HTTPS;
		return false;
	}
	if(strncmp(p, "http://", 7) == 0)
		p += 7;
	for(part = 0; part < 4; part++) {
		if(!parseOctet(&p, &octet[part])) {
			/* Letters where an address belongs are a name, and names
			 * need the DNS the console does not have. */
			if(part == 0 && why != NULL &&
				((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
				*why = UI_UPDATE_BASE_NAME;
			return false;
		}
		if(part < 3) {
			if(*p != '.')
				return false;
			p++;
		}
	}
	/* The address as validated, digit for digit, is the address used. */
	{
		char *host = out->host;
		size_t used = 0;
		for(part = 0; part < 4; part++) {
			uint32_t value = octet[part];
			char digits[4];
			int count = 0;
			int d;
			if(part > 0)
				host[used++] = '.';
			do {
				digits[count++] = (char)('0' + (int)(value % 10u));
				value /= 10u;
			} while(value > 0);
			for(d = count - 1; d >= 0; d--)
				host[used++] = digits[d];
		}
		host[used] = '\0';
	}
	if(*p == ':') {
		uint32_t port = 0;
		int digits = 0;
		p++;
		while(*p >= '0' && *p <= '9') {
			if(digits == 5)
				return false;
			port = port * 10u + (uint32_t)(*p - '0');
			digits++;
			p++;
		}
		if(digits == 0 || port == 0 || port > 65535u)
			return false;
		out->port = (uint16_t)port;
	}
	if(*p == '/') {
		size_t used = 0;
		while(*p != '\0') {
			char c = *p;
			bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				(c >= '0' && c <= '9') || c == '.' || c == '_' ||
				c == '~' || c == '-' || c == '/';
			if(!ok || used + 1u >= sizeof(out->path))
				return false;
			if(c == '.' && used > 0 && out->path[used - 1] == '.' &&
				(used < 2u || out->path[used - 2] == '/'))
				return false; /* a ".." step: the folder is one folder */
			out->path[used++] = c;
			p++;
		}
		if(used == 0 || out->path[used - 1] != '/') {
			if(used + 1u >= sizeof(out->path))
				return false;
			out->path[used++] = '/';
		}
		out->path[used] = '\0';
	}
	if(*p != '\0')
		return false;
	if(why != NULL)
		*why = UI_UPDATE_BASE_OK;
	return true;
}

/* A header name, matched without either side's case mattering. */
static bool headerNameIs(const char *text, size_t length, const char *name)
{
	size_t i;

	if(length != strlen(name))
		return false;
	for(i = 0; i < length; i++) {
		char a = text[i];
		char b = name[i];
		if(a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if(b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if(a != b)
			return false;
	}
	return true;
}

static bool spanHasWord(const char *text, size_t length, const char *word)
{
	size_t wordLength = strlen(word);
	size_t i;

	if(length < wordLength)
		return false;
	for(i = 0; i + wordLength <= length; i++) {
		if(headerNameIs(text + i, wordLength, word))
			return true;
	}
	return false;
}

bool UIUpdate_ParseHttpHead(const char *head, size_t length, int *status,
	uint32_t *contentLength, bool *hasLength, bool *chunked)
{
	size_t pos;
	size_t lineEnd;

	if(head == NULL || status == NULL || contentLength == NULL ||
		hasLength == NULL || chunked == NULL)
		return false;
	*status = 0;
	*contentLength = 0;
	*hasLength = false;
	*chunked = false;
	if(length < 12u || memcmp(head, "HTTP/", 5) != 0)
		return false;
	pos = 5;
	while(pos < length && head[pos] != ' ' && head[pos] != '\r' && head[pos] != '\n')
		pos++;
	if(pos + 4u > length || head[pos] != ' ')
		return false;
	pos++;
	if(head[pos] < '1' || head[pos] > '5' ||
		head[pos + 1u] < '0' || head[pos + 1u] > '9' ||
		head[pos + 2u] < '0' || head[pos + 2u] > '9')
		return false;
	*status = (head[pos] - '0') * 100 + (head[pos + 1u] - '0') * 10 +
		(head[pos + 2u] - '0');
	pos += 3u;
	if(pos >= length || (head[pos] != ' ' && head[pos] != '\r' && head[pos] != '\n'))
		return false;
	/* To the end of the status line, then one header per line. */
	while(pos < length && head[pos] != '\n')
		pos++;
	if(pos >= length)
		return false;
	pos++;
	for(;;) {
		size_t lineStart = pos;
		size_t colon;

		while(pos < length && head[pos] != '\n')
			pos++;
		if(pos >= length)
			return false; /* the blank line never came */
		lineEnd = pos;
		pos++;
		if(lineEnd > lineStart && head[lineEnd - 1] == '\r')
			lineEnd--;
		if(lineEnd == lineStart)
			return true; /* the blank line: the head is whole */
		colon = lineStart;
		while(colon < lineEnd && head[colon] != ':')
			colon++;
		if(colon >= lineEnd)
			continue; /* not a header this reader knows; the body decides */
		if(headerNameIs(head + lineStart, colon - lineStart, "Content-Length")) {
			size_t value = colon + 1;
			uint32_t parsed;
			while(value < lineEnd && (head[value] == ' ' || head[value] == '\t'))
				value++;
			if(!parseU32(head + value, lineEnd - value, &parsed))
				return false;
			if(*hasLength && parsed != *contentLength)
				return false;
			*contentLength = parsed;
			*hasLength = true;
		}
		else if(headerNameIs(head + lineStart, colon - lineStart,
			"Transfer-Encoding")) {
			if(spanHasWord(head + colon + 1, lineEnd - colon - 1, "chunked"))
				*chunked = true;
		}
	}
}
