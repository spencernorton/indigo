"""Exercise release readback against immutable build receipts."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


VERIFY = Path(__file__).with_name('verify_pages_feed.sh').resolve()
TAG = 'v3.1.0-rc.1'
GOOD_COMMIT = 'a' * 40
BAD_COMMIT = 'b' * 40


class PagesFeedTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.signed = self.root / 'signed'
        self.signed.mkdir()
        for commit in (GOOD_COMMIT, BAD_COMMIT):
            updates = self.root / commit / 'updates'
            updates.mkdir(parents=True)
            for name in ('rc.signed', 'rc.txt', f'indigo-{TAG}.dol', f'apploader-{TAG}.img'):
                data = b'\x00\xff' + name.encode()
                (self.signed / name).write_bytes(data)
                (updates / name).write_bytes(data)
        (self.root / BAD_COMMIT / 'updates' / f'apploader-{TAG}.img').write_bytes(b'changed')
        self.command('gh', 'echo "$PAGES_RECEIPT"')
        self.command('sleep', 'exit 0')
        self.command('curl', '''while [ "$1" != --output ]; do shift; done
        url=$3
        prefix=https://raw.githubusercontent.com/test/indigo/
        case "$url" in "$prefix"*) cp "$FIXTURE/${url#"$prefix"}" "$2" ;; *) exit 1 ;; esac''')

    def command(self, name, body):
        tool = self.root / name
        tool.write_text('#!/bin/sh\nset -eu\n' + body + '\n')
        tool.chmod(0o755)

    def verify(self, receipt):
        env = dict(os.environ, GITHUB_REPOSITORY='test/indigo', PAGES_RECEIPT=receipt,
                   FIXTURE=str(self.root), PATH=str(self.root) + os.pathsep + os.environ['PATH'])
        return subprocess.run([VERIFY, TAG, self.signed], capture_output=True, text=True, env=env)

    def test_reads_exact_deployed_revision(self):
        result = self.verify(GOOD_COMMIT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(GOOD_COMMIT, result.stdout)

    def test_changed_apploader_and_build_in_progress_cannot_pass(self):
        for receipt in (BAD_COMMIT, 'building'):
            with self.subTest(receipt=receipt):
                self.assertNotEqual(self.verify(receipt).returncode, 0)
