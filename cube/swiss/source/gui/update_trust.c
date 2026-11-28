#include <sodium/crypto_sign_ed25519.h>
#include "update_trust.h"

bool updateReadSigned(const char *bytes, size_t size, const char *channel,
	ui_update_manifest_t *manifest)
{
	uint8_t key[crypto_sign_ed25519_PUBLICKEYBYTES];
	if(bytes == NULL || size <= UPDATE_SIGNATURE_BYTES || size > UPDATE_ENVELOPE_MAX ||
		!UIUpdate_HexToBytes32(INDIGO_UPDATE_PUBLIC_KEY, key))
		return false;
	const char *text = bytes + UPDATE_SIGNATURE_BYTES;
	size_t length = size - UPDATE_SIGNATURE_BYTES;
	if(crypto_sign_ed25519_verify_detached((const unsigned char *)bytes,
		(const unsigned char *)text, length, key) != 0)
		return false;
	ui_update_err_t result = UIUpdate_ParseManifest(text, length, channel ? channel : "stable", manifest);
	if(channel == NULL && result == UI_UPDATE_CHANNEL)
		result = UIUpdate_ParseManifest(text, length, "rc", manifest);
	return result == UI_UPDATE_OK;
}
