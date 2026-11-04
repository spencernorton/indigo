#!/usr/bin/env python3
"""Keep Indigo's self-hosted GitHub Actions runners running on this machine.

Every runner is a fresh container that registers just in time, takes exactly
one job and is thrown away. A job runs as an unprivileged user with no Docker
socket, no host mounts and no capabilities, on a network of its own whose
only way out is the restricted egress proxy. README.md beside this
file has the design.

    indigo_ci.py run       keep every pool's slots filled (what the unit runs)
    indigo_ci.py status    containers on this machine and runners on GitHub
    indigo_ci.py build     build the images from the trusted branch now
    indigo_ci.py stop      remove this machine's runners, containers and proxy

Environment:
    GH_TOKEN          admin on the repository (it mints runner registrations)
    INDIGO_CI_POOLS   slots per pool, default "build:3,emulator:1"
    INDIGO_CI_REPO    default spencernorton/indigo (its pools, and the images' source)
    INDIGO_CI_REF     the trusted branch images are built from, default beta
    INDIGO_CI_NAME    the prefix of this machine's runner names, default "indigo";
                      runner names show in public job logs, so never a hostname.
                      A second machine needs a name of its own ("indigo2"):
                      cleanup removes offline runners that carry its prefix
    INDIGO_CI_HOME    state and the source checkout, ~/.local/share/indigo-ci
    INDIGO_CI_ALERT   optional command, run with one message argument when a
                      pool cannot start runners, an image will not build or the
                      supervisor keeps failing, and again when it recovers.
                      INDIGO_CI_ALERT_KEY says what ("build", "build-image",
                      "supervisor", ...) and INDIGO_CI_ALERT_STATE is "firing"
                      or "resolved"
"""

from __future__ import annotations

import calendar
import hashlib
import json
import os
import re
import secrets
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

API = "https://api.github.com"
LABEL = "indigo-ci"
PROXY = "indigo-ci-proxy"
PROXY_PORT = 3128
EGRESS_NETWORK = "indigo-ci-egress"
CONTEXT = "buildtools/ci/runner"
RUNNER_UID = "1001:1001"
TICK_SECONDS = 15
SOURCE_EVERY = 600          # look for new image definitions on the trusted branch
RUNNER_RELEASE_EVERY = 21600  # look for a new actions/runner release
CLEANUP_EVERY = 300         # drop registrations whose container is gone
MAX_IDLE_AGE = 12 * 3600    # replace an idle runner this old with a fresh one
STUCK_AFTER = 180           # a runner still offline this long after it started is replaced
HEALTH_EVERY = 60           # how often to ask GitHub whether running runners are connected
ALERT_AFTER = 5             # consecutive failed starts, or failed ticks, before INDIGO_CI_ALERT


REPO = os.environ.get("INDIGO_CI_REPO", "spencernorton/indigo")


@dataclass(frozen=True)
class Pool:
    name: str
    dockerfile: str
    copies: tuple[str, ...]  # what the Dockerfile COPYs: with it, the image's inputs
    label: str
    cpus: str
    memory: str
    repo: str = REPO  # whose runners the pool's slots register as
    # CPU weight when the machine is short of it (docker --cpu-shares): an
    # emulator's console runs in real time, a build only waits longer.
    shares: int = 1024


POOLS = {
    "build": Pool("build", "build.Dockerfile", ("entrypoint.sh", "egress_proxy.py", "libiconv-lite.sh"),
                  "indigo-build", "8", "6g"),
    "emulator": Pool("emulator", "emulator.Dockerfile",
                     ("entrypoint.sh", "dolphin/0001-sd2sp2-adapter.patch", "dolphin/0002-flush-sd-writes.patch",
                      "dolphin/0003-controller-no-response.patch", "dolphin/0004-sd-faults.patch",
                      "dolphin/0005-emulated-clock.patch", "dolphin/0006-gcloader.patch",
                      "dolphin/0007-dabr.patch"),
                     "indigo-emulator", "8", "6g", shares=4096),
}
REF = os.environ.get("INDIGO_CI_REF", "beta")
NAME = (os.environ.get("INDIGO_CI_NAME") or "indigo").lower()
HOME = Path(os.environ.get("INDIGO_CI_HOME") or Path.home() / ".local/share/indigo-ci")
SOURCE = HOME / "src"
SELF = Path(__file__).resolve()
SELF_DIGEST = hashlib.sha256(SELF.read_bytes()).hexdigest()


def log(message: str) -> None:
    print(time.strftime("%Y-%m-%dT%H:%M:%S ") + message, flush=True)


