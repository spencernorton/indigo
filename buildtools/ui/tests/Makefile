# Host builds for the combined UI test suite (never part of the DOL build).
CC ?= cc
GUI := ../../../cube/swiss/source/gui
CHEATS := ../../../cube/swiss/source/cheats
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -g
SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer
# Hosts with vm.mmap_rnd_bits=32 (high ASLR entropy) break this: Debian PIE sanitizer executables can land
# inside ASan's shadow range and recursively fault before main; non-PIE keeps
# the sanitizer mapping deterministic. Darwin's linker does not use this flag.
ifeq ($(shell uname -s),Linux)
SANFLAGS += -fno-pie -no-pie
endif
GAMEFLOW_CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion \
	-Wsign-conversion -pedantic

# Every C test, once, in the order run_tests.sh runs them (make -s list).
# A test builds <name>.c (or <name>.main) with the sources in <name>.src,
# the flags in <name>.flags (GAMEFLOW_CFLAGS unless set) and the libraries in
# <name>.libs: plain as <name>, under ASan/UBSan as <name>_san.
TESTS := test_ui_browser_launch test_ui_cheats test_ui_saves test_ui_saves_raw test_ui_saves_metadata test_ui_saves_details test_ui_save_cubes test_ui_apps test_ui_png \
	test_ui_assets test_ui_assets_target_sync \
	test_ui_settings_layout test_ui_settings_focus test_ui_system_info \
	test_ui_menu_input test_ui_clock test_ui_hint test_ui_stage \
	test_ui_home test_ui_cube_motif test_ui_home_safety test_ui_home_layout \
	test_ui_scene test_ui_command_rail test_ui_presentation \
	test_gameflow_state test_gameflow_library test_ui_folder test_gameflow_resolver \
	test_gameflow_detail test_ui_game_history test_ui_about \
	test_gameflow_ownership test_cheat_policy test_ui_launch test_ui_files \
	test_ui_card_slots test_ui_hint_geometry test_ui_time_sync test_ui_lab test_indigo_log test_ui_manual

test_ui_browser_launch.src := $(GUI)/ui_browser_launch.c $(GUI)/ui_time_sync.c
test_ui_cheats.src := $(GUI)/ui_cheats.c
test_ui_saves.src := $(GUI)/ui_saves.c
test_ui_saves_raw.src := $(GUI)/ui_saves_raw.c $(GUI)/ui_saves.c
test_ui_saves_metadata.src := $(GUI)/ui_saves_metadata.c $(GUI)/ui_saves.c
test_ui_saves_details.src := $(GUI)/ui_saves_details.c
# Memory Cards' screen: its cubes, their motion and the cursor.
test_ui_save_cubes.src := $(GUI)/ui_save_cubes.c $(GUI)/ui_motion.c $(GUI)/ui_saves.c $(GUI)/ui_folder.c
test_ui_save_cubes.libs := -lm
test_ui_apps.src := $(GUI)/ui_apps.c
# test_ui_png.py drives these with the pictures it writes. test_ui_png.c
# builds ui_png.c in, to count what it allocates for the memory tests.
test_ui_png.flags = $(GAMEFLOW_CFLAGS) -DUI_PNG_COUNT
test_ui_png.libs := -lz -lm
test_ui_assets.src := $(GUI)/ui_assets.c
test_ui_assets.flags = $(CFLAGS) -DUI_ASSETS_HOST_BUILD
test_ui_assets.libs := -lz
# Host-compiled policy variant: exercises the target requirement that Init
# receive a complete external critical-section pair without needing libogc.
test_ui_assets_target_sync.main := test_ui_assets.c
test_ui_assets_target_sync.src := $(GUI)/ui_assets.c
test_ui_assets_target_sync.flags = $(CFLAGS) -DUI_ASSETS_HOST_BUILD \
	-DUI_ASSETS_HOST_REQUIRE_SYNC
test_ui_assets_target_sync.libs := -lz
# Settings layout module: strict C99. ui_cheats is the cheat browser whose
# window the pages share.
test_ui_settings_layout.src := $(GUI)/ui_settings_layout.c $(GUI)/ui_hint.c \
	$(GUI)/ui_cheats.c
