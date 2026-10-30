#include <new>
#include "unwind_check.h"

namespace {
volatile int live;
volatile int destroyed;

struct Guard {
	Guard() { live = live + 1; }
	~Guard() { live = live - 1; destroyed = destroyed + 1; }
};

struct Marker { int value; };

__attribute__((noinline)) void raise_marker(int value)
{
	Guard guard;
	throw Marker{value};
}

__attribute__((noinline)) void relay(int value)
{
	Guard guard;
	try {
		raise_marker(value);
	} catch(const Marker &) {
		throw;
	}
}
}

/* Exercise the actual target exception runtime, including rethrow, typed
 * catches and cleanup across frames. Repetition uses registered FDE lookups
 * after their initial cache population. The linker-generated index is absent;
 * the real .eh_frame, personalities and CRT registration must still work. */
int unwind_check(void)
{
	for(int value = 1; value <= 16; ++value) {
		live = 0;
		destroyed = 0;
		try {
			relay(value);
			return 0;
		} catch(const Marker &marker) {
			if(marker.value != value || live != 0 || destroyed != 2)
				return 0;
		} catch(...) {
			return 0;
		}
	}
	try {
		throw std::bad_alloc();
	} catch(const std::bad_alloc &) {
		return 1;
	} catch(...) {
		return 0;
	}
}
