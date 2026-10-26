#!/bin/sh
# run_tests.sh -- deterministic host suite with independently schedulable lanes.
#
# Usage: run_tests.sh [all|plain|sanitized|contracts]
# Needs: Python 3 + Pillow, a C compiler, zlib, and (when strict) gxtexconv.
set -eu
cd "$(dirname "$0")"

SUITE="${1:-all}"
case "$SUITE" in
	all|plain|sanitized|contracts) ;;
	*)
		echo "usage: $0 [all|plain|sanitized|contracts]" >&2
		exit 2
		;;
esac

# The structural audits intentionally use Python assertions today. Never let
# PYTHONOPTIMIZE silently turn those checks into no-ops.
python3 - <<'PY'
import sys

if not __debug__:
    print("host suite refuses optimized Python; assertions would be disabled", file=sys.stderr)
    raise SystemExit(2)
PY

detect_jobs() {
	if command -v nproc >/dev/null 2>&1; then
		nproc
	elif command -v getconf >/dev/null 2>&1; then
		getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2
	else
		echo 2
	fi
}

JOBS="${UI_TEST_JOBS:-$(detect_jobs)}"
if ! printf '%s\n' "$JOBS" | grep -Eqx '0*[1-9][0-9]*'; then
	echo "UI_TEST_JOBS must be a positive integer (got: $JOBS)" >&2
	exit 2
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

build_binaries() {
	target=$1
	echo "== clean parallel host build: $target ($JOBS jobs) =="
	make -s clean
	make -s -j "$JOBS" "$target"
}

make_fixture() {
	if python3 fixture_pack.py "$TMP/fixture.pak" "$TMP/fixture-stills.pak"; then
		return 0
	else
		status=$?
	fi
	if [ "$status" -ne 3 ]; then
		return "$status"
	fi
	if [ "${UI_TEST_REQUIRE_GXTEXCONV:-0}" = "1" ]; then
		echo "real-pak pass required, but gxtexconv is unavailable" >&2
		return 3
	fi
	echo "real-pak pass skipped (gxtexconv unavailable; set UI_TEST_REQUIRE_GXTEXCONV=1 to fail)"
	return 3
}

# Every C test in the Makefile's list, plain ("") or under ASan/UBSan (_san).
run_binaries() {
	suffix=$1
	fixtures=
	echo "== game folder navigation$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_gameflow_folder_navigation.py; else python3 ./test_gameflow_folder_navigation.py --sanitize; fi
	echo "== a game's Detail from the File Browser: its window$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_files_detail.py; else python3 ./test_files_detail.py --sanitize; fi
	if make_fixture; then
		fixtures="$TMP/fixture.pak $TMP/fixture-stills.pak"
	else
		status=$?
		if [ "$status" -ne 3 ] || [ "${UI_TEST_REQUIRE_GXTEXCONV:-0}" = "1" ]; then
			return "$status"
		fi
	fi
	for test in $(make -s list); do
		echo "== $test$suffix =="
		# shellcheck disable=SC2086 # $fixtures is two paths or none
		case $test in
			test_ui_png) python3 ./test_ui_png.py "./$test$suffix" ;;
			test_ui_assets|test_ui_assets_target_sync) "./$test$suffix" $fixtures ;;
			# A manual: its unit tests, then manual_pack.py's files through it.
			test_ui_manual) "./$test$suffix"; python3 ./test_manual_pack.py "./$test$suffix" ;;
			*) "./$test$suffix" ;;
		esac
	done
	echo "== raw card device adapter$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_saves_raw_backend.py; else python3 ./test_saves_raw_backend.py --sanitize; fi
	echo "== raw card writer$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_saves_raw_writer.py; else python3 ./test_saves_raw_writer.py --sanitize; fi
	echo "== game save stats$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_saves_stats.py; else python3 ./test_saves_stats.py --sanitize; fi
	echo "== Game Detail's save choice$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_save_choice.py; else python3 ./test_save_choice.py --sanitize; fi
	echo "== memory card slots$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_card_slots.py; else python3 ./test_card_slots.py --sanitize; fi
	if [ -z "$suffix" ]; then python3 ./test_card_trace_export.py; else python3 ./test_card_trace_export.py --sanitize; fi
	echo "== manual MP3 player$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_mp3_player.py; else python3 ./test_mp3_player.py --sanitize; fi
	echo "== uploads: exact stream and safe handoff$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_wiiload.py; else python3 ./test_wiiload.py --sanitize; fi
	echo "== a DOL or ELF cut short never starts$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_boot_image.py; else python3 ./test_boot_image.py --sanitize; fi
	# card_art builds its own binaries: posters of apps and folders, their guards.
	echo "== test_card_art$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_card_art.py; else python3 ./test_card_art.py --sanitize; fi
	echo "== Spotlight stills: only on first use$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_stills_lifecycle.py; else python3 ./test_stills_lifecycle.py --sanitize; fi
	echo "== font batches: the release's exact glyph stream$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_font_batches.py; else python3 ./test_font_batches.py --sanitize; fi
	echo "== game folder visibility$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_gameflow_folder_visibility.py; else python3 ./test_gameflow_folder_visibility.py --sanitize; fi
	echo "== test_saves_card_io$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_saves_card_io.py; else python3 ./test_saves_card_io.py --sanitize; fi
	echo "== test_saves_raw_controller$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_saves_raw_controller.py; else python3 ./test_saves_raw_controller.py --sanitize; fi
	echo "== upstream console identity$suffix =="
	if [ -z "$suffix" ]; then python3 ./test_upstream_console.py; else python3 ./test_upstream_console.py --sanitize; fi
}

