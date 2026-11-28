"""The update files a release publishes, against a fixture zip.

The console's reader (gui/ui_update.h) and this writer agree on the
manifest by these checks: the fields, the hashes of the exact bytes
copied out of the zip, the channel each tag shape feeds, and the
refusals — above all that no branch build, only a published tag, can
produce a manifest at all.
"""

import hashlib
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import update_manifest as um

DOL = b"a dol, for the purposes of this test" * 100
APPLOADER = b"*indigo-v9.9.9".ljust(4096, b"\0")
COMMIT = "0123456789abcdef0123456789abcdef01234567"


def fixture_zip(folder: Path, members=("ipl.dol", "swiss/patches/apploader.img"),
                dol: bytes = DOL) -> Path:
    path = folder / "Indigo-v9.9.9.zip"
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("Indigo-README.txt", "readme")
        if "ipl.dol" in members:
            archive.writestr("ipl.dol", dol)
        if "swiss/patches/apploader.img" in members:
            archive.writestr("swiss/patches/apploader.img", APPLOADER)
    return path


def manifest_fields(text: bytes) -> dict[str, str]:
    fields = {}
    for line in text.decode().splitlines():
        key, _, value = line.partition("=")
        fields[key] = value
    return fields


class UpdateManifestTests(unittest.TestCase):
    def build(self, tag="v9.9.9", members=("ipl.dol", "swiss/patches/apploader.img"),
              dol=DOL, **kwargs):
        folder = Path(self.enterContext(tempfile.TemporaryDirectory()))
        out = folder / "update"
        written = um.build(tag, COMMIT, 1796000000, "2026-12-01",
                           fixture_zip(folder, members, dol), out, **kwargs)
        return out, written

    def test_a_stable_tag_writes_the_three_files(self):
        out, written = self.build()
        self.assertEqual([path.name for path in written],
                         ["indigo-v9.9.9.dol", "apploader-v9.9.9.img",
                          "stable.txt"])
        self.assertEqual((out / "indigo-v9.9.9.dol").read_bytes(), DOL)
        self.assertEqual((out / "apploader-v9.9.9.img").read_bytes(), APPLOADER)

        raw = (out / "stable.txt").read_bytes()
        self.assertLessEqual(len(raw), 4096)
        self.assertTrue(raw.endswith(b"\r\n"))
        fields = manifest_fields(raw)
        self.assertEqual(fields, {
            "FormatVersion": "1",
            "Channel": "stable",
            "Version": "v9.9.9",
            "Commit": COMMIT,
            "Date": "2026-12-01",
            "BuildTime": "1796000000",
            "File": "indigo-v9.9.9.dol",
            "Size": str(len(DOL)),
            "SHA256": hashlib.sha256(DOL).hexdigest(),
            "ApploaderFile": "apploader-v9.9.9.img",
            "ApploaderSize": str(len(APPLOADER)),
            "ApploaderSHA256": hashlib.sha256(APPLOADER).hexdigest(),
            "NotesURL": "https://github.com/spencernorton/indigo"
                        "/releases/tag/v9.9.9",
        })

    def test_candidate_tags_feed_the_rc_channel(self):
        for tag in ("v9.9.9-rc.1", "v9.9.9-beta.2"):
            with self.subTest(tag=tag):
                out, _ = self.build(tag=tag)
                fields = manifest_fields((out / "rc.txt").read_bytes())
                self.assertEqual(fields["Channel"], "rc")
                self.assertEqual(fields["Version"], tag)
                self.assertFalse((out / "stable.txt").exists())

    def test_a_stepping_stone_is_written_only_as_a_pair(self):
        out, _ = self.build(min_version="v9.8.0", min_build_time=1790000000)
        fields = manifest_fields((out / "stable.txt").read_bytes())
        self.assertEqual(fields["MinVersion"], "v9.8.0")
        self.assertEqual(fields["MinBuildTime"], "1790000000")
        with self.assertRaises(um.Refused):
            self.build(min_version="v9.8.0")
        with self.assertRaises(um.Refused):
            self.build(min_build_time=1790000000)

    def test_only_published_tags_make_a_manifest(self):
        for tag in ("beta", "main", "v9.9", "9.9.9", "v9.9.9-hotfix",
                    "v9.9.9-rc", "release-v9.9.9"):
            with self.subTest(tag=tag):
                with self.assertRaises(um.Refused):
                    self.build(tag=tag)

    def test_a_zip_without_the_pair_is_refused(self):
        with self.assertRaises(um.Refused):
            self.build(members=("swiss/patches/apploader.img",))
        with self.assertRaises(um.Refused):
            self.build(members=("ipl.dol",))
        with self.assertRaises(um.Refused):
            self.build(dol=b"")
        with self.assertRaises(um.Refused):
            self.build(dol=b"x" * (16 * 1024 * 1024 + 1))

    def test_main_reports_a_refusal_without_writing(self):
        folder = Path(self.enterContext(tempfile.TemporaryDirectory()))
        zip_path = fixture_zip(folder)
        out = folder / "update"
        self.assertEqual(um.main(["not-a-tag", COMMIT, "1796000000",
                                  "2026-12-01", str(zip_path),
                                  "--out", str(out)]), 1)
        self.assertFalse(out.exists())
        self.assertEqual(um.main(["v9.9.9", "short", "1796000000",
                                  "2026-12-01", str(zip_path),
                                  "--out", str(out)]), 1)
        self.assertEqual(um.main(["v9.9.9", COMMIT, "1796000000",
                                  "2026-12-01", str(zip_path),
                                  "--out", str(out)]), 0)
        self.assertTrue((out / "stable.txt").exists())


if __name__ == "__main__":
    unittest.main()
