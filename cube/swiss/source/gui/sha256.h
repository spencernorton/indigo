#ifndef SHA256_H
#define SHA256_H

/* SHA-256 (FIPS 180-4) for the software updater's download checks
 * (gui/ui_update.h). Based on Brad Conte's public-domain implementation
 * ("Written in 2014 by Brad Conte... dedicated to the public domain"),
 * retyped with stdint types; the known-answer tests in
 * buildtools/ui/tests/test_ui_update.c pin every constant. */

#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE 32 /* SHA256 outputs a 32 byte digest */

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t hash[SHA256_BLOCK_SIZE]);

#endif /* SHA256_H */