test_ui_settings_layout.flags := -std=c99 -Wall -Wextra -Werror -pedantic
test_ui_settings_focus.src := $(GUI)/ui_settings_focus.c $(GUI)/ui_motion.c
test_ui_settings_focus.libs := -lm
test_ui_system_info.src := $(GUI)/ui_system_info.c
test_ui_menu_input.src := $(GUI)/ui_menu_input.c
test_ui_clock.src := $(GUI)/ui_clock.c
test_ui_time_sync.src := $(GUI)/ui_time_sync.c
# Memory card slots around a GameID switch (indigo#102), against scripted cards.
test_ui_card_slots.src := $(GUI)/ui_card_slots.c
test_ui_clock.libs := -lm
test_ui_hint.src := $(GUI)/ui_hint.c
test_ui_hint_geometry.libs := -lm
test_ui_stage.src := $(GUI)/ui_stage.c
test_ui_stage.libs := -lm
test_ui_home.src := $(GUI)/ui_home.c
test_ui_cube_motif.src := $(GUI)/ui_cube_motif.c $(GUI)/ui_home.c
test_ui_cube_motif.libs := -lm
test_ui_home_safety.src := $(GUI)/ui_home_safety.c
test_ui_home_layout.src := $(GUI)/ui_home_layout.c $(GUI)/ui_home_text.c \
	$(GUI)/ui_home.c
test_ui_scene.src := $(GUI)/ui_scene.c $(GUI)/ui_motion.c $(GUI)/ui_home.c \
	$(GUI)/ui_cube_motif.c
test_ui_scene.libs := -lm
test_ui_command_rail.src := $(GUI)/ui_command_rail.c
test_ui_command_rail.libs := -lm
test_ui_presentation.src := $(GUI)/ui_presentation.c
# Retained Gameflow suites.
test_gameflow_state.src := $(GUI)/ui_gameflow.c $(GUI)/ui_motion.c
test_gameflow_state.libs := -lm
test_gameflow_library.src := $(GUI)/ui_gameflow_library.c
test_ui_folder.src := $(GUI)/ui_folder.c
test_gameflow_resolver.src := $(GUI)/ui_gameflow_resolver.c \
	$(GUI)/ui_gameflow_library.c
test_gameflow_detail.src := $(GUI)/ui_gameflow_detail.c $(GUI)/ui_game_history.c \
	$(GUI)/ui_saves_metadata.c $(GUI)/ui_saves.c
test_ui_game_history.src := $(GUI)/ui_game_history.c
# Spotlight's game descriptions (swiss/ui/descriptions.txt).
test_ui_about.src := $(GUI)/ui_about.c
test_gameflow_ownership.src := $(GUI)/ui_gameflow_ownership.c
test_cheat_policy.src := $(CHEATS)/cheat_policy.c
test_cheat_policy.flags = $(GAMEFLOW_CFLAGS) -I$(CHEATS)
# The launch screen's steps; it reads the Swiss sources it pins.
test_ui_launch.src := $(GUI)/ui_launch.c
# A game's manual: its checks, the viewer's page slots, where a page draws.
# test_manual_pack.py also runs it on manuals manual_pack.py writes.
test_ui_manual.src := $(GUI)/ui_manual.c
test_ui_manual.libs := -lz -lm
# The File Browser's two panes; ui_hint counts the hint lines' buttons.
test_ui_files.src := $(GUI)/ui_files.c $(GUI)/ui_hint.c
# The console lab's requests and settings, and the log it serves.
test_ui_lab.src := $(GUI)/ui_lab.c
test_indigo_log.src := $(GUI)/indigo_log.c

# A header change rebuilds every test; run_tests.sh builds from clean anyway.
HEADERS := $(wildcard $(GUI)/*.h $(CHEATS)/*.h)

# $(1): the test, $(2): the binary's suffix, $(3): extra flags.
define c_test
$(1)$(2): $$(or $$($(1).main),$(1).c) $$($(1).src) $$(HEADERS)
	$$(CC) $$(or $$($(1).flags),$$(GAMEFLOW_CFLAGS)) $(3) -I$$(GUI) -o $$@ \
		$$(or $$($(1).main),$(1).c) $$($(1).src) $$($(1).libs)
endef
$(foreach t,$(TESTS),$(eval $(call c_test,$(t),,))$(eval $(call c_test,$(t),_san,$$(SANFLAGS))))

all: plain sanitized

plain: $(TESTS)

sanitized: $(TESTS:=_san)

list:
	@echo $(TESTS)

clean:
	rm -f $(TESTS) $(TESTS:=_san)
	rm -rf *.dSYM

.PHONY: all plain sanitized list clean