def parse_pools(spec: str) -> dict[str, int]:
    pools: dict[str, int] = {}
    for item in filter(None, (part.strip() for part in spec.split(","))):
        name, _, count = item.partition(":")
        if name not in POOLS or not count.isdigit() or not 0 <= int(count) <= 16 or name in pools:
            raise SystemExit(f"INDIGO_CI_POOLS: bad entry {item!r} (pools: {', '.join(POOLS)})")
        pools[name] = int(count)
    if not pools:
        raise SystemExit("INDIGO_CI_POOLS names no pool")
    return pools


def runner_name(pool: str, slot: int) -> str:
    return f"{NAME}-{pool}-{slot}-{secrets.token_hex(3)}"


def container_name(pool: str, slot: int) -> str:
    return f"indigo-ci-{pool}-{slot}"


def slot_network(pool: str, slot: int) -> str:
    return f"indigo-ci-{pool}-{slot}"


def parse_runner_release(release: dict) -> tuple[str, str]:
    """(version, sha256 of the linux-x64 tarball) from an actions/runner release."""
    version = str(release.get("tag_name", "")).lstrip("v")
    match = re.search(r"<!-- BEGIN SHA linux-x64 -->([0-9a-f]{64})<!-- END SHA linux-x64 -->",
                      str(release.get("body", "")))
    if not re.fullmatch(r"\d+\.\d+\.\d+", version) or not match:
        raise RuntimeError("actions/runner release has no version or linux-x64 checksum")
    return version, match.group(1)


def image_tag(pool: Pool, context: Path, runner: tuple[str, str]) -> str:
    """Content address of a pool's image: its Dockerfile, what it copies, the runner release."""
    digest = hashlib.sha256()
    for name in (pool.dockerfile, *sorted(pool.copies)):
        digest.update(name.encode() + b"\0" + (context / name).read_bytes() + b"\0")
    digest.update(f"{runner[0]}\0{runner[1]}".encode())
    return f"indigo-ci/{pool.name}:{digest.hexdigest()[:16]}"


def ours(runner: dict, live: set[str]) -> bool:
    """A registration this machine made whose container is gone and that is offline."""
    return (str(runner.get("name", "")).startswith(NAME + "-") and runner.get("status") == "offline"
            and runner.get("name") not in live)


# ---------------------------------------------------------------- side effects

