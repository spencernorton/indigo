# Canonical rod-lighting texture

The Jewel Case keeps its original texture-generation algorithm behind
`UI_JEWEL_CASE_REFERENCE`. Normal builds decode the canonical 64 by 64 RGBA8
texture and its three lower mip levels lazily into the same 32-byte aligned
21,760-byte allocation. Its zlib payload is 12,726 bytes; the boot image stays
uncompressed. The texture descriptors, cache flush and rendering passes retain
the existing behavior.

`jewel_matcap.json` records the accepted PowerPC resource. Host floating-point
math is not its oracle. Keep the room and backdrop function boundaries: removing
the ray-tracer can otherwise inline those functions into the table builder and
change float rounding. The target comparison checks the complete resource set,
not just this texture.

To regenerate, compile the export utility in the pinned SDK image named in the
metadata. From the repository root, inside that image:

```sh
mkdir -p /tmp/jewel-matcap
/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc \
  -O2 -g -fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -ffunction-sections -fdata-sections -DGEKKO -mogc -mcpu=750 -meabi -mhard-float \
  -Icube/swiss/source/gui -I/opt/devkitpro/libogc2/gamecube/include \
  buildtools/ui/jewel_matcap_export.c -Wl,--gc-sections -Wl,--no-eh-frame-hdr \
  -L/opt/devkitpro/libogc2/gamecube/lib -logc -lm -o /tmp/jewel-matcap/export.elf
elf2dol /tmp/jewel-matcap/export.elf /tmp/jewel-matcap/export.dol
```

Run that diagnostic in Dolphin with OSREPORT logging enabled and preserve its
UART log. Run it twice in fresh profiles, then verify each capture independently:

```sh
python3 buildtools/ui/generate_jewel_matcap.py --capture first.log --check
python3 buildtools/ui/generate_jewel_matcap.py --capture second.log --check
```

The utility rejects missing, duplicated, oversized or noncanonical captures.
Omit `--check` to write the generated include deliberately. A change to the
canonical SHA-256 requires an intentional visual/resource review. Ordinary
builds use the checked-in payload; they need neither Dolphin nor network access.
`generate_jewel_matcap.py --check` validates the payload and generated text.

The focused host test checks all truncated payload lengths, corruption, guards,
strict consumption, alignment, warm preparation and every application allocation
failure followed by retry. Target images and complete platform checks remain
separate from host decoding and emulated preparation timings.
