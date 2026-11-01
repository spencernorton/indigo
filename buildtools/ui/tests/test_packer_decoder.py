#!/usr/bin/env python3
"""Exercise the return loader's real decoder and static initialization."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PACKER = ROOT / "cube/packer/source"
SANITIZE = "--sanitize" in sys.argv
if SANITIZE:
    sys.argv.remove("--sanitize")

# Synthetic branch/NOP words, encoded with PowerPC BCJ + LZMA2 and CRC32.
EXPECTED = bytes.fromhex("48000005600000004bfffffd60000000") * 16
PACKED = bytes.fromhex(
    "fd377a585a0000016922de360201050021011c0022bd862ee000ff002a5d0024002c0a"
    "6308aa7821360c298f780f2dbfd69dced2d5375393cc6b0e97e8181b3006d3de7a6538"
    "919a000000007df8af580001428002000000a3072a9a3e300d8b020000000001595a")

DRIVER = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Decode only: cache instructions and entrypoint handoff need target tests. */
#undef memcpy
#undef memmove
void *packerMemmove(void *, const void *, size_t);
#define memmove packerMemmove
#define __attribute(...)
#define asm(...)
#define main packedProgramMain
#include @MAIN@
#undef main
#undef asm
#undef __attribute
#undef memmove
struct __argv __argv, __envp;

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
    if (mode > 3) return 2;
    static const uint8_t expected[] = { @EXPECTED@ };
    uint8_t input[sizeof(executable_xz)], storage[sizeof(expected) + 64];
    memmove(input, executable_xz, sizeof(input));
    memset(storage, 0xc7, sizeof(storage));
    if (mode == 2) input[sizeof(input) - 12] ^= 1;
    xz_crc32_init();
    xz_dec.bcj = &xz_dec_bcj;
    xz_dec.lzma2 = &xz_dec_lzma2;
    xz_buf.in = input;
    xz_buf.in_size = sizeof(input) - (mode == 1);
    xz_buf.out = storage + 32;
    xz_buf.out_size = sizeof(expected) - (mode == 3);
    enum xz_ret status = xz_dec_run(&xz_dec, &xz_buf);
    bool ok = mode ? status != XZ_STREAM_END :
        status == XZ_STREAM_END && xz_buf.in_pos == sizeof(input) &&
        xz_buf.out_pos == sizeof(expected) &&
        memcmp(xz_buf.out, expected, sizeof(expected)) == 0;
    for (size_t i = 0; i < 32; ++i) ok &= storage[i] == 0xc7;
    for (size_t i = 32 + xz_buf.out_size; i < sizeof(storage); ++i)
        ok &= storage[i] == 0xc7;
    if (!ok) {
        fprintf(stderr, "decoder mode %u failed, status %u\n", mode, status);
        return 1;
    }
    printf("decoder mode %u passed\n", mode);
    return 0;
}
'''


def compile_probe(folder, main_source=None):
    include = folder / "ogc/machine"
    include.mkdir(parents=True)
    (include / "processor.h").write_text(
        "#pragma once\n#define PPC_CACHE_ALIGNMENT 32\n"
        "struct __argv { unsigned words[6]; };\n"
        "static inline void _sync(void) {}\n"
        "static inline unsigned mfhid0(void) { return 0; }\n"
        "static inline void mthid0(unsigned value) { (void)value; }\n")
    (folder / "executable_xz.h").write_text(
        "#include <stdint.h>\n#include <stddef.h>\n"
        "static const uint8_t executable_xz[] = {" +
        ",".join(str(x) for x in PACKED) + "};\n"
        "static const size_t executable_xz_size = sizeof(executable_xz);\n")
    (folder / "executable_bin.h").write_text(
        f"static const size_t executable_bin_size = {len(EXPECTED)};\n")
    actual = PACKER / "main.c"
    if main_source is not None:
        actual = folder / "packer_main.c"
        actual.write_text(main_source)
    source = DRIVER.replace("@MAIN@", json.dumps(str(actual))).replace(
        "@EXPECTED@", ",".join(str(x) for x in EXPECTED))
    driver = folder / "decoder.c"
    driver.write_text(source)
    output = folder / "decoder"
    flags = ["-std=gnu11", "-O2", "-g", "-ffreestanding", "-fno-builtin"]
    if SANITIZE:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                  "-fno-omit-frame-pointer"]
        if sys.platform.startswith("linux"):
            flags += ["-fno-pie", "-no-pie"]
    command = [*shlex.split(os.environ.get("CC", "cc")), *flags,
               "-I", str(folder), "-I", str(PACKER), str(driver), "-o", str(output)]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError("decoder probe did not compile:\n" + built.stderr)
    return output


class ReturnDecoder(unittest.TestCase):
    def test_exact_output_and_bounded_failures(self):
        with tempfile.TemporaryDirectory() as name:
            probe = compile_probe(Path(name))
            for mode in range(4):
                with self.subTest(mode=mode):
                    result = subprocess.run([str(probe), str(mode)], check=True,
                                            capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.stdout.strip(), f"decoder mode {mode} passed")

    def test_missing_mode_mutant_is_rejected(self):
        source = (PACKER / "main.c").read_text()
        needle = ".single_call = true"
        self.assertEqual(source.count(needle), 1)
        with tempfile.TemporaryDirectory() as name:
            probe = compile_probe(Path(name), source.replace(needle, ".single_call = false", 1))
            # The old initialization still decodes the valid fixture. Its short
            # output path must fail or hit the deadline, not silently pass.
            subprocess.run([str(probe), "0"], check=True, capture_output=True, timeout=5)
            try:
                result = subprocess.run([str(probe), "3"], capture_output=True, timeout=2)
            except subprocess.TimeoutExpired:
                return
            self.assertNotEqual(result.returncode, 0, "missing mode was not detected")


if __name__ == "__main__":
    unittest.main()
