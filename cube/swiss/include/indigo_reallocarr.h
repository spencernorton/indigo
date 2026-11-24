#ifndef INDIGO_REALLOCARR_H
#define INDIGO_REALLOCARR_H

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The pinned SDK provides reallocarray, but Swiss calls NetBSD's reallocarr
 * with the address of the caller's pointer. Preserve that pointer on error. */
static inline int indigo_reallocarr(void *address, size_t count, size_t size)
{
	if (!address || !size)
		return EINVAL;
	if (count > SIZE_MAX / size)
		return EOVERFLOW;

	void *old, *next = NULL;
	memcpy(&old, address, sizeof(old));
	if (count) {
		next = realloc(old, count * size);
		if (!next)
			return ENOMEM;
	} else {
		free(old);
	}
	memcpy(address, &next, sizeof(next));
	return 0;
}

#define reallocarr indigo_reallocarr
#endif
