#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

static bool exhaust;
static void *controlled_resize(void *p, size_t size)
{
	return exhaust ? NULL : realloc(p, size);
}

#define realloc controlled_resize
#include "../../../cube/swiss/include/indigo_reallocarr.h"
#undef realloc

int main(void)
{
	int *items = NULL;
	assert(reallocarr(&items, 2, sizeof(*items)) == 0);
	items[0] = 17;
	items[1] = 23;
	assert(reallocarr(&items, 8, sizeof(*items)) == 0 && items[1] == 23);
	assert(reallocarr(&items, 1, sizeof(*items)) == 0 && items[0] == 17);
	int *before = items;
	exhaust = true;
	assert(reallocarr(&items, 5, sizeof(*items)) == ENOMEM && items == before);
	assert(reallocarr(&items, SIZE_MAX, sizeof(*items)) == EOVERFLOW && items == before);
	assert(reallocarr(&items, 1, 0) == EINVAL && items[0] == 17);
	assert(reallocarr(&items, 0, sizeof(*items)) == 0 && items == NULL);
	assert(reallocarr(&items, 0, sizeof(*items)) == 0 && items == NULL);
	return 0;
}
