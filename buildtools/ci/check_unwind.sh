#!/bin/sh
# The pinned bare-metal runtime registers .eh_frame with the CRT. Preserve
# that machinery while omitting its unused linker-generated lookup index.
set -eu
elf=${1:?usage: check_unwind.sh FILE.elf}
prefix=${DEVKITPPC:?DEVKITPPC must name the pinned compiler}/bin/powerpc-eabi-
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${prefix}readelf" --wide --section-headers "$elf" > "$tmp/sections"
"${prefix}nm" --defined-only "$elf" > "$tmp/symbols"
if grep -Eq '[[:space:]]\.eh_frame_hdr[[:space:]]' "$tmp/sections"; then
	echo "unwind: unexpected linker-generated index in $elf" >&2
	exit 1
fi
for section in .eh_frame .gcc_except_table; do
	if ! awk -v wanted="$section" '{for(i=1;i<=NF;i++) if($i==wanted) found=1} END{exit !found}' "$tmp/sections"; then
		echo "unwind: missing $section in $elf" >&2
		exit 1
	fi
done
for symbol in __EH_FRAME_BEGIN__ __register_frame_info __deregister_frame_info _Unwind_Find_FDE __gxx_personality_v0; do
	if ! awk -v wanted="$symbol" '$3==wanted {found=1} END{exit !found}' "$tmp/symbols"; then
		echo "unwind: missing $symbol in $elf" >&2
		exit 1
	fi
done
echo "unwind: registered frame tables and exception runtime retained"
