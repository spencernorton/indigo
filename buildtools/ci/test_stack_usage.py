"""Unit and mutant tests for PowerPC stack-usage verification."""

import tempfile
import unittest
from pathlib import Path

import check_stack_usage as csu


class StackUsageTests(unittest.TestCase):
    def test_empty_records_fail_closed(self):
        self.assertTrue(csu.check_records([]))
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(csu.main([tmp]), 1)

    def test_valid_lto_and_object_records_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            (root / "swiss.elf.ltrans0.ltrans.su").write_text(
                "source/swiss.c:1320:5:gameflow_info_game\t12280\tstatic\n"
                "source/patcher.c:1046:5:Patch_Hypervisor\t23976\tstatic\n",
                encoding="utf-8",
            )
            (root / "build/ui_jewel_case.su").write_text(
                "source/gui/ui_jewel_case.c:1181:13:buildSheets\t8360\tstatic\n",
                encoding="utf-8",
            )
            self.assertEqual(csu.main([str(root)]), 0)

    def test_indigo_ceiling_catches_oversized_gui_frame_below_total_ceiling(self):
        records = csu.parse_su_text(
            "source/gui/FrameBufferMagic.c:4200:13:_DrawGameflow\t18000\tstatic\n"
        )
        errors = csu.check_records(records)
        self.assertEqual(len(errors), 1)
        self.assertIn("Indigo stack ceiling", errors[0])

    def test_total_ceiling_catches_oversized_upstream_frame(self):
        records = csu.parse_su_text(
            "source/patcher.c:1046:5:Patch_Hypervisor\t30000\tstatic\n"
        )
        errors = csu.check_records(records)
        self.assertEqual(len(errors), 1)
        self.assertIn("total stack ceiling", errors[0])

    def test_dynamic_or_bounded_vla_frame_is_rejected(self):
        for qualifier in ("dynamic", "dynamic,bounded"):
            with self.subTest(qualifier=qualifier):
                records = csu.parse_su_text(
                    f"source/gui/ui_files.c:88:6:helper\t64\t{qualifier}\n"
                )
                errors = csu.check_records(records)
                self.assertEqual(len(errors), 1)
                self.assertIn("non-static stack frame", errors[0])

    def test_malformed_su_line_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            (root / "build/broken.su").write_text("not-a-valid-su-line\n", encoding="utf-8")
            self.assertEqual(csu.main([str(root)]), 1)


if __name__ == "__main__":
    unittest.main()
