"""The game content list, from packs to the signed feed.

content_list.py writes what gui/ui_content.c reads; update_manifest.py
vouches for it with the release; publish_update.py keeps the newest
build's list on the feed. The console's own parser has its tests in
buildtools/ui/tests/test_ui_content.c.
"""

import hashlib
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import content_list as cl
import update_manifest as um
from publish_update import stage
from sign_update import sign
from test_update_manifest import COMMIT, fixture_zip


def pack(folder: Path, name: str, members: dict[str, bytes]) -> Path:
    path = folder / name
    with zipfile.ZipFile(path, "w") as archive:
        for member, data in members.items():
            archive.writestr(member, data)
    return path


class ContentListTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.posters = pack(self.root, "posters.zip", {
            "swiss/ui/posters.pak": b"P" * 300, "swiss/ui/descriptions.txt": b"GALE01 Melee\n"})
        self.cheats = pack(self.root, "cheats.zip", {
            "swiss/cheats/GALE01_v102.txt": b"$Code\n", "swiss/cheats/GALE01.txt": b"$Code\n"})
        self.manuals = pack(self.root, "manuals.zip", {
            "README.txt": b"not for the card", "SHA256SUMS.txt": b"",
            "swiss/ui/manuals/GALE.pak": b"M" * 40})

    def test_list_names_card_files_in_byte_order(self):
        text = cl.build([self.posters, self.cheats, self.manuals],
                        "203.0.113.7/console/1", "content.example.net")
        lines = text.decode().splitlines()
        self.assertEqual(lines[:3], ["# Indigo game content",
                                     "Server=203.0.113.7/console/1",
                                     "Host=content.example.net"])
        paths = [line.split(" ")[2] for line in lines[3:]]
        self.assertEqual(paths, ["swiss/cheats/GALE01.txt", "swiss/cheats/GALE01_v102.txt",
                                 "swiss/ui/descriptions.txt", "swiss/ui/manuals/GALE.pak",
                                 "swiss/ui/posters.pak"])
        self.assertIn(f"{hashlib.sha256(b'P' * 300).hexdigest()} 300 swiss/ui/posters.pak",
                      lines)
        cl.check(text)

    def test_folder_laid_out_like_a_card(self):
        card = self.root / "card"
        (card / "swiss/cheats").mkdir(parents=True)
        (card / "swiss/cheats/GZLE01.txt").write_bytes(b"$Code\n")
        (card / "swiss/settings.ini").write_bytes(b"not content")
        self.assertEqual(cl.build([card]).decode().splitlines()[1].split(" ")[2],
                         "swiss/cheats/GZLE01.txt")

    def test_refusals(self):
        with self.assertRaises(cl.Refused):
            cl.build([self.posters, self.posters])
        with self.assertRaises(cl.Refused):
            cl.build([self.manuals], "203.0.113.7/console")
        for server in ("indigo.example.net/console", "256.0.0.1/", "203.0.113.7:0/",
                       "203.0.113.7/../x", "https://203.0.113.7/"):
            with self.subTest(server=server), self.assertRaises(cl.Refused):
                cl.build([self.manuals], server, "content.example.net")
        with self.assertRaises(cl.Refused):
            cl.build([pack(self.root, "empty.zip", {"README.txt": b""})])
        line = "a" * 64 + " 5 swiss/ui/discs.pak\n"
        for text in (line * 2, "0" * 64 + " 05 swiss/ui/discs.pak\n",
                     "a" * 64 + " 5 swiss/ui/global.ini\n", "Server=1.2.3.4/\n" + line):
            with self.subTest(text=text), self.assertRaises(cl.Refused):
                cl.check(text.encode())

    def test_release_vouches_for_the_list(self):
        listing = self.root / "content.list"
        listing.write_bytes(cl.build([self.cheats]))
        written = um.build("v9.9.9", COMMIT, 1796000000, "2026-11-26",
                           fixture_zip(self.root), self.root / "out", content=listing)
        names = [path.name for path in written]
        self.assertEqual(names[-2:], ["content-v9.9.9.list", "content.txt"])
        fields = dict(line.split("=", 1) for line in
                      (self.root / "out/content.txt").read_text().splitlines())
        self.assertEqual(fields["Channel"], "content")
        self.assertEqual(fields["File"], "content-v9.9.9.list")
        self.assertEqual(int(fields["Size"]), listing.stat().st_size)
        self.assertEqual(fields["SHA256"], hashlib.sha256(listing.read_bytes()).hexdigest())
        listing.write_bytes(b"not a list\n")
        with self.assertRaises(um.Refused):
            um.build("v9.9.9", COMMIT, 1796000000, "2026-11-26",
                     fixture_zip(self.root), self.root / "out", content=listing)

    def test_feed_keeps_the_newest_builds_list(self):
        key = subprocess.check_output(["openssl", "genpkey", "-algorithm", "Ed25519"])
        public = subprocess.check_output(["openssl", "pkey", "-pubout", "-outform", "DER"],
                                         input=key)[-32:]
        feed = self.root / "feed"
        listing = self.root / "content.list"

        def release(tag, stamp, packs):
            source = self.root / tag
            listing.write_bytes(cl.build(packs))
            um.build(tag, COMMIT, stamp, "2026-11-26", fixture_zip(self.root), source,
                     content=listing)
            for manifest in source.glob("*.txt"):
                sign(manifest, key, public)
            stage(tag, source, feed)
            return (feed / "content.txt").read_bytes()

        first = release("v9.9.9", 1796000000, [self.cheats])
        self.assertIn(b"Version=v9.9.9", first)
        # An older build on the other channel leaves the newer list.
        self.assertEqual(release("v9.10.0-rc.1", 1795000000, [self.manuals]), first)
        newer = release("v9.10.0-rc.2", 1797000000, [self.manuals])
        self.assertIn(b"Version=v9.10.0-rc.2", newer)
        self.assertTrue((feed / "content-v9.10.0-rc.2.list").exists())
        self.assertEqual((feed / "content.signed").read_bytes()[64:], newer)


if __name__ == "__main__":
    unittest.main()
