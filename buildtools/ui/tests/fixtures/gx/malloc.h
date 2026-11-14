/* Just enough of the target's <malloc.h> for host compiles in
 * test_frame_budget.py: macOS has no <malloc.h>, and its aligned
 * allocation is posix_memalign in <stdlib.h>. When a test driver has
 * already macro-defined memalign to count allocations, leave that
 * definition alone and only supply the standard declarations. */
#ifndef STUB_MALLOC_H
#define STUB_MALLOC_H
#include <stdlib.h>
#ifndef memalign
static inline void *memalign(size_t alignment, size_t size)
{
	void *p = NULL;
	return posix_memalign(&p, alignment, size) ? NULL : p;
}
#endif
#endif
