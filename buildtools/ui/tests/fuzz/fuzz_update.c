/* Fuzz what the updater reads (gui/ui_update.c): a release manifest from a
 * server or a share, an HTTP reply's head, and the Update Server text.
 * The bytes are copied to exactly their size, so a read past any of them
 * is a finding. A manifest that parses names safe files within the
 * loader's own size limits and a build time a release can have; a server
 * text that parses is a real IPv4 address and port. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ui_update.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *text = malloc(size + 1);
	ui_update_manifest_t manifest;
	ui_update_base_t base;
	ui_update_base_err_t why;
	ui_update_decision_t decision;
	int status;
	uint32_t length;
	bool hasLength;
	bool chunked;

	if(!text)
		abort();
	memcpy(text, data, size);
	text[size] = '\0';

	if(UIUpdate_ParseManifest(text, size, "stable", &manifest) ==
		UI_UPDATE_OK) {
		if(!UIUpdate_SafeBasename(manifest.file) ||
		   manifest.size == 0 || manifest.size > UI_UPDATE_DOL_MAX ||
		   (manifest.hasApploader &&
		    (!UIUpdate_SafeBasename(manifest.apploaderFile) ||
		     manifest.apploaderSize == 0 ||
		     manifest.apploaderSize > UI_UPDATE_APPLOADER_MAX)) ||
		   manifest.buildTime < INT64_C(946684800))
			abort();
		decision = UIUpdate_Decide(&manifest, "0000000", manifest.buildTime);
		if(decision > UI_UPDATE_DECIDE_OLDER)
			abort();
		(void)UIUpdate_Decide(&manifest, NULL, 0);
	}
	size_t body = UIUpdate_ParseHttpHead(text, size, &status, &length, &hasLength, &chunked);
	if(body > size || (body && (status < 100 || status > 999)))
		abort();
	if(UIUpdate_ParseBase(text, &base, &why) &&
	   (base.port == 0 || base.host[0] == '\0'))
		abort();
	free(text);
	return 0;
}