def run(*command: str, check: bool = True, timeout: float = 120,
        capture: bool = True, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    result = subprocess.run(command, text=True, timeout=timeout, env=env,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None)
    if check and result.returncode != 0:
        tail = "\n".join((result.stdout or "").splitlines()[-30:])
        raise RuntimeError(f"{shlex.join(command)} exited {result.returncode}\n{tail}")
    return result


def docker(*args: str, check: bool = True, timeout: float = 120) -> subprocess.CompletedProcess:
    return run("docker", *args, check=check, timeout=timeout)


def github(method: str, path: str, body: dict | None = None) -> object:
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if not token:
        raise SystemExit("GH_TOKEN is not set")
    request = urllib.request.Request(
        API + path, method=method,
        data=None if body is None else json.dumps(body).encode(),
        headers={"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28",
                 "Authorization": f"Bearer {token}", "User-Agent": "indigo-ci"})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    return json.loads(data) if data else None


def repo_runners(repos: set[str]) -> list[dict]:
    """Every runner registered with these repositories, each marked with its repository."""
    runners: list[dict] = []
    for repo in sorted(repos):
        for page in range(1, 20):
            batch = github("GET", f"/repos/{repo}/actions/runners?per_page=100&page={page}")["runners"]
            runners += [{**runner, "repo": repo} for runner in batch]
            if len(batch) < 100:
                break
    return runners


def inspect(name: str) -> dict | None:
    result = docker("inspect", "--type", "container", name, check=False)
    return json.loads(result.stdout)[0] if result.returncode == 0 else None


def image_exists(tag: str) -> bool:
    return docker("image", "inspect", tag, check=False).returncode == 0


def alert(message: str, key: str, resolved: bool = False) -> None:
    log("ALERT " + message)
    command = os.environ.get("INDIGO_CI_ALERT")
    if command:
        env = {**os.environ, "INDIGO_CI_ALERT_KEY": key,
               "INDIGO_CI_ALERT_STATE": "resolved" if resolved else "firing"}
        try:
            run(*shlex.split(command), f"Indigo CI on {NAME}: {message}", check=False, env=env)
        except (OSError, subprocess.TimeoutExpired) as error:
            # A broken alert command is logged; it must not take the runners down with it.
            log(f"INDIGO_CI_ALERT failed: {error}")


class Supervisor:
    def __init__(self, pools: dict[str, int]) -> None:
        self.pools = pools
        self.images: dict[str, str] = {}
        self.runner: tuple[str, str] | None = None
        self.next_source = self.next_release = self.next_cleanup = self.next_health = 0.0
        self.failures: dict[str, int] = {}
        self.backoff: dict[str, float] = {}
        self.tick_failures = 0
        self.broken_images: set[str] = set()
        self.stopping = False

    # -- images from the trusted branch
    def refresh(self, now: float, force: bool = False) -> None:
        if force or now >= self.next_release or self.runner is None:
            self.runner = parse_runner_release(github("GET", "/repos/actions/runner/releases/latest"))
            self.next_release = now + RUNNER_RELEASE_EVERY
        if not (force or now >= self.next_source or not self.images):
            return
        self.next_source = now + SOURCE_EVERY
        if not (SOURCE / ".git").exists():
            SOURCE.parent.mkdir(parents=True, exist_ok=True)
            run("git", "clone", "--quiet", "--no-checkout", f"https://github.com/{REPO}.git", str(SOURCE))
        run("git", "-C", str(SOURCE), "fetch", "--quiet", "--prune", "origin",
            f"+refs/heads/{REF}:refs/remotes/origin/{REF}", timeout=300)
        run("git", "-C", str(SOURCE), "checkout", "--quiet", "--force", "--detach", f"origin/{REF}")
        context = SOURCE / CONTEXT
        if SELF.is_relative_to(SOURCE.resolve()) and \
                hashlib.sha256(SELF.read_bytes()).hexdigest() != SELF_DIGEST:
            log(f"{REF} changed the supervisor; exiting so the service restarts it")
            self.stopping = True
            return
        for name in self.pools:
            pool = POOLS[name]
            tag = image_tag(pool, context, self.runner)
            if not image_exists(tag):
                log(f"building {tag} from {REF} with runner {self.runner[0]}")
                try:
                    run("docker", "build", "--file", str(context / pool.dockerfile),
                        "--build-arg", f"RUNNER_VERSION={self.runner[0]}",
                        "--build-arg", f"RUNNER_SHA256={self.runner[1]}",
                        "--label", f"{LABEL}.pool={name}", "--tag", tag, str(context), timeout=5400)
                except (RuntimeError, subprocess.TimeoutExpired) as error:
                    if name not in self.broken_images:
                        self.broken_images.add(name)
                        alert(f"cannot build the {name} image; keeping {self.images.get(name, 'none')}: {error}",
                              f"{name}-image")
                    continue
            if name in self.broken_images:
                self.broken_images.discard(name)
                alert(f"the {name} image builds again", f"{name}-image", resolved=True)
            if self.images.get(name) != tag:
                log(f"{name} pool now uses {tag}")
            self.images[name] = tag

    # -- the proxy and the networks
    def ensure_network(self, network: str, internal: bool) -> None:
        if docker("network", "inspect", network, check=False).returncode != 0:
            docker("network", "create", "--driver", "bridge", "--label", LABEL,
                   *(["--internal"] if internal else []), network)

    def ensure_proxy(self, busy: bool = True) -> None:
        """Keep the proxy running and on every slot's network. A new egress_proxy.py
        replaces it only while no job runs: replacing it cuts every runner off."""
        image = self.images.get("build")
        if not image:
            return
        script = hashlib.sha256((SOURCE / CONTEXT / "egress_proxy.py").read_bytes()).hexdigest()[:16]
        self.ensure_network(EGRESS_NETWORK, internal=False)
        info = inspect(PROXY)
        if info and info["Config"]["Labels"].get(f"{LABEL}.proxy") != script and not busy:
            log(f"replacing the egress proxy for egress_proxy.py {script}")
            docker("rm", "--force", PROXY)
            info = None
        if info is None:
            docker("run", "--detach", "--name", PROXY, "--restart", "unless-stopped",
                   "--label", LABEL, "--label", f"{LABEL}.proxy={script}",
                   "--network", EGRESS_NETWORK, "--runtime", "runc",
                   "--user", RUNNER_UID, "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
                   "--read-only", "--memory", "256m", "--pids-limit", "256",
                   "--env", "PYTHONDONTWRITEBYTECODE=1", "--entrypoint", "python3",
                   image, "/opt/indigo-ci/egress_proxy.py")
            info = inspect(PROXY) or {}
        elif not info["State"]["Running"]:
            docker("start", PROXY)
        joined = info.get("NetworkSettings", {}).get("Networks", {})
        for pool, count in self.pools.items():
            for slot in range(1, count + 1):
                network = slot_network(pool, slot)
                self.ensure_network(network, internal=True)
                if network not in joined:
                    docker("network", "connect", network, PROXY)

    # -- one runner per slot
    def launch(self, pool: Pool, slot: int) -> str:
        name = runner_name(pool.name, slot)
        network = slot_network(pool.name, slot)
        config = github("POST", f"/repos/{pool.repo}/actions/runners/generate-jitconfig", {
            "name": name, "runner_group_id": 1, "work_folder": "_work",
            "labels": ["self-hosted", "linux", "x64", pool.label]})["encoded_jit_config"]
        proxy = f"http://{PROXY}:{PROXY_PORT}"
        with tempfile.NamedTemporaryFile("w", dir=HOME, prefix=".env-", delete=True) as env:
            os.chmod(env.name, 0o600)
            env.write(f"INDIGO_RUNNER_JITCONFIG={config}\n")
            for variable in ("https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY"):
                env.write(f"{variable}={proxy}\n")
            env.write("no_proxy=localhost,127.0.0.1\nNO_PROXY=localhost,127.0.0.1\n")
            env.flush()
            docker("run", "--detach", "--name", container_name(pool.name, slot),
                   "--label", LABEL, "--label", f"{LABEL}.pool={pool.name}",
                   "--label", f"{LABEL}.runner={name}", "--network", network, "--runtime", "runc",
                   "--user", RUNNER_UID, "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
                   "--pids-limit", "4096", "--cpus", pool.cpus, "--cpu-shares", str(pool.shares),
                   "--memory", pool.memory,
                   "--memory-swap", pool.memory, "--shm-size", "1g", "--env-file", env.name,
                   self.images[pool.name])
        log(f"started {name} in {container_name(pool.name, slot)}")
        return name

    def tick(self, now: float) -> None:
        try:
            self.refresh(now)
        except (OSError, RuntimeError, subprocess.TimeoutExpired, urllib.error.URLError, KeyError) as error:
            log(f"refresh failed, keeping current images: {error}")
        try:
            self.ensure_proxy()
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            log(f"egress proxy: {error}")
        runners: dict[str, dict] | None = None

        def registrations() -> dict[str, dict]:
            nonlocal runners
            if runners is None:
                runners = {r["name"]: r for r in repo_runners({POOLS[p].repo for p in self.pools})}
            return runners

        health = now >= self.next_health
        if health:
            self.next_health = now + HEALTH_EVERY
        live: set[str] = set()
        for pool_name, count in self.pools.items():
            pool = POOLS[pool_name]
            for slot in range(1, count + 1):
                container = container_name(pool_name, slot)
                info = inspect(container)
                if info and info["State"]["Running"]:
                    runner = info["Config"]["Labels"].get(f"{LABEL}.runner", "")
                    live.add(runner)
                    outdated = info["Config"]["Image"] != self.images.get(pool_name)
                    started = info["State"].get("StartedAt", "")
                    old = started and now - _epoch(started) > MAX_IDLE_AGE
                    if (outdated or old) and not registrations().get(runner, {}).get("busy", True):
                        log(f"retiring idle {runner} ({'new image' if outdated else 'age'})")
                        docker("rm", "--force", container)
                        self.delete_registration(registrations().get(runner))
                    elif health and started and now - _epoch(started) > STUCK_AFTER and \
                            registrations().get(runner, {}).get("status") != "online":
                        log(f"{runner} is not connected to GitHub; replacing it")
                        docker("rm", "--force", container)
                        self.delete_registration(registrations().get(runner))
                    continue
                if info:
                    code = info["State"].get("ExitCode")
                    if code:
                        tail = docker("logs", "--tail", "15", container, check=False).stdout
                        log(f"{container} exited {code}:\n{tail}")
                    docker("rm", "--force", container)
                key = f"{pool_name}-{slot}"
                if now < self.backoff.get(key, 0) or pool_name not in self.images:
                    continue
                try:
                    # Live from now: a runner shows offline until it connects,
                    # and cleanup below must not take that for a dead one.
                    live.add(self.launch(pool, slot))
                    if self.failures.get(pool_name, 0) >= ALERT_AFTER:
                        alert(f"the {pool_name} pool starts runners again", pool_name, resolved=True)
                    self.failures[pool_name] = 0
                    self.backoff.pop(key, None)
                except (OSError, RuntimeError, subprocess.TimeoutExpired, urllib.error.URLError,
                        KeyError) as error:
                    count_failed = self.failures.get(pool_name, 0) + 1
                    self.failures[pool_name] = count_failed
                    self.backoff[key] = now + min(300, 15 * 2 ** min(count_failed, 5))
                    log(f"cannot start {container}: {error}")
                    if count_failed == ALERT_AFTER:
                        alert(f"the {pool_name} pool cannot start runners: {error}", pool_name)
        if now >= self.next_cleanup:
            self.next_cleanup = now + CLEANUP_EVERY
            try:
                for runner in registrations().values():
                    if ours(runner, live):
                        self.delete_registration(runner)
            except (OSError, urllib.error.URLError, KeyError) as error:
                log(f"cleanup failed: {error}")
        if runners is not None:
            try:
                self.ensure_proxy(busy=any(r.get("busy") for r in runners.values()))
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                log(f"egress proxy: {error}")

    def delete_registration(self, runner: dict | None) -> None:
        if runner and runner.get("id"):
            try:
                github("DELETE", f"/repos/{runner['repo']}/actions/runners/{runner['id']}")
                log(f"removed registration {runner.get('name')}")
            except urllib.error.HTTPError as error:
                if error.code != 404:
                    raise

    def guarded_tick(self, now: float) -> None:
        """A tick that fails on GitHub or Docker is retried on the next one, not fatal.

        On 2026-09-27 one slow GitHub reply in the health check ended the
        service; had GitHub stayed slow, systemd would have restarted it every
        few seconds without anyone hearing. A bug still raises.
        """
        try:
            self.tick(now)
        except (OSError, RuntimeError, subprocess.TimeoutExpired, urllib.error.URLError, KeyError) as error:
            self.tick_failures += 1
            log(f"tick failed, retrying: {error}")
            if self.tick_failures == ALERT_AFTER:
                alert(f"the supervisor keeps failing: {error}", "supervisor")
            return
        if self.tick_failures >= ALERT_AFTER:
            alert("the supervisor works again", "supervisor", resolved=True)
        self.tick_failures = 0

    def loop(self) -> None:
        signal.signal(signal.SIGTERM, lambda *_: setattr(self, "stopping", True))
        HOME.mkdir(parents=True, exist_ok=True)
        log(f"{NAME}: pools {self.pools}, images from {REPO} {REF}")
        while not self.stopping:
            self.guarded_tick(time.time())
            for _ in range(TICK_SECONDS):
                if self.stopping:
                    break
                time.sleep(1)
        log("stopping; running jobs finish in their containers")


def _epoch(stamp: str) -> float:
    # Docker's StartedAt: 2026-09-27T12:00:00.123456789Z
    return float(calendar.timegm(time.strptime(stamp[:19], "%Y-%m-%dT%H:%M:%S")))


def status(pools: dict[str, int]) -> None:
    print(docker("ps", "--all", "--filter", f"label={LABEL}",
                 "--format", "table {{.Names}}\t{{.Status}}\t{{.Image}}").stdout)
    for runner in sorted(repo_runners({POOLS[p].repo for p in pools}), key=lambda r: r["name"]):
        labels = ",".join(label["name"] for label in runner.get("labels", []))
        state = "busy" if runner.get("busy") else runner.get("status")
        print(f"{runner['name']:32} {state:8} {labels}")


def stop(pools: dict[str, int]) -> None:
    names = docker("ps", "--all", "--quiet", "--filter", f"label={LABEL}").stdout.split()
    if names:
        docker("rm", "--force", *names)
    for runner in repo_runners({POOLS[p].repo for p in pools}):
        if str(runner.get("name", "")).startswith(NAME + "-"):
            github("DELETE", f"/repos/{runner['repo']}/actions/runners/{runner['id']}")
            print(f"removed {runner['name']}")


def main(argv: list[str]) -> int:
    command = argv[1] if len(argv) > 1 else "run"
    pools = parse_pools(os.environ.get("INDIGO_CI_POOLS", "build:3,emulator:1"))
    if command == "run":
        Supervisor(pools).loop()
    elif command == "build":
        HOME.mkdir(parents=True, exist_ok=True)
        supervisor = Supervisor(pools)
        supervisor.refresh(time.time(), force=True)
        print(json.dumps(supervisor.images, indent=2))
    elif command == "status":
        status(pools)
    elif command == "stop":
        stop(pools)
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
