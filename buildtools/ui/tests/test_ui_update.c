#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "ui_update.h"

static unsigned long checks;

#define CHECK(condition) do { checks++; if(!(condition)) { \
	fprintf(stderr, "check failed at %s:%d: %s\n", \
		__FILE__, __LINE__, #condition); exit(1); } } while(0)

/* FIPS 180-4's own answers, and the long-message one. */
static void hashEquals(const uint8_t *data, size_t length,
	const char *expectedHex)
{
	sha256_ctx ctx;
	uint8_t digest[32];
	uint8_t expected[32];

	sha256_init(&ctx);
	sha256_update(&ctx, data, length);
	sha256_final(&ctx, digest);
	CHECK(UIUpdate_HexToBytes32(expectedHex, expected));
	CHECK(memcmp(digest, expected, 32) == 0);
}

static void testSha256(void)
{
	static const char message56[] =
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	sha256_ctx oneShot;
	sha256_ctx streamed;
	uint8_t whole[32];
	uint8_t pieces[32];
	uint8_t *million;
	size_t offset;

	hashEquals((const uint8_t *)"", 0,
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	hashEquals((const uint8_t *)"abc", 3,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	hashEquals((const uint8_t *)message56, sizeof(message56) - 1,
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	million = malloc(1000000u);
	CHECK(million != NULL);
	memset(million, 'a', 1000000u);
	hashEquals(million, 1000000u,
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	/* The same bytes in odd pieces hash the same: a download hashes as it
	 * lands, in whatever pieces the network hands over. */
	sha256_init(&oneShot);
	sha256_update(&oneShot, million, 1000000u);
	sha256_final(&oneShot, whole);
	sha256_init(&streamed);
	for(offset = 0; offset < 1000000u; ) {
		size_t piece = 999u - (offset % 7u);
		if(piece > 1000000u - offset)
			piece = 1000000u - offset;
		sha256_update(&streamed, million + offset, piece);
		offset += piece;
	}
	sha256_final(&streamed, pieces);
	CHECK(memcmp(whole, pieces, 32) == 0);
	free(million);
}

static const char MANIFEST[] =
	"FormatVersion=1\n"
	"Channel=stable\n"
	"Version=v2.10.0\n"
	"Commit=0123456789abcdef0123456789abcdef01234567\n"
	"Date=2026-12-01\n"
	"BuildTime=1796000000\n"
	"File=indigo-v2.10.0.dol\n"
	"Size=4500000\n"
	"SHA256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
	"ApploaderFile=apploader-v2.10.0.img\n"
	"ApploaderSize=123456\n"
	"ApploaderSHA256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
	"NotesURL=https://github.com/spencernorton/indigo/releases/tag/v2.10.0\n";

static ui_update_err_t parse(const char *text, const char *channel,
	ui_update_manifest_t *out)
{
	return UIUpdate_ParseManifest(text, strlen(text), channel, out);
}

/* The manifest with one line's value changed, or the line dropped when the
 * value is NULL. `key` includes its '='; the line must start with it, so
 * "Version=" never matches inside "FormatVersion=". */
static ui_update_err_t parseEdited(const char *key, const char *value,
	const char *channel, ui_update_manifest_t *out)
{
	static char edited[8192];
	char anchored[64];
	const char *line;
	const char *lineEnd;
	size_t before;

	if(strncmp(MANIFEST, key, strlen(key)) == 0) {
		line = MANIFEST;
	}
	else {
		anchored[0] = '\n';
		strcpy(anchored + 1, key);
		line = strstr(MANIFEST, anchored);
		if(line != NULL)
			line++;
	}
	if(line == NULL) {
		fprintf(stderr, "test bug: no line %s\n", key);
		exit(1);
	}
	lineEnd = strchr(line, '\n');
	before = (size_t)(line - MANIFEST);
	memcpy(edited, MANIFEST, before);
	edited[before] = '\0';
	if(value != NULL) {
		strcat(edited, key);
		strcat(edited, value);
		strcat(edited, "\n");
	}
	strcat(edited, lineEnd + 1);
	return parse(edited, channel, out);
}

static void testManifest(void)
{
	ui_update_manifest_t manifest;
	char crlf[8192];
	char *rest;
	size_t i;

	CHECK(parse(MANIFEST, "stable", &manifest) == UI_UPDATE_OK);
	CHECK(strcmp(manifest.version, "v2.10.0") == 0);
	CHECK(strcmp(manifest.commit, "0123456789abcdef0123456789abcdef01234567") == 0);
	CHECK(strcmp(manifest.date, "2026-12-01") == 0);
	CHECK(manifest.buildTime == INT64_C(1796000000));
	CHECK(strcmp(manifest.file, "indigo-v2.10.0.dol") == 0);
	CHECK(manifest.size == 4500000u);
	for(i = 0; i < 32u; i++)
		CHECK(manifest.sha256[i] == 0xAAu);
	CHECK(manifest.hasApploader);
	CHECK(strcmp(manifest.apploaderFile, "apploader-v2.10.0.img") == 0);
	CHECK(manifest.apploaderSize == 123456u);
	for(i = 0; i < 32u; i++)
		CHECK(manifest.apploaderSha256[i] == 0xBBu);
	CHECK(!manifest.hasMinBuild);
	CHECK(strstr(manifest.notesUrl, "v2.10.0") != NULL);

	/* CRLF reads the same as LF, and so does no newline at the end. */
	crlf[0] = '\0';
	rest = crlf;
	for(i = 0; MANIFEST[i] != '\0'; i++) {
		if(MANIFEST[i] == '\n')
			*rest++ = '\r';
		*rest++ = MANIFEST[i];
	}
	*rest = '\0';
	CHECK(parse(crlf, "stable", &manifest) == UI_UPDATE_OK);
	strcpy(crlf, MANIFEST);
	crlf[strlen(crlf) - 1] = '\0';
	CHECK(parse(crlf, "stable", &manifest) == UI_UPDATE_OK);

	/* Every line the format requires, missing, is a broken manifest. */
	CHECK(parseEdited("FormatVersion=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Channel=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Version=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Commit=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Date=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("BuildTime=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("File=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Size=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("SHA256=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);

	/* A newer format and another channel get their own answers. */
	CHECK(parseEdited("FormatVersion=", "2", "stable", &manifest) == UI_UPDATE_FORMAT);
	CHECK(parseEdited("FormatVersion=", "0", "stable", &manifest) == UI_UPDATE_FORMAT);
	CHECK(parseEdited("Channel=", "rc", "stable", &manifest) == UI_UPDATE_CHANNEL);
	CHECK(parseEdited("Channel=", "rc", "rc", &manifest) == UI_UPDATE_OK);
	CHECK(parse(MANIFEST, "rc", &manifest) == UI_UPDATE_CHANNEL);

	/* A key twice is broken, however harmless the second copy looks. */
	strcpy(crlf, MANIFEST);
	strcat(crlf, "Version=v2.10.0\n");
	CHECK(parse(crlf, "stable", &manifest) == UI_UPDATE_INVALID);

	/* A line the format does not know yet is skipped, not an error: a
	 * later manifest may carry a signature this build cannot check. */
	strcpy(crlf, MANIFEST);
	strcat(crlf, "Signature=whatever-comes-next\n");
	CHECK(parse(crlf, "stable", &manifest) == UI_UPDATE_OK);

	/* The file names are used as names: nothing that walks anywhere. */
	CHECK(parseEdited("File=", "../indigo.dol", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("File=", "sub/indigo.dol", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("File=", ".indigo.dol", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("File=", "indigo dol.dol", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("File=", "", "stable", &manifest) == UI_UPDATE_INVALID);

	/* Sizes: something, and never more than the loader itself reads. */
	CHECK(parseEdited("Size=", "0", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Size=", "16777217", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Size=", "16777216", "stable", &manifest) == UI_UPDATE_OK);
	CHECK(parseEdited("Size=", "-1", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Size=", "4500000 ", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("ApploaderSize=", "8388609", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("ApploaderSize=", "8388608", "stable", &manifest) == UI_UPDATE_OK);
	CHECK(parseEdited("ApploaderSize=", "2454656", "stable", &manifest) == UI_UPDATE_OK);

	/* The hash is 64 lower-case hex letters, no more, no less. */
	CHECK(parseEdited("SHA256=", "aaa", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("SHA256=", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("SHA256=", "gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg", "stable", &manifest) == UI_UPDATE_INVALID);

	/* The commit is 40 lower-case hex; the version starts v and a digit. */
	CHECK(parseEdited("Commit=", "0123456789abcdef", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Version=", "2.10.0", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("Version=", "vX", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("BuildTime=", "yesterday", "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("BuildTime=", "12345", "stable", &manifest) == UI_UPDATE_INVALID);

	/* The apploader's three lines travel together or not at all. */
	CHECK(parseEdited("ApploaderSHA256=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	CHECK(parseEdited("ApploaderSize=", NULL, "stable", &manifest) == UI_UPDATE_INVALID);
	{
		static char dolOnly[8192];
		const char *app = strstr(MANIFEST, "ApploaderFile=");
		const char *notes = strstr(MANIFEST, "NotesURL=");
		size_t head = (size_t)(app - MANIFEST);
		memcpy(dolOnly, MANIFEST, head);
		dolOnly[head] = '\0';
		strcat(dolOnly, notes);
		CHECK(parse(dolOnly, "stable", &manifest) == UI_UPDATE_OK);
		CHECK(!manifest.hasApploader);
	}

	/* Over the manifest's size, the caller never hands it over; the
	 * reader refuses too, and so does it refuse a NUL inside. */
	CHECK(UIUpdate_ParseManifest(MANIFEST, UI_UPDATE_MANIFEST_MAX + 1u,
		"stable", &manifest) == UI_UPDATE_INVALID);
	strcpy(crlf, MANIFEST);
	crlf[10] = '\0';
	CHECK(UIUpdate_ParseManifest(crlf, strlen(MANIFEST), "stable",
		&manifest) == UI_UPDATE_INVALID);
}

static void testDecide(void)
{
	ui_update_manifest_t manifest;

	CHECK(parse(MANIFEST, "stable", &manifest) == UI_UPDATE_OK);
	/* The same commit is up to date, whatever the clocks say. */
	CHECK(UIUpdate_Decide(&manifest, "0123456",
		manifest.buildTime - 100) == UI_UPDATE_DECIDE_CURRENT);
	CHECK(UIUpdate_Decide(&manifest, "0123456",
		manifest.buildTime + 100) == UI_UPDATE_DECIDE_CURRENT);
	/* Another commit built later is an update; built earlier, an older
	 * build; built at the same second, still not "newer". */
	CHECK(UIUpdate_Decide(&manifest, "89abcde",
		manifest.buildTime - 1) == UI_UPDATE_DECIDE_NEWER);
	CHECK(UIUpdate_Decide(&manifest, "89abcde",
		manifest.buildTime + 1) == UI_UPDATE_DECIDE_OLDER);
	CHECK(UIUpdate_Decide(&manifest, "89abcde",
		manifest.buildTime) == UI_UPDATE_DECIDE_OLDER);
	/* A stepping-stone floor refuses before anything else is weighed. */
	CHECK(parseEdited("BuildTime=", "1796000000\nMinVersion=v2.8.0\nMinBuildTime=1790000000",
		"stable", &manifest) == UI_UPDATE_OK);
	CHECK(manifest.hasMinBuild);
	CHECK(strcmp(manifest.minVersion, "v2.8.0") == 0);
	CHECK(UIUpdate_Decide(&manifest, "89abcde",
		INT64_C(1789999999)) == UI_UPDATE_DECIDE_MIN_BUILD);
	CHECK(UIUpdate_Decide(&manifest, "89abcde",
		INT64_C(1790000000)) == UI_UPDATE_DECIDE_NEWER);
}

static void baseIs(const char *text, bool ok, ui_update_base_err_t why,
	const char *host, uint16_t port, const char *path)
{
	ui_update_base_t base;
	ui_update_base_err_t reason = UI_UPDATE_BASE_BAD;

	CHECK(UIUpdate_ParseBase(text, &base, &reason) == ok);
	if(!ok) {
		CHECK(reason == why);
		return;
	}
	CHECK(strcmp(base.host, host) == 0);
	CHECK(base.port == port);
	CHECK(strcmp(base.path, path) == 0);
}

static void testParseBase(void)
{
	baseIs("192.168.1.10", true, UI_UPDATE_BASE_OK, "192.168.1.10", 80u, "/");
	baseIs("192.168.1.10:8080", true, UI_UPDATE_BASE_OK, "192.168.1.10", 8080u, "/");
	baseIs("192.168.1.10:8080/indigo", true, UI_UPDATE_BASE_OK, "192.168.1.10", 8080u, "/indigo/");
	baseIs("192.168.1.10/indigo/", true, UI_UPDATE_BASE_OK, "192.168.1.10", 80u, "/indigo/");
	baseIs("http://192.168.1.10", true, UI_UPDATE_BASE_OK, "192.168.1.10", 80u, "/");
	baseIs("http://10.0.0.2:80/a/b-1.2_x~y", true, UI_UPDATE_BASE_OK, "10.0.0.2", 80u, "/a/b-1.2_x~y/");
	/* Leading zeros are read as written, so the address shown is the
	 * address used (inet_aton alone would read octal). */
	baseIs("010.0.0.1", true, UI_UPDATE_BASE_OK, "10.0.0.1", 80u, "/");

	baseIs("https://192.168.1.10", false, UI_UPDATE_BASE_HTTPS, "", 0u, "");
	baseIs("example.com", false, UI_UPDATE_BASE_NAME, "", 0u, "");
	baseIs("indigo.local/indigo", false, UI_UPDATE_BASE_NAME, "", 0u, "");
	baseIs("", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.256", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1:0", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1:65536", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1:65535", true, UI_UPDATE_BASE_OK, "192.168.1.1", 65535u, "/");
	baseIs("192.168.1.1/../etc", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1/a/../b", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1/a b", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1/?x=1", false, UI_UPDATE_BASE_BAD, "", 0u, "");
	baseIs("192.168.1.1:8080x", false, UI_UPDATE_BASE_BAD, "", 0u, "");
}

static void headIs(const char *text, bool ok, int status,
	uint32_t contentLength, bool hasLength, bool chunked)
{
	int gotStatus = -1;
	uint32_t gotLength = 0;
	bool gotHas = false;
	bool gotChunked = false;

	CHECK(UIUpdate_ParseHttpHead(text, strlen(text), &gotStatus,
		&gotLength, &gotHas, &gotChunked) == ok);
	if(ok) {
		CHECK(gotStatus == status);
		CHECK(gotLength == contentLength);
		CHECK(gotHas == hasLength);
		CHECK(gotChunked == chunked);
	}
}

static void testHttpHead(void)
{
	headIs("HTTP/1.1 200 OK\r\nContent-Length: 4500000\r\n\r\n",
		true, 200, 4500000u, true, false);
	headIs("HTTP/1.0 200 OK\ncontent-length: 12\n\n",
		true, 200, 12u, true, false);
	headIs("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
		true, 200, 0u, false, true);
	headIs("HTTP/1.1 301 Moved\r\nLocation: https://example.test/\r\nContent-Length: 0\r\n\r\n",
		true, 301, 0u, true, false);
	headIs("HTTP/1.1 404 Not Found\r\n\r\n", true, 404, 0u, false, false);
	headIs("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n",
		true, 200, 5u, true, false);
	/* Two lengths that disagree are not a head to trust. */
	headIs("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n",
		false, 0, 0u, false, false);
	/* No blank line yet: not finished, whatever else it says. */
	headIs("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n",
		false, 0, 0u, false, false);
	headIs("not http at all\r\n\r\n", false, 0, 0u, false, false);
	headIs("HTTP/1.1 200 OK\r\nContent-Length: soon\r\n\r\n",
		false, 0, 0u, false, false);
}

static void testHexAndNames(void)
{
	uint8_t hash[32];
	char text[17];
	size_t i;

	CHECK(UIUpdate_HexToBytes32(
		"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
		hash));
	for(i = 0; i < 32u; i++)
		CHECK(hash[i] == (uint8_t)i);
	UIUpdate_Hex16(hash, text);
	CHECK(strcmp(text, "0001020304050607") == 0);
	CHECK(!UIUpdate_HexToBytes32("0001", hash));
	CHECK(!UIUpdate_HexToBytes32(
		"000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
		hash));

	CHECK(strcmp(UIUpdate_ChannelName(0), "stable") == 0);
	CHECK(strcmp(UIUpdate_ChannelName(1), "rc") == 0);

	CHECK(UIUpdate_SafeBasename("indigo-v2.10.0.dol"));
	CHECK(!UIUpdate_SafeBasename(""));
	CHECK(!UIUpdate_SafeBasename(".."));
	CHECK(!UIUpdate_SafeBasename("a/b"));
	CHECK(!UIUpdate_SafeBasename(NULL));
}

int main(void)
{
	testSha256();
	testManifest();
	testDecide();
	testParseBase();
	testHttpHead();
	testHexAndNames();
	printf("test_ui_update: %lu checks passed\n", checks);
	return 0;
}
