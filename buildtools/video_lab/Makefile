.DEFAULT_GOAL := video-lab.dol
include $(DEVKITPRO)/libogc2/gamecube_rules

REVISION ?= unversioned
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -Werror $(MACHDEP) -I$(LIBOGC_INC)
CFLAGS += -DVL_REVISION=\"$(REVISION)\" -ffile-prefix-map=$(CURDIR)=.
LDFLAGS := $(MACHDEP) -Wl,--no-eh-frame-hdr -L$(LIBOGC_LIB)

video-lab.dol: video-lab.elf
video-lab.elf: main.o core.o
	$(CC) $^ $(LDFLAGS) -lbba -logc -lm -o $@

main.o: main.c core.h
	$(CC) $(CFLAGS) -c $< -o $@
core.o: core.c core.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f main.o core.o video-lab.elf video-lab.dol

.PHONY: clean
