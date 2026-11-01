#!/usr/bin/env python3
"""The DOL, package and workflow checks catch what they are for."""

import contextlib
import io
import re
import struct
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import check_aesnd
import check_package
import check_upstream
import check_workflows
import verify_dol

SHORT = "0a2edf3"
FULL = SHORT + "7" * 33
IMAGE = "example.invalid/libogc2@sha256:" + "1" * 64


def dol(revisions: int = 1, entry: int = 0x80003100, text_offset: int = 0x100) -> bytes:
    data = bytearray(0x200)
    struct.pack_into(">I", data, 0x00, text_offset)
    struct.pack_into(">I", data, 0x48, 0x80003100)
    struct.pack_into(">I", data, 0x90, 0x100)
    struct.pack_into(">I", data, 0xE0, entry)
    for n in range(revisions):
        at = 0x120 + n * 16
        data[at:at + 9] = b"\0" + SHORT.encode() + b"\0"
    return bytes(data)


class Dol(unittest.TestCase):
    def test_a_good_dol(self):
        self.assertEqual(verify_dol.check(dol(), SHORT)["sections"], 1)

    def test_the_commit_must_appear_exactly_once(self):
        for count in (0, 2):
            with self.assertRaises(verify_dol.Invalid):
                verify_dol.check(dol(revisions=count), SHORT)

    def test_structure(self):
        for bad in (dol(entry=0x90000000), dol(text_offset=0x180), dol()[:0x80]):
            with self.assertRaises((verify_dol.Invalid, struct.error)):
                verify_dol.check(bad, SHORT)

    def test_size_budget(self):
        with self.assertRaises(verify_dol.Invalid):
            verify_dol.check(dol() + bytes(verify_dol.MAX_DOL_BYTES), SHORT)

    def test_cli_refuses_a_mismatched_or_unpinned_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "swiss.dol"
            path.write_bytes(dol())

            def cli(*args: str) -> int:
                # Quiet: an ::error:: line in a CI log becomes an annotation.
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    return verify_dol.main([str(path), *args])

            self.assertEqual(cli("--revision", FULL, "--short", SHORT, "--toolchain", IMAGE), 0)
            self.assertEqual(cli("--revision", "f" * 40, "--short", SHORT, "--toolchain", IMAGE), 1)
            self.assertEqual(cli("--revision", FULL, "--short", SHORT, "--toolchain", "libogc2:latest"), 1)


