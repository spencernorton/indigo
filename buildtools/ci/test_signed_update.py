"""Exercise the release authority, immutable assets and monotonic channel."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from publish_update import stage
from sign_update import sign
from test_update_manifest import COMMIT, fixture_zip
from update_manifest import build


class SignedUpdateTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.source = self.root / "release"
        self.feed = self.root / "feed"
        self.key = subprocess.check_output(["openssl", "genpkey", "-algorithm", "Ed25519"])
        self.der = subprocess.check_output(["openssl", "pkey", "-pubout", "-outform", "DER"],
                                           input=self.key)

    def release(self, tag, timestamp):
        _, _, manifest = build(tag, COMMIT, timestamp, "2026-11-23", fixture_zip(self.root),
                               self.source)
        return sign(manifest, self.key, self.der[-32:])

    def test_signature_covers_every_manifest_byte(self):
        envelope = self.release("v9.9.9", 1796000000).read_bytes()
        signature = self.root / "signature"
        public = self.root / "public"
        message = self.root / "message"
        signature.write_bytes(envelope[:64])
        public.write_bytes(self.der)
        message.write_bytes(envelope[64:])
        args = ["openssl", "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey",
                str(public), "-rawin", "-in", str(message), "-sigfile", str(signature)]
        self.assertEqual(subprocess.run(args, capture_output=True).returncode, 0)
        message.write_bytes(envelope[64:] + b" ")
        self.assertNotEqual(subprocess.run(args, capture_output=True).returncode, 0)
        with self.assertRaises(ValueError):
            sign(self.source / "stable.txt", self.key, bytes(32))

    def test_publish_retry_and_release_order(self):
        self.release("v9.9.9", 1796000000)
        stage("v9.9.9", self.source, self.feed)
        original = (self.feed / "stable.signed").read_bytes()
        stage("v9.9.9", self.source, self.feed)
        for tag, stamp in [("v9.9.8", 1796001000), ("v9.9.10", 1795999000)]:
            self.release(tag, stamp)
            with self.assertRaises(ValueError):
                stage(tag, self.source, self.feed)
        self.assertEqual(original, (self.feed / "stable.signed").read_bytes())
        self.release("v9.10.0-rc.1", 1796001000)
        stage("v9.10.0-rc.1", self.source, self.feed)
        self.assertEqual(original, (self.feed / "stable.signed").read_bytes())
        (self.source / "indigo-v9.10.0-rc.1.dol").write_bytes(b"incomplete")
        with self.assertRaises(ValueError):
            stage("v9.10.0-rc.1", self.source, self.feed)
