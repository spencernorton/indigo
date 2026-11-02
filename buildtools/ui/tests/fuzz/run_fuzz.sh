#!/bin/sh
# Coverage-guided fuzzing of the files Indigo reads from a card: poster and
# stills packs, game descriptions, its play history, save files, settings files,
# the file table of every disc image the Library lists, the pictures of the
# programs in /apps, games' cheats files and manuals, and the Time Zone and NTP
# server reply the clock sync reads. Each target is built with libFuzzer, AddressSanitizer and
# UBSan and runs for SECONDS, starting from seeds.py's real files. A crash, a sanitizer finding or a broken invariant
# fails the run and leaves the input that caused it in OUT/crashes/<target>/.
#
# usage: buildtools/ui/tests/fuzz/run_fuzz.sh [SECONDS per target, 30] [OUT, ./fuzz-out]
#        buildtools/ui/tests/fuzz/run_fuzz.sh total:SECONDS [OUT]   (shared by the targets)
#        buildtools/ui/tests/fuzz/run_fuzz.sh --build-only [OUT]
# Needs clang with its fuzzer and sanitizer runtimes, zlib, Python 3 with
# Pillow and NumPy; the poster seed also needs gxtexconv.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build_only=
if [ "${1:-}" = --build-only ]; then
	build_only=1
	shift
	set -- 0 "${1:-fuzz-out}"
fi
targets="history saves raw_saves posters manual about settings fst png cheats wiiload time_sync lab"
seconds=${1:-30}
case $seconds in
total:*) seconds=$(( ${seconds#total:} / $(echo "$targets" | wc -w) )) ;;
esac
mkdir -p "${2:-fuzz-out}"
out=$(cd "${2:-fuzz-out}" && pwd)
cc=${FUZZ_CC:-clang}
gui=$here/../../../../cube/swiss/source/gui
flags="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
# Sanitizer executables and high-ASLR hosts: see ../Makefile.
[ "$(uname -s)" = Linux ] && flags="$flags -fno-pie -no-pie"

mkdir -p "$out/bin"
echo "== building the fuzzers =="
# shellcheck disable=SC2086 # $flags is a list of flags
{
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/history" "$here/fuzz_history.c" "$gui/ui_game_history.c"
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/saves" "$here/fuzz_saves.c" "$gui/ui_saves.c"
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/raw_saves" "$here/fuzz_raw_saves.c" "$gui/ui_saves_raw.c" "$gui/ui_saves.c"
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/about" "$here/fuzz_about.c" "$gui/ui_about.c"
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/lab" "$here/fuzz_lab.c" "$gui/ui_lab.c"
	$cc $flags -std=c11 -DUI_ASSETS_HOST_BUILD -I"$gui" -o "$out/bin/posters" \
		"$here/fuzz_posters.c" "$gui/ui_assets.c" -lz
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/png" "$here/fuzz_png.c" "$gui/ui_png.c" -lz -lm
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/time_sync" "$here/fuzz_time_sync.c" "$gui/ui_time_sync.c" "$here/../tzdata_host.c" -I"$gui/../../include"
	$cc $flags -std=c11 -I"$gui" -o "$out/bin/manual" "$here/fuzz_manual.c" "$gui/ui_manual.c" -lz -lm
	python3 "$here/settings_source.py" "$out/fuzz_settings.c"
	$cc $flags -std=gnu11 -w -o "$out/bin/settings" "$out/fuzz_settings.c"
	python3 "$here/fst_source.py" "$out/fuzz_fst.c"
	$cc $flags -funsigned-char -std=gnu11 -w -o "$out/bin/fst" "$out/fuzz_fst.c"
	python3 "$here/cheats_source.py" "$out/fuzz_cheats.c"
	$cc $flags -funsigned-char -std=gnu11 -w -o "$out/bin/cheats" "$out/fuzz_cheats.c"
	python3 "$here/wiiload_source.py" "$out/fuzz_wiiload.c"
	$cc $flags -std=gnu11 -w -o "$out/bin/wiiload" "$out/fuzz_wiiload.c" -lz
}
[ -n "$build_only" ] && exit 0
python3 "$here/seeds.py" "$out/corpus"
# Inputs that once broke something stay in the corpus for good: corpus/<target>/.
for target in $targets; do
	if [ -d "$here/corpus/$target" ]; then
		cp "$here/corpus/$target"/* "$out/corpus/$target/"
	fi
done

status=0
for target in $targets; do
	echo "== $target: ${seconds}s =="
	mkdir -p "$out/crashes/$target"
	if "$out/bin/$target" -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 \
		-artifact_prefix="$out/crashes/$target/" -print_final_stats=1 \
		"$out/corpus/$target" >"$out/$target.log" 2>&1; then
		grep -E "^stat::number_of_executed_units|^stat::new_units_added" "$out/$target.log" | tr '\n' ' '
		echo
	else
		status=1
		echo "::error::the $target fuzzer found a problem; the input is in crashes/$target/"
		tail -n 40 "$out/$target.log"
	fi
done
exit $status