class Package(unittest.TestCase):
    def build(self, directory: Path, extra: str | None = None, drop: str | None = None,
              ipl: bytes | None = None, also: tuple[str, ...] = ()) -> Path:
        path = directory / "Indigo-v9.9.9.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name in sorted(check_package.LAYOUT | ({extra} if extra else set()) | set(also)):
                if name == drop:
                    continue
                if name.endswith("/"):
                    archive.writestr(name, b"")
                elif name == "ipl.dol":
                    archive.writestr(name, ipl if ipl is not None else dol())
                elif name == "swiss/patches/apploader.img":
                    archive.writestr(name, b"*indigo-v9.9.9".ljust(32, b"\0") + b"image")
                elif name == "Indigo-README.txt":
                    archive.writestr(name, "Indigo v9.9.9 - an unofficial fork\n")
                else:
                    archive.writestr(name, b"text")
        return path

    def test_the_layout_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(check_package.problems(self.build(Path(directory)), dol(), "v9.9.9"), [])

    def test_extra_missing_and_foreign_files_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            here = Path(directory)
            # An empty games/ is refused too: a Mac's Replace would swap the card's games folder for it.
            for case in ({"extra": ".DS_Store"}, {"extra": "games/"}, {"extra": "games/demo.iso"}, {"drop": "swiss/ui/"},
                         {"drop": "swiss/patches/apploader.img"}, {"drop": "swiss/indigo/LICENSE-lwip.txt"}, {"ipl": dol(revisions=2)}):
                self.assertNotEqual(check_package.problems(self.build(here, **case), dol(), "v9.9.9"),
                                    [], case)

    def test_the_browser_brings_mbed_tls_licence(self):
        browser = (check_package.BROWSER, "gcbrowser/", "gcbrowser/cacert.pem")
        with tempfile.TemporaryDirectory() as directory:
            here = Path(directory)
            whole = self.build(here, also=browser + (check_package.MBEDTLS_LICENSE,))
            self.assertEqual(check_package.problems(whole, dol(), "v9.9.9"), [])
            self.assertEqual(check_package.problems(self.build(here, also=browser), dol(), "v9.9.9"),
                             ["swiss/indigo/cubebrowser.dol ships without mbed TLS's licence, "
                              "swiss/indigo/LICENSE-mbedtls.txt"])

    def test_a_long_version_is_cut_to_the_header_field(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Indigo-ci-d87bcf54.zip"
            with zipfile.ZipFile(path, "w") as archive:
                for name in sorted(check_package.LAYOUT):
                    archive.writestr(name, {
                        "ipl.dol": dol(),
                        "swiss/patches/apploader.img": b"*indigo-ci-d87bc" + bytes(16) + b"image",
                        "Indigo-README.txt": b"Indigo ci-d87bcf54 - an unofficial fork\n",
                    }.get(name, b""))
            self.assertEqual(check_package.problems(path, dol(), "ci-d87bcf54"), [])

    def test_the_version_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(len(check_package.problems(self.build(Path(directory)), dol(), "v1.0.0")), 2)


GOOD_WORKFLOW = """\
on:
  pull_request:
env:
  TOOLCHAIN: example.invalid/libogc2@sha256:{digest}
jobs:
  build:
    runs-on: [self-hosted, indigo-build]
    steps:
      - uses: actions/checkout@{sha} # v7
  emulator:
    runs-on:
      - self-hosted
      - indigo-emulator
    steps:
      - run: true
"""


class Workflows(unittest.TestCase):
    def root(self, directory: Path, workflow: str) -> Path:
        (directory / "buildtools/ci/runner").mkdir(parents=True)
        (directory / ".github/workflows").mkdir(parents=True)
        image = "example.invalid/libogc2@sha256:" + "1" * 64
        (directory / "buildtools/ci/runner/build.Dockerfile").write_text(
            f'FROM {image}\nRUN echo "{image}" > /etc/indigo-ci/toolchain\n')
        (directory / ".github/workflows/ci.yml").write_text(workflow)
        return directory

    def check(self, workflow: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            return check_workflows.problems(self.root(Path(directory), workflow))

    def good(self) -> str:
        return GOOD_WORKFLOW.format(digest="1" * 64, sha="a" * 40)

    def test_our_runners_pinned_actions_and_the_runner_toolchain_pass(self):
        self.assertEqual(self.check(self.good()), [])

    def test_each_breach_is_caught(self):
        good = self.good()
        for bad in (good.replace("[self-hosted, indigo-build]", "ubuntu-24.04"),
                    good.replace("[self-hosted, indigo-build]", "[self-hosted]"),
                    good.replace("      - indigo-emulator\n", ""),
                    good.replace("@" + "a" * 40, "@v7"),
                    good.replace("  pull_request:", "  pull_request_target:"),
                    good.replace("    steps:\n      - uses", "    container: debian\n    steps:\n      - uses", 1),
                    good.replace("1" * 64, "2" * 64, 1)):
            self.assertNotEqual(self.check(bad), [], bad)

    def test_every_copy_of_the_toolchain_digest_matches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.root(Path(directory), self.good())
            (root / "README.md").write_text("docker pull x/libogc2@sha256:" + "1" * 64 + "\n")
            (root / "pack.py").write_text('IMAGE = ("x/libogc2@sha256:"\n    "' + "1" * 64 + '")\n')
            self.assertEqual(check_workflows.problems(root), [])
            for name in ("README.md", "pack.py"):
                text = (root / name).read_text()
                (root / name).write_text(text.replace("1" * 64, "3" * 64))
                self.assertEqual(len(check_workflows.problems(root)), 1, name)
                (root / name).write_text(text)


class Upstream(unittest.TestCase):
    """Outside the interface, a file matches the upstream commit UPSTREAM names,
    line endings aside, or UPSTREAM lists it; a listed file that matches again fails."""

    def commit(self, directory: Path, files: dict[str, bytes]) -> str:
        for name, data in files.items():
            (directory / name).parent.mkdir(parents=True, exist_ok=True)
            (directory / name).write_bytes(data)
        def git(*args: str) -> str:
            return subprocess.run(["git", "-C", str(directory), *args], capture_output=True,
                                  text=True, check=True).stdout.strip()
        if not (directory / ".git").exists():
            git("init", "-q")
        git("add", "-A")
        git("-c", "user.name=t", "-c", "user.email=t@example.invalid", "-c", "commit.gpgsign=false",
            "commit", "-qm", "c")
        return git("rev-parse", "HEAD")

    def check(self, patcher: bytes, listed: str = "",
              extra: dict[str, bytes] | None = None) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            up, ours = Path(tmp, "up"), Path(tmp, "ours")
            commit = self.commit(up, {"cube/swiss/source/patcher.c": b"a\r\nb\r\n",
                                      "cube/swiss/source/gui/menu.c": b"swiss\n"})
            self.commit(ours, {"UPSTREAM": f"upstream {up}\ncommit {commit}\n{listed}".encode(),
                               "cube/swiss/source/patcher.c": patcher,
                               "cube/swiss/source/gui/menu.c": b"indigo\n", **(extra or {})})
            return check_upstream.problems(ours)

    def test_line_endings_and_the_interface_are_not_changes(self):
        self.assertEqual(self.check(b"a\nb\n"), [])

    def test_project_journal_is_owned_without_exempting_upstream_code(self):
        journal = {"AGENTS/journal.md": b"Project development history\n"}
        self.assertEqual(self.check(b"a\nb\n", extra=journal), [])
        self.assertIn("does not list it", self.check(b"a\nc\n", extra=journal)[0])
        self.assertIn("does not list it", self.check(b"a\nb\n", extra={
            "AGENTS-unowned/journal.md": b"Not a project-owned path\n"})[0])

    def test_a_change_is_listed_and_the_list_stays_true(self):
        self.assertEqual(self.check(b"a\nc\n", "cube/swiss/source/patcher.c  a fix\n"), [])
        self.assertIn("does not list it", self.check(b"a\nc\n")[0])
        self.assertIn("gives no reason", self.check(b"a\nc\n", "cube/swiss/source/patcher.c\n")[0])
        self.assertIn("matches upstream", self.check(b"a\nb\n", "cube/swiss/source/patcher.c  a fix\n")[0])

    def merge(self, ours: bytes) -> tuple[subprocess.CompletedProcess, Path, str]:
        """upstream_merge.sh from an upstream commit to the next, over line endings."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        tmp = Path(directory.name)
        up, indigo = tmp / "up", tmp / "indigo"
        old = self.commit(up, {"a.c": b"x\r\ny\r\n", "b.c": b"keep\r\n"})
        new = self.commit(up, {"a.c": b"x\r\nz\r\n", "c.c": b"new\r\n"})
        self.commit(indigo, {"UPSTREAM": f"upstream {up}\ncommit {old}\n".encode(),
                             "a.c": ours, "b.c": b"mine\n"})
        done = subprocess.run(["sh", str(ROOT / "buildtools/upstream_merge.sh"), new], cwd=indigo,
                              capture_output=True, text=True)
        return done, indigo, new

    def test_merge_takes_upstream_changes_over_line_endings(self):
        done, indigo, new = self.merge(b"x\ny\n")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual([(indigo / f).read_bytes() for f in ("a.c", "b.c", "c.c")],
                         [b"x\nz\n", b"mine\n", b"new\n"])
        self.assertIn(f"commit {new}", (indigo / "UPSTREAM").read_text())

    def test_merge_lists_a_conflict(self):
        done, indigo, _ = self.merge(b"x\nq\n")
        self.assertEqual(done.returncode, 1)
        self.assertIn("  a.c\n", done.stdout)
        self.assertIn(b"<<<<<<<", (indigo / "a.c").read_bytes())


class Aesnd(unittest.TestCase):
    """The AESND copy is byte-identical to the libogc2 commit it names, and that
    commit is the toolchain's: changes go in patches, and a moved toolchain fails."""

    def check(self, copied: dict[str, bytes], toolchain: str = "libogc2 r1.abc") -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            up, ours = Path(tmp, "up"), Path(tmp, "ours")
            commit = Upstream.commit(self, up, {"libaesnd/aesndlib.c": b"void AESND_Reset(void);\r\n",
                                                "include/aesndlib.h": b"#pragma once\n"})
            copy = ours / check_aesnd.COPY
            (copy / "libogc2").mkdir(parents=True)
            (copy / "UPSTREAM").write_text(f"upstream {up}\ncommit {commit}\ntoolchain libogc2 r1.abc\n")
            for name, data in copied.items():
                (copy / "libogc2" / name).parent.mkdir(parents=True, exist_ok=True)
                (copy / "libogc2" / name).write_bytes(data)
            libversion = Path(tmp, "libversion.h")
            libversion.write_text(f'#define _V_STRING "{toolchain}"\n')
            return check_aesnd.problems(ours, libversion=libversion)

    def test_the_copy_is_the_commit_byte_for_byte(self):
        self.assertEqual(self.check({"libaesnd/aesndlib.c": b"void AESND_Reset(void);\r\n"}), [])
        self.assertIn("differs from libogc2", self.check({"libaesnd/aesndlib.c": b"void AESND_Reset(void);\n"})[0])
        self.assertIn("is not in libogc2", self.check({"libaesnd/extra.c": b""})[0])

    def test_a_toolchain_that_moves_on_fails(self):
        self.assertIn("the toolchain's libogc2 is libogc2 r2.def",
                      self.check({"include/aesndlib.h": b"#pragma once\n"}, toolchain="libogc2 r2.def")[0])


ROOT = Path(__file__).resolve().parents[2]


class Release(unittest.TestCase):
    """release.yml sends vX.Y.Z to main and betas and release candidates to beta,
    and release_notes.sh says which kind of pre-release a tag is."""

    def channel(self, tag: str):
        text = (ROOT / ".github/workflows/release.yml").read_text()
        stable, pre = re.findall(r"grep -Eqx '([^']+)'", text)[:2]
        if re.fullmatch(stable, tag):
            return "main"
        return "beta" if re.fullmatch(pre, tag) else None

    def test_tags_and_their_channels(self):
        for tag, channel in (("v2.0.0", "main"), ("v2.0.0-beta.3", "beta"), ("v2.0.0-rc.1", "beta"),
                             ("v2.0.0-rc", None), ("v2.0-rc.1", None), ("v2.0.0-RC.1", None),
                             ("2.0.0", None)):
            self.assertEqual(self.channel(tag), channel, tag)

    def notes(self, tag: str, entry: str = "- Something new.") -> str:
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "CHANGELOG.md").write_text(f"# Changelog\n\n## Unreleased\n\n{entry}\n")
            return subprocess.run(["sh", str(ROOT / "buildtools/release_notes.sh"), tag], cwd=tmp,
                                  capture_output=True, text=True, check=True).stdout

    def test_each_pre_release_says_what_it_is(self):
        self.assertIn("> **Release candidate.** v2.0.0 as it is meant to ship", self.notes("v2.0.0-rc.1"))
        self.assertIn("> **Beta.** A pre-release", self.notes("v2.0.0-beta.1"))
        for tag in ("v2.0.0-rc.1", "v2.0.0-beta.1"):
            notes = self.notes(tag)
            self.assertIn("- Something new.", notes)
            self.assertIn(f"Indigo-{tag}.zip", notes)

    def test_changelog_links_name_the_file_at_the_tag(self):
        """The API and gh pass a relative link on as written, so a file in the
        repository is linked at the tag by full URL; anchors and full URLs stay as written."""
        notes = self.notes("v2.0.0-rc.1", "- See [RELEASING](docs/RELEASING.md), [the examples](docs/examples/),"
                                          " [AGENTS.md](AGENTS.md#rules), [above](#install) and"
                                          " [the site](https://norvitech.com/indigo/).")
        repo = "https://github.com/spencernorton/indigo"
        self.assertIn(f"[RELEASING]({repo}/blob/v2.0.0-rc.1/docs/RELEASING.md)", notes)
        self.assertIn(f"[the examples]({repo}/tree/v2.0.0-rc.1/docs/examples/)", notes)
        self.assertIn(f"[AGENTS.md]({repo}/blob/v2.0.0-rc.1/AGENTS.md#rules)", notes)
        self.assertIn("[above](#install)", notes)
        self.assertIn("[the site](https://norvitech.com/indigo/)", notes)
        self.assertNotIn("](docs/", notes)

    def test_install_keeps_the_card_safe(self):
        """Release instructions copy two files and never a whole swiss folder (a Mac's
        Replace on that folder deletes settings and saves), back up first, and name
        the file to update for every setup."""
        install = self.notes("v2.0.0-rc.1").split("## Install", 1)[1]
        self.assertLess(install.index("back up the card's `swiss` folder"), install.index("copy two files"))
        self.assertIn("`swiss/patches/apploader.img`", install)
        self.assertNotIn("Merge", install)
        for destination in ("`apps/indigo.dol`", "`ipl.dol` at the top of the card",
                            "`boot.dol` at the top of its microSD card"):
            self.assertIn(destination, install)
        updating = install.split("**Updating:**", 1)[1]
        for name in ("`ipl.dol`", "`boot.dol`", "`indigo.dol`"):
            self.assertIn(name, updating)



if __name__ == "__main__":
    unittest.main()