# The fuzzers (fuzz/) must build on every change, since a Fuzz run is not
# required. They need clang with libFuzzer; UI_TEST_REQUIRE_FUZZERS=1 makes
# a missing one an error rather than a skip.
build_fuzzers() {
	echo "== the fuzzers build =="
	if printf 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n) { (void)d; (void)n; return 0; }\n' |
		"${FUZZ_CC:-clang}" -x c -fsanitize=fuzzer -o "$TMP/probe" - >/dev/null 2>&1; then
		./fuzz/run_fuzz.sh --build-only "$TMP/fuzz"
	elif [ "${UI_TEST_REQUIRE_FUZZERS:-0}" = "1" ]; then
		echo "fuzzers required, but clang with libFuzzer is unavailable" >&2
		return 3
	else
		echo "fuzzer build skipped (no clang with libFuzzer; set UI_TEST_REQUIRE_FUZZERS=1 to fail)"
	fi
}

run_contracts() {
	build_fuzzers
	echo "== play-history device faults and handoff boundaries =="
	python3 ./test_history_persistence.py
	echo "== cheat panel GX vertex stream and geometry =="
	python3 ./test_cheats_gx_stream.py
	echo "== Source picker: GX stream, sliding row, both screen shapes =="
	python3 ./test_source_picker_gx_stream.py
	echo "== Home: its name, hint and rows fade in, never pop =="
	python3 ./test_home_gx_stream.py
	echo "== Settings and Memory Cards: the current tab's mark slides =="
	python3 ./test_page_tabs_gx_stream.py
	echo "== native stroke GX stream and perspective coverage =="
	python3 ./test_stroke_gx_stream.py
	echo "== wave and grid native coverage =="
	python3 ./test_background_gx_stream.py
	echo "== turned faces' icon pictures: doubled, copied, laid on the glass =="
	python3 ./test_face_pictures.py
	echo "== cube glass light: refraction, dispersion, bloom, rim, glint =="
	python3 ./test_glass_light.py
	echo "== display copy clear and poster retreat =="
	python3 ./test_frame_copy_clear.py
	echo "== Library layouts: GX stream, poses, motion, Horizontal unchanged =="
	python3 ./test_gameflow_gx_stream.py
	echo "== launch screen: GX stream in both screen shapes =="
	python3 ./test_launch_gx_stream.py
	echo "== Library layouts: navigation and state mutants =="
	python3 ./test_gameflow_layout_mutants.py
	echo "== cube orientation and visible face binding =="
	python3 ./test_cube_render_pose.py
	echo "== frame budget: what one frame of each scene costs the console =="
	python3 ./test_frame_budget.py
	echo "== settings files: real parser/writer vs docs/SETTINGS.md =="
	python3 ./test_settings_file.py
	echo "== settings saves: power lost at any step leaves them whole =="
	python3 ./test_folder_config.py
	python3 ./test_folder_config.py --sanitize
	python3 ./test_memory_folder_controls.py
	python3 ./test_memory_folder_controls.py --sanitize
	python3 ./test_config_save.py
	echo "== settings views: every setting in exactly one view =="
	python3 ./test_settings_views.py
	echo "== Menu Color: Indigo's colors turn, meanings and neutrals stay =="
	python3 ./test_ui_color.py
	echo "== Clock: the time and its dial take the corner Settings names =="
	python3 ./test_clock_corner.py
	echo "== menu music: a stream the console can seek and loop =="
	python3 ./test_menu_music.py
	echo "== settings semantics audit =="
	./audit_settings_semantics.sh

	echo "== Game Detail safety audit =="
	python3 ./audit_game_detail_safety.py

	echo "== retained Detail presentation audit =="
	python3 ./audit_detail_presentation.py

	echo "== shared retained presentation audit =="
	python3 ./audit_shared_presentation.py

	echo "== retained System Information presentation audit =="
	python3 ./audit_system_info_presentation.py

	echo "== cheat runtime integration and mutation audit =="
	python3 ./audit_cheat_safety.py

	echo "== Memory Cards: the test saves' icons and banners through the decoder =="
	python3 ./test_save_art.py

	echo "== Memory Cards: the cube screen's GX stream, its passes and icons =="
	python3 ./test_save_cubes_gx_stream.py

	echo "== Memory Cards: save details hierarchy, text bounds and GX budget =="
	python3 ./test_save_details_gx_stream.py

	echo "== Memory Cards move, copy and erase safety audit =="
	python3 ./audit_saves_safety.py

	echo "== strict Game Library dispatch audit =="
	python3 ./audit_gameflow_dispatch.py

	echo "== File Browser list contract audit =="
	python3 ./audit_files_contract.py

	echo "== hardware Alpha V1 Home polish audit =="
	python3 ./audit_home_polish.py

	echo "== Settings hardware presentation/input audit =="
	python3 ./audit_settings_hardware_followup.py
	python3 -O ./audit_settings_hardware_followup.py

	echo "== Home hardware follow-up audit =="
	python3 ./audit_home_hardware_followup.py

	echo "== semantic four-face Home integration audit =="
	python3 ./audit_four_face_home.py

	echo "== browser to Home event ownership =="
	python3 ./test_browser_home_lifecycle.py

	echo "== routing lifecycle mutation audit =="
	python3 ./audit_routing_safety.py
}

case "$SUITE" in
	all)
		echo "== poster_pack generator tests =="
		python3 -m unittest -v test_poster_pack
		build_binaries all
		run_binaries ""
		run_binaries _san
		run_contracts
		;;
	plain)
		echo "== poster_pack generator tests =="
		python3 -m unittest -v test_poster_pack
		build_binaries plain
		run_binaries ""
		;;
	sanitized)
		build_binaries sanitized
		run_binaries _san
		;;
	contracts)
		run_contracts
		;;
esac

echo "$SUITE host suite passed"
