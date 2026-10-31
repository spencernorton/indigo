#!/usr/bin/env python3
"""The built-in pack keeps every input byte and bounds the expanded storage."""

import struct
import unittest
import zlib

import pack_builtin_images as packer


class BuiltinImages(unittest.TestCase):
    def test_lossless_deterministic_pack_and_exact_size(self):
        raw = struct.pack(">III", 0x0020AF30, 21, 12) + bytes(range(256)) * 400
        packed, header = packer.pack(raw)
        self.assertEqual(zlib.decompress(packed), raw)
        self.assertEqual(packer.pack(raw), (packed, header))
        self.assertIn(f"#define UI_BUILTIN_IMAGES_BYTES {len(raw)}u\n", header)
        self.assertLess(len(packed), len(raw))

    def test_rejects_wrong_asset_and_expanded_memory_growth(self):
        for raw in (b"", b"not a TPL" * 30,
                    struct.pack(">III", 0x0020AF30, 0, 12) + bytes(100),
                    struct.pack(">III", 0x0020AF30, 65, 12) + bytes(100),
                    struct.pack(">III", 0x0020AF30, 21, 12) + bytes(packer.MAX_BYTES)):
            with self.subTest(size=len(raw)), self.assertRaises(ValueError):
                packer.pack(raw)


if __name__ == "__main__":
    unittest.main()
