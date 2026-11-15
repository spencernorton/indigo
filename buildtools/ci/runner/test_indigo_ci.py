#!/usr/bin/env python3
"""The supervisor's decisions, without Docker or GitHub."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

os.environ["INDIGO_CI_NAME"] = "ci-host"
import indigo_ci  # noqa: E402


class Pools(unittest.TestCase):
    def test_default_and_custom(self):
        self.assertEqual(indigo_ci.parse_pools("build:3,emulator:1"), {"build": 3, "emulator": 1})
        self.assertEqual(indigo_ci.parse_pools("build:4,emulator:4"), {"build": 4, "emulator": 4})
        self.assertEqual(indigo_ci.POOLS["build"].repo, indigo_ci.REPO)
        self.assertEqual(indigo_ci.parse_pools(" build:0 "), {"build": 0})

    def test_bad_entries_stop_the_supervisor(self):
        for spec in ("", "build", "build:x", "build:17", "mystery:1", "build:1,build:2"):
            with self.assertRaises(SystemExit, msg=spec):
                indigo_ci.parse_pools(spec)


class RunnerRelease(unittest.TestCase):
    body = ("- actions-runner-linux-arm64-2.337.0.tar.gz <!-- BEGIN SHA linux-arm64 -->"
            + "b" * 64 + "<!-- END SHA linux-arm64 -->\n"
            "- actions-runner-linux-x64-2.337.0.tar.gz <!-- BEGIN SHA linux-x64 -->"
            + "a" * 64 + "<!-- END SHA linux-x64 -->\n")

    def test_version_and_linux_x64_checksum(self):
        self.assertEqual(indigo_ci.parse_runner_release({"tag_name": "v2.337.0", "body": self.body}),
                         ("2.337.0", "a" * 64))

    def test_refuses_a_release_it_cannot_verify(self):
        for release in ({"tag_name": "v2.337.0", "body": "no checksums"},
                        {"tag_name": "latest", "body": self.body}, {}):
            with self.assertRaises(RuntimeError):
                indigo_ci.parse_runner_release(release)


class ImageTag(unittest.TestCase):
    def test_changes_with_its_inputs_and_the_runner_only(self):
        with tempfile.TemporaryDirectory() as directory:
            context = Path(directory)
            names = {name for pool in indigo_ci.POOLS.values() for name in (pool.dockerfile, *pool.copies)}
            for name in names:
                (context / name).parent.mkdir(parents=True, exist_ok=True)
                (context / name).write_text(name)
            build, emulator = indigo_ci.POOLS["build"], indigo_ci.POOLS["emulator"]
            runner = ("2.337.0", "a" * 64)
            first = indigo_ci.image_tag(build, context, runner)
            self.assertRegex(first, r"^indigo-ci/build:[0-9a-f]{16}$")
            self.assertEqual(first, indigo_ci.image_tag(build, context, runner))
            self.assertNotEqual(first, indigo_ci.image_tag(build, context, ("2.338.0", "c" * 64)))
            (context / "README.md").write_text("docs")  # not an input: no rebuild
            self.assertEqual(first, indigo_ci.image_tag(build, context, runner))
            emulator_tag = indigo_ci.image_tag(emulator, context, runner)
            (context / "egress_proxy.py").write_text("changed")  # the build image copies it
            self.assertNotEqual(first, indigo_ci.image_tag(build, context, runner))
            self.assertEqual(emulator_tag, indigo_ci.image_tag(emulator, context, runner))
            (context / "dolphin/0002-flush-sd-writes.patch").write_text("changed")  # Dolphin's patches
            self.assertNotEqual(emulator_tag, indigo_ci.image_tag(emulator, context, runner))


class Dockerfiles(unittest.TestCase):
    def test_each_pool_declares_what_its_dockerfile_copies(self):
        here = Path(__file__).resolve().parent
        for pool in indigo_ci.POOLS.values():
            copied = set()
            for line in (here / pool.dockerfile).read_text().splitlines():
                if line.startswith("COPY ") and "--from=" not in line:
                    words = [w for w in line.split()[1:] if not w.startswith("--")]
                    copied.update(words[:-1])
            self.assertEqual(copied, set(pool.copies), pool.dockerfile)
            for name in pool.copies:
                self.assertTrue((here / name).is_file(), name)


class Cleanup(unittest.TestCase):
    def test_only_this_machines_offline_runners_without_a_container(self):
        live = {"ci-host-build-1-aaaaaa"}
        self.assertTrue(indigo_ci.ours({"name": "ci-host-build-2-bbbbbb", "status": "offline"}, live))
        self.assertFalse(indigo_ci.ours({"name": "ci-host-build-1-aaaaaa", "status": "offline"}, live))
        self.assertFalse(indigo_ci.ours({"name": "ci-host-build-2-bbbbbb", "status": "online"}, live))
        self.assertFalse(indigo_ci.ours({"name": "other-host-build-1-cccccc", "status": "offline"}, live))
        self.assertFalse(indigo_ci.ours({"name": "ci-host2-build-1", "status": "offline"}, live))

    def test_names(self):
        self.assertRegex(indigo_ci.runner_name("build", 2), r"^ci-host-build-2-[0-9a-f]{6}$")
        self.assertEqual(indigo_ci.container_name("emulator", 1), "indigo-ci-emulator-1")

    def test_unnamed_machine_is_not_named_after_its_host(self):
        """Runner names are public in job logs, so without INDIGO_CI_NAME they say indigo."""
        env = {k: v for k, v in os.environ.items() if k != "INDIGO_CI_NAME"}
        name = subprocess.run([sys.executable, "-c", "import indigo_ci; print(indigo_ci.NAME)"],
                              cwd=Path(__file__).parent, env=env, capture_output=True, text=True,
                              check=True).stdout.strip()
        self.assertEqual(name, "indigo")


class Alerts(unittest.TestCase):
    def setUp(self):
        quiet = mock.patch.object(indigo_ci, "log", lambda *_: None)
        quiet.start()
        self.addCleanup(quiet.stop)

    def test_github_timing_out_is_retried_then_alerted_once_and_resolved(self):
        supervisor = indigo_ci.Supervisor({"build": 1})
        outcomes = [TimeoutError("The read operation timed out")] * (indigo_ci.ALERT_AFTER + 1) + [None, None]

        def tick(now):
            outcome = outcomes.pop(0)
            if outcome:
                raise outcome
        supervisor.tick = tick
        alerts = []
        with mock.patch.object(indigo_ci, "alert", lambda message, key, resolved=False: alerts.append((key, resolved))):
            for _ in range(indigo_ci.ALERT_AFTER + 3):
                supervisor.guarded_tick(0)
        self.assertEqual(alerts, [("supervisor", False), ("supervisor", True)])
        self.assertEqual(supervisor.tick_failures, 0)

    def test_a_bug_still_stops_the_supervisor(self):
        supervisor = indigo_ci.Supervisor({"build": 1})
        supervisor.tick = lambda now: None + 1
        with self.assertRaises(TypeError):
            supervisor.guarded_tick(0)

    def test_the_command_hears_what_and_whether_and_a_broken_one_is_survived(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory, "alert")
            command = f"sh -c 'printf \"%s|%s|%s\" \"$INDIGO_CI_ALERT_KEY\" \"$INDIGO_CI_ALERT_STATE\" \"$0\" > {out}'"
            with mock.patch.dict(os.environ, {"INDIGO_CI_ALERT": command}):
                indigo_ci.alert("the build pool starts runners again", "build", resolved=True)
            self.assertEqual(out.read_text(), "build|resolved|Indigo CI on ci-host: the build pool starts runners again")
        with mock.patch.dict(os.environ, {"INDIGO_CI_ALERT": "/nonexistent/indigo-ci-alert"}):
            indigo_ci.alert("the supervisor keeps failing", "supervisor")


class Timestamps(unittest.TestCase):
    def test_docker_started_at_is_utc(self):
        self.assertEqual(indigo_ci._epoch("1970-01-01T00:01:40.123456789Z"), 100.0)
        self.assertEqual(indigo_ci._epoch("2026-07-01T00:00:00Z"), 1782864000.0)


class HealthAndBusyProtection(unittest.TestCase):
    def setUp(self):
        quiet = mock.patch.object(indigo_ci, "log", lambda *_: None)
        quiet.start()
        self.addCleanup(quiet.stop)

    def _running_container(self, runner: str, image: str = "indigo-ci/emulator:abc") -> dict:
        return {
            "State": {"Running": True, "StartedAt": "1970-01-01T00:00:00Z"},
            "Config": {"Image": image, "Labels": {f"{indigo_ci.LABEL}.runner": runner}},
        }

    def test_busy_runner_with_offline_status_blip_is_never_killed(self):
        supervisor = indigo_ci.Supervisor({"emulator": 1})
        supervisor.images = {"emulator": "indigo-ci/emulator:abc"}
        supervisor.refresh = lambda now, force=False: None
        supervisor.ensure_proxy = lambda busy=True: None
        runner = "ci-host-emulator-1-111111"
        removed = []
        with mock.patch.object(indigo_ci, "inspect", return_value=self._running_container(runner)), \
             mock.patch.object(indigo_ci, "repo_runners", return_value=[{
                 "id": 7, "name": runner, "repo": indigo_ci.REPO, "status": "offline", "busy": True}]), \
             mock.patch.object(indigo_ci, "docker", side_effect=lambda *args, **kw: removed.append(args)):
            for step in range(5):
                supervisor.tick(1000.0 + step * indigo_ci.HEALTH_EVERY)
        self.assertEqual(removed, [])

    def test_single_offline_blip_is_debounced_before_replacing_idle_runner(self):
        supervisor = indigo_ci.Supervisor({"emulator": 1})
        supervisor.images = {"emulator": "indigo-ci/emulator:abc"}
        supervisor.refresh = lambda now, force=False: None
        supervisor.ensure_proxy = lambda busy=True: None
        runner = "ci-host-emulator-1-222222"
        statuses = ["offline", "online", "offline", "offline"]
        removed = []
        deleted = []
        supervisor.delete_registration = lambda reg: deleted.append(reg.get("id"))
        with mock.patch.object(indigo_ci, "inspect", return_value=self._running_container(runner)), \
             mock.patch.object(indigo_ci, "repo_runners", side_effect=lambda _: [{
                 "id": 9, "name": runner, "repo": indigo_ci.REPO,
                 "status": statuses.pop(0), "busy": False}]), \
             mock.patch.object(indigo_ci, "docker", side_effect=lambda *args, **kw: removed.append(args)):
            supervisor.tick(1000.0)
            self.assertEqual(removed, [])  # 1st offline blip: kept
            supervisor.tick(1000.0 + indigo_ci.HEALTH_EVERY)
            self.assertEqual(removed, [])  # recovered online: strikes reset
            supervisor.tick(1000.0 + 2 * indigo_ci.HEALTH_EVERY)
            self.assertEqual(removed, [])  # 1st strike again: kept
            supervisor.tick(1000.0 + 3 * indigo_ci.HEALTH_EVERY)
            self.assertEqual(removed, [("rm", "--force", "indigo-ci-emulator-1")])
            self.assertEqual(deleted, [9])

    def test_cleanup_ignores_busy_and_survives_http_422_to_remove_next_runner(self):
        import io
        import urllib.error
        live: set[str] = set()
        self.assertFalse(indigo_ci.ours(
            {"name": "ci-host-emulator-1-aaaaaa", "status": "offline", "busy": True}, live))
        supervisor = indigo_ci.Supervisor({"emulator": 0})
        supervisor.refresh = lambda now, force=False: None
        supervisor.ensure_proxy = lambda busy=True: None
        calls = []

        def fake_github(method, path, body=None):
            calls.append((method, path))
            if path.endswith("/1"):
                raise urllib.error.HTTPError(path, 422, "Unprocessable Entity", {}, io.BytesIO(b"busy"))
            return {}

        with mock.patch.object(indigo_ci, "repo_runners", return_value=[
                {"id": 1, "name": "ci-host-emulator-1-aaaaaa", "repo": indigo_ci.REPO,
                 "status": "offline", "busy": False},
                {"id": 2, "name": "ci-host-emulator-2-bbbbbb", "repo": indigo_ci.REPO,
                 "status": "offline", "busy": False},
             ]), \
             mock.patch.object(indigo_ci, "github", side_effect=fake_github):
            supervisor.tick(1000.0)
        self.assertEqual(calls, [
            ("DELETE", f"/repos/{indigo_ci.REPO}/actions/runners/1"),
            ("DELETE", f"/repos/{indigo_ci.REPO}/actions/runners/2"),
        ])


    def test_non_health_ticks_do_not_query_github_runner_api(self):
        supervisor = indigo_ci.Supervisor({"emulator": 1})
        supervisor.images = {"emulator": "indigo-ci/emulator:abc"}
        supervisor.refresh = lambda now, force=False: None
        supervisor.ensure_proxy = lambda busy=True: None
        supervisor.next_health = 1060.0
        supervisor.next_cleanup = 1300.0
        runner = "ci-host-emulator-1-333333"
        container = {
            "State": {"Running": True, "StartedAt": "1970-01-01T00:16:00Z"},
            "Config": {"Image": "indigo-ci/emulator:abc", "Labels": {f"{indigo_ci.LABEL}.runner": runner}},
        }
        with mock.patch.object(indigo_ci, "inspect", return_value=container), \
             mock.patch.object(indigo_ci, "repo_runners", side_effect=AssertionError("unexpected API call")):
            supervisor.tick(1005.0)
            supervisor.tick(1010.0)

if __name__ == "__main__":
    unittest.main()
