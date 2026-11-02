# The emulator test's probe (probe.c). Built in the toolchain image, as the DOL is:
#   make -C buildtools/ui/emulator/probe
include $(DEVKITPRO)/libogc2/gamecube_rules

ICONV := $(abspath ../../../../.build-deps/libiconv-lite)
CFLAGS := -O2 -Wall -Wextra -Werror $(MACHDEP) -I$(LIBOGC_INC) -I$(ICONV)/include
CXXFLAGS := $(CFLAGS) -fexceptions
EH_FRAME_FLAGS ?= -Wl,--no-eh-frame-hdr

probe.dol: probe.elf
probe.elf: probe.o unwind_check.o
	$(CXX) $(CXXFLAGS) $^ $(MACHDEP) $(EH_FRAME_FLAGS) -L$(LIBOGC_LIB) -logc -lm $(ICONV)/lib/libiconv.a -o $@

probe.o: probe.c probe_grid.h iconv_check.h unwind_check.h iconv-overlay
	$(CC) $(CFLAGS) -c $< -o $@

unwind_check.o: unwind_check.cpp unwind_check.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

iconv-overlay:
	../../../ci/runner/libiconv-lite.sh "$(ICONV)"

clean:
	rm -f probe.elf probe.dol probe.o unwind_check.o

.PHONY: clean iconv-overlay
