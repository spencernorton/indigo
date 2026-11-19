# Indigo software update — implementation design (UPDATE_DESIGN)

Status: design only. No code in this document has been implemented, compiled,
or run on hardware. Written 2026-11-19 against Indigo `beta`
`8070d19fc66c11b7899b932760a95f795e2ab109` (`v2.9.0-4-g8070d19`).

Deliverable note: this file is delivered at `/tmp/UPDATE_DESIGN.md` because
`/Users/spencernorton/Kleos/indigo` is the canonical shared checkout and must
stay untouched until implementation. The first implementation pull request
copies it, unchanged, to `docs/dev/software-update-design.md` in its branch
(`docs/` is an Indigo-owned path) and implements against it.

Basis: four discovery reports, every load-bearing claim re-verified by opening
the cited body in the checkout above:

- `/tmp/indigo-platform-report-2026-11-18.md`
- `/tmp/swiss-indigo-upstream-audit-2026-11-18.md`
- `/tmp/swiss-indigo-network-updater-audit.md`
- `/tmp/indigo-release-settings-report-2026-11-18.md`

In citations, `$INDIGO` = `/Users/spencernorton/Kleos/indigo`.

## 0. What is being built, and what is not

One user-initiated software updater in Settings › Network, with **one shared
verify+apply pipeline** and **two transports** that only differ in how the
manifest and package bytes are fetched:

- **Check for updates** — plain HTTP from a configurable channel base
  (IPv4-literal host, see §2), and
- **Update from SMB share** — the share already configured for browsing.

Both end in the same staging, SHA-256 + structural verification, backup,
atomic replace, and restart offer. Updating replaces the two files a manual
update replaces today (`$INDIGO/buildtools/sd_package.sh:84-118`, "UPDATING
INDIGO"): the boot DOL and `swiss/patches/apploader.img`.

Non-goals, deliberately: no background or startup check of any kind (nothing
runs until the user presses one of the two rows); no HTTPS/TLS and no DNS
(neither exists in the DOL, §2); no update of the bundled CubeBrowser DOL or
its CA bundle (separate pinned program, `sd_package.sh:49-76`); no poster or
cheat packs; no new filesystem device; no zip handling (there is no zip
reader in the tree); no Swiss upstream rebase as part of this feature (§1
keeps that a separate pull request).

## 1. Swiss integration verdict

### 1.1 Exact current state (verified 2026-11-18/19)

| Item | Value |
| --- | --- |
| Latest Swiss release | `v0.6r2119`, "Swiss v0.6r2119", published `2026-09-25T18:47:48Z` |
| Release tag commit | `163f8b1de75328ad3a9a79caabff924930c04213` |
| Upstream `master` HEAD observed | `96e2143307e2a02eebd4249dc7e3e50bb37af287` (2026-11-18, `v0.6r2119-54-g96e21433`). HEAD moved on audit day: re-run `git ls-remote` at intake and pin whatever it returns then. |
| Indigo pin today | `4c9e30aefb367fc70e3fa55e98003e7bb2955358` (`$INDIGO/UPSTREAM:14`) = release + 48 commits; dispositions in `$INDIGO/docs/dev/upstream-intake-2026-11-14.md:3-29` |

So Indigo is **already beyond the latest Swiss release** and exactly **6
commits behind upstream HEAD**. The six, each diff opened in the audit and
re-listed from the fetched clone for this design:

```
d817e8d94427eb7c5d75ff409c72de347f4fbaaf  Update Redump database.
a647fd74880f2cb31c8edbb9b9ba6a9d6e26a2f5  Fail in very large directories more gracefully.
8bdd31f869e39b3a9b55e7d5d37df579060e3d92  Add sanity checks for RTC update.
ed6ab9fda34f96a3f952fba21420b0957ffcdec7  Fix time adjustment when setting a time zone.
11a44a417e4e083a7fe079685904b5322f18fed2  Implement system date/time options.
96e2143307e2a02eebd4249dc7e3e50bb37af287  Use transmit timestamp instead of receive timestamp.
```

### 1.2 Ordered integration list

Integration of Swiss changes is a **separate follow-up** (one dedicated
pin-move pull request into `beta`, per `$INDIGO/AGENTS.md:119-131` and
`buildtools/upstream_merge.sh`). It is sequenced **before the updater ships**
for the MUST item, but it is not mixed into the updater pull request.

**Now — in the pin-move PR, before the updater's first release:**

1. **`a647fd74` MUST.** `reallocarray` → `reallocarr` at every directory/FST
   growth site (12 files). Today a failed growth assigns NULL and the next
   `memset` crashes — visible in the tree at
   `$INDIGO/cube/swiss/source/devices/smb/deviceHandler-SMB.c:118` (growth),
   `:128` (`memset`), `:136` (shrink), and the same pattern in
   `devices/fat/deviceHandler-FAT.c:126` and `gcm.c:848`. The updater itself
   never calls `readDir` (§4.3), so this is *not* a code prerequisite for the
   updater; it is a ship blocker anyway, because shipping an SMB update path
   actively directs users to browse network shares with a known
   hostile-directory crash in exactly that path. Conflict risk **MEDIUM**:
   LOW for the nine upstream-identical handlers; hand-merge for the three
   `UPSTREAM`-listed divergent files `gcm.c` (Indigo FST bounds), `deviceHandler-CARD.c`,
   and `deviceHandler-SMB.c` (Indigo keeps TinySMB/POSIX `readdir`, upstream's
   context is libsmb2 — apply the pattern, not the patch). Two intake-time
   checks from the audit, still open: confirm `reallocarr` is declared by
   pinned libogc2 r2500 and Qoob's labelled `break loop;` compiles there
   (rewrite as flag/goto if rejected); add a result guard at upstream's
   DVD/WODE single-append sites, which ignore `reallocarr`'s return before
   writing the new slot.
2. **`d817e8d9` SHOULD.** Redump database refresh, `nkit.c` only; Indigo is
   at the pre-change values (`nkit.c:193`, `:2295`). Conflict risk **LOW**
   (file is `UPSTREAM`-listed only for Latin-1 `\x` escapes; new lines are
   ASCII). Rides the same pin-move PR.

**Prerequisite for the updater itself — the one exception:**

3. **The `strcpy` → `strncpy(src, text, size)` hunk of `11a44a41`.** Indigo's
   text-entry accept path still copies unbounded at
   `$INDIGO/cube/swiss/source/gui/FrameBufferMagic.c:8704-8706`, and the
   updater adds a new free-text row (Update Server, §6) that goes through
   `DrawGetTextEntry` (`FrameBufferMagic.h:353`, which already receives the
   destination size). Land this single hunk as the first commit of the
   updater series (or in the pin-move PR): `gui/` is Indigo-owned
   (`$INDIGO/buildtools/ci/check_upstream.py:20-24`), so no `UPSTREAM`
   bookkeeping applies. Do **not** half-port the coupled mode-condition and
   Rename-caller changes from the same upstream commit.

**Later (only if a need is proven):**

4. **libsmb2 replacing TinySMB** — already declined at pin time (upstream
   `2ae78094`; `docs/dev/upstream-intake-2026-11-14.md:20`, `UPSTREAM:24`).
   The SMB transport in §5 therefore requires an **SMB 1.0/CIFS-enabled
   server** (dialect string at `deviceHandler-SMB.c:296`). Revisit only if
   SMB1 refusal in the field proves to be the updater's dominant failure;
   it is a listed-file change with its own size and credential-surface cost,
   and the credential-env companion commit `63932963` stays declined
   regardless (credentials must never enter a launched program's
   environment).

**Never (as upstream intakes):**

5. **Clock cluster `8bdd31f8`, `ed6ab9fd`, `96e21433`** — SKIP at runtime.
   They touch `updateRTC`/`updateTZ`, which Indigo removed
   (`config/sram.c` carries neither; `sntp.c` is retained uncompiled,
   `$INDIGO/cube/swiss/Makefile:117`), and Indigo's live client already does
   the `96e21433` fix with stronger validation
   (`gui/ui_time_sync.c:45` reads the transmit timestamp at offset 40;
   `:55` rejects pre-epoch replies). A pin move updates dead `sntp.c` for
   provenance only. Conflict risk LOW as skipped, HIGH if the RTC clamp
   were imported (it was already declined once, at `245ea76f`).
6. **`11a44a41` stock date/time Settings UI** — SKIP verbatim (conflict risk
   HIGH): Indigo has its own Settings views, and the dummy rows this commit
   implements were already declined (`upstream-intake-2026-11-14.md:29`).
   Manual date/time setting, if ever wanted, is an Indigo product decision
   in Indigo's own views, not an intake. (The separable `strncpy` hunk is
   item 3 above.)

### 1.3 Answer to "what must land before the updater can be written"

Nothing in the six blocks writing the updater: it lives entirely in owned
paths (§9) and calls only public, already-linked machinery. Item 3 must
land with it; item 1 must land before it ships; the rest follow the normal
pin-move cadence. No build/dependency commits exist in the six (no Makefile
or toolchain delta), and the only SMB delta is item 1's hunk — no protocol,
credential, or libsmb2 change is pending upstream.

## 2. Platform facts the design is built on (each body opened)

- **No TLS, no HTTP client, no DNS in the DOL.** The only HTTP code is
  servers (`$INDIGO/cube/swiss/source/httpd.c:39-41,57-78` on :80; wiiload
  :4299; lab :4300). Link line carries no TLS or crypto library
  (`$INDIGO/cube/swiss/Makefile:74,88`). Every network setting is an IPv4
  literal in a `char[16]` (`$INDIGO/cube/swiss/include/swiss.h:196-214`);
  FTP's resolver has `net_gethostbyname` commented out and falls back to
  `inet_addr` (`devices/ftp/ftp_devoptab.c:455-483`); user docs state it
  plainly (`docs/SETTINGS.md:199`, "the console has no DNS"). **Therefore:
  the "release-channel base URL" is, on this platform, an IPv4 address with
  optional port and path over plain HTTP.** A hostname or `https://` value
  is rejected with an explanation (§5.1). Consequence, stated plainly as
  required: with no TLS and no signing infrastructure in existence,
  transport is untrusted; **SHA-256 verification against the manifest plus
  explicit user confirmation showing the hash are mandatory**, and this
  detects corruption and casual substitution, not a determined attacker
  who controls both manifest and file. See §7.3 and open decision D1 (§13).
- **The DOL does not know its version.** Only `GIT_COMMIT` (7 chars),
  `GIT_REVISION` (commit count), `GIT_TIMESTAMP` (HEAD committer epoch,
  `%ct`) are injected (`$INDIGO/cube/swiss/Makefile:74`) and displayed
  (`gui/info.c:406-407,550-569`). The manifest must therefore carry the
  release **commit and build time**; "newer" is decided from those, never
  from `GIT_REVISION` (not comparable across beta/main histories). Version
  is the git tag only (`docs/RELEASING.md:8-12,83-88`).
- **No cryptographic hash exists in-tree.** Only XXH3/XXH64 and zlib CRC32
  (integrity, not authenticity). SHA-256 must be vendored (§9, file 2),
  after first checking whether the pinned SDK image already ships one (§13,
  one `docker run … ls` — not inspectable from the repo alone).
- **Releases today** are tag-driven: `v*` → `.github/workflows/release.yml`
  builds `Indigo-<tag>.zip` + `SHA256SUMS.txt`, attests provenance, and
  attaches only those two files (`release.yml:123-132,186-199`).
  `dist/provenance.json` is built but not attached. There is no update
  manifest, no per-file DOL hash on the Release, and no GPG/minisign
  anywhere in the release path. The zip's SD layout is exact-checked by
  `buildtools/ci/check_package.py` — the updater work must not change the
  zip.
- **A manual update replaces two files**: the boot DOL (named `ipl.dol`,
  `boot.dol`, or `apps/indigo.dol` depending on the loader) and
  `swiss/patches/apploader.img` (`sd_package.sh:3-15,84-118`). The
  apploader image is generated per release from `cube/packer/reboot.dol`
  via `buildtools/dol2ipl.py` and stamped `*indigo-<version>`. The updater
  must move both, or IGR-to-Indigo silently mixes releases.
- **Which file the console booted from is not recorded anywhere.**
  `main()` receives `argv` but stores no self path
  (`$INDIGO/cube/swiss/source/main.c:209,350`). Indigo deliberately never
  runs upstream's `load_auto_dol` (`main.c:306-308`). The updater resolves
  its target by probing the Configuration Device (§6.3) and showing the
  resolved path for confirmation.
- **Reusable machinery** (no new protocol or filesystem code):
  `init_network()` (`bba.c:113-119`) joins the net thread
  (`bba.c:31-104`, `if_configex` DHCP/static) and returns false on failure;
  `__device_smb` init over TinySMB (`deviceHandler-SMB.c:71-86,193-215`)
  with pure-POSIX vtable reads (`:144-176`); the device vtable itself
  (`devices/deviceHandler.h:189-220`) with `file_handle.size` as `u32`
  (`:56-73`) — ample for a ~4.5 MiB DOL; `config_set_device()` /
  `config_unset_device()` (`config/config.c:89-123`) to mount
  `DEVICE_CONFIG`; the write-new-then-swap precedent
  `config_file_write()` (`config.c:199-235`, "FatFs will not rename onto a
  file that exists", `:225`); the 256 KiB chunk-copy loop with one read
  retry, progress and B-cancel (`swiss.c:6494-6581`) and partial-drop
  (`swiss.c:6088-6104`); whole-image structural validation
  `wiiload_image_valid()` (`wiiload.c:238-289`, declared `swiss.h:83`) and
  the whole-file RAM read pattern of `boot_dol` (`swiss.c:5837-5880`,
  ≤16 MiB `memalign`); bounded socket reads (`wiiload.c:93-109`, 10 s idle
  `net_select`; `gui/time_sync.c:28-70`, 1 s × 3); the only outbound TCP
  client template (`rt4k.c:77-123`); restart via
  `SYS_ResetSystem(SYS_HOTRESET, 0, FALSE)` (`wiiload.c:366`).
- **DOL size budget is a tripwire with ~5.7 KiB of headroom.**
  `MAX_DOL_BYTES = 17 MiB // 4` = 4,456,448 B
  (`$INDIGO/buildtools/ci/verify_dol.py:29-32`, "A tripwire, not a hardware
  limit … Growth past this remains an explicit decision"). The v2.9.0
  release DOL was 4,450,752 B (release handoff receipt; re-measure current
  `beta` as implementation step 0). §11 makes the budget decision a gate.
- **House rules that shape this feature**: docs move with code in the same
  PR (`AGENTS.md:112-118`); every untrusted file format gets a fuzzer
  (`AGENTS.md:153-157`); pure logic lives in host-testable `ui_*.c` modules
  with no console calls (`gui/ui_time_sync.h:8-10` is the model); nothing
  blocking in the draw path (`AGENTS.md:133-135`); heap, not stack, for big
  buffers (CI enforces `-fstack-usage` ceilings via
  `buildtools/ci/check_stack_usage.py`); no private hostnames/IPs in the
  public repo (`AGENTS.md:165-168`) — so the shipped default server is
  empty until a public endpoint exists (§5.1, D2).

## 3. Manifest format

One small `Key=Value` text file per channel — the format the codebase
already parses everywhere (`config_parse_global`, `config.c:1286-1923`) —
not JSON (no JSON parser exists in the DOL, and none is worth its bytes).

- Files: `stable.txt` and `rc.txt` at the channel base (§5) or under
  `indigo-updates/` on the SMB share (§5.3). Package files sit beside the
  manifest. Maximum manifest size **4096 bytes**; both transports enforce
  it before parsing.
- Lines are `Key=Value`, LF or CRLF. Keys are case-sensitive ASCII; unknown
  keys are ignored (forward compatibility); a duplicate key, a line over
  512 bytes, a NUL byte, or a missing required key makes the whole
  manifest invalid. Values are never executed, never used as paths without
  the validation below, and `NotesURL` is display text only.

| Key | Req | Meaning / validation |
| --- | --- | --- |
| `FormatVersion` | yes | Exactly `1`. Anything else → "needs a newer Indigo" error, not a parse error. |
| `Channel` | yes | `stable` or `rc`; must equal the channel that was fetched. |
| `Version` | yes | Display string, e.g. `v2.10.0` / `v2.10.0-rc.1`. Validated `vN.N.N…` shape; never compared numerically on-console (the running build has no version to compare against). |
| `Commit` | yes | Full 40-hex release commit. Console compares its first 7 chars with `GIT_COMMIT`. |
| `Date` | yes | `YYYY-MM-DD`, display only. |
| `BuildTime` | yes | Committer epoch seconds of the release commit (same clock as `GIT_TIMESTAMP`, `git log -1 --format=%ct`). Drives newer/older (§4.2). |
| `File` | yes | Bare DOL **basename** only, e.g. `indigo-v2.10.0.dol`. Charset `[A-Za-z0-9._-]`, ≤64 chars, no `/`, no leading `.`. |
| `Size` | yes | Decimal byte count of `File`; must be >0 and ≤16 MiB (the `boot_dol`/`wiiload_image_valid` ceiling). |
| `SHA256` | yes | 64 lowercase hex of `File`. |
| `ApploaderFile` / `ApploaderSize` / `ApploaderSHA256` | trio | All three or none. Same validation; size ≤1 MiB. Present in every generated manifest (§8); optional in the parser only so a hand-made DOL-only share still works, with an explicit warning in the confirm box. |
| `MinVersion` | no | Display text for the refusal message, e.g. `v2.8.0`. |
| `MinBuildTime` | no | Epoch seconds. If `GIT_TIMESTAMP < MinBuildTime`, refuse: "install `MinVersion` first" (stepping-stone releases). This is the only enforceable minimum, because build time is what the running DOL actually knows. |
| `NotesURL` | no | Release-notes location, shown as text for the user's computer. Never fetched (it will be an `https://` page the console cannot open). |

Example (illustrative values only — no live host is asserted anywhere in
this design):

```
FormatVersion=1
Channel=stable
Version=v2.10.0
Commit=0123456789abcdef0123456789abcdef01234567
Date=2026-12-01
BuildTime=1796000000
File=indigo-v2.10.0.dol
Size=4500000
SHA256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
ApploaderFile=apploader-v2.10.0.img
ApploaderSize=123456
ApploaderSHA256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
NotesURL=https://github.com/spencernorton/indigo/releases/tag/v2.10.0
```

Channel mapping follows `docs/RELEASING.md:8-12`: `stable.txt` is generated
only from `vX.Y.Z` tags on `main`; `rc.txt` from the newest
`vX.Y.Z-rc.N` (or `-beta.N`) tag on `beta`. The console offers exactly two
channel values, **Stable** and **Release candidate** — no Beta value, so no
console is ever silently enrolled in betas (§6.1).

## 4. The one shared verify+apply pipeline

### 4.1 Shape

Two new modules in `cube/swiss/source/gui/` (owned path; picked up by the
existing `SOURCES` wildcard, `$INDIGO/cube/swiss/Makefile:20-50,110-117` —
no Makefile edit):

- **`ui_update.c/.h` — pure core, no console calls** (the
  `ui_time_sync.c/.h` pattern): manifest parser, newer/older decision,
  channel-base parser, HTTP response-head parser, hex/SHA helpers. Host
  tests and the fuzzer compile this file alone.
- **`update.c/.h` — target glue**: the pipeline below, both transports,
  staging/verify/swap, and every message/progress box. It runs
  **synchronously inside the Settings toggle arm**, exactly like other
  Settings child loops (`settingsConfirmReset`, `settings.c:2444-2471`) and
  the file-copy core (`swiss.c:6494-6581`) — not on a thread and never in
  the draw path. One press owns the devices until it returns; progress is
  drawn with `DrawPublish`/`DrawUpdateProgressBarDetail`; B cancels between
  chunks. Threads (the `time_sync.c` pattern) are rejected here: the copy
  core proves the synchronous pattern for multi-megabyte transfers, and
  single ownership makes cancel/cleanup races impossible.

Internal transport interface (in `update.c`, two implementations in §5):

```c
typedef struct update_source update_source_t;
/* Fetch the channel manifest into buf (cap 4096), NUL-terminate, set *len. */
update_err_t (*fetch_manifest)(update_source_t *, char *buf, size_t cap, size_t *len);
/* Stream `name` (manifest-validated basename) into the already-open staging
 * handle on DEVICE_CONFIG, updating sha/size; progress + B-cancel inside. */
update_err_t (*fetch_file)(update_source_t *, const char *name,
                           file_handle *stage, u32 expected_size,
                           sha256_ctx *sha, uiDrawObj_t *progress);
```

Everything after bytes land in the staging file is transport-independent.

### 4.2 Steps, in order, with every failure exit

1. **Preconditions (no network yet).** Adapter present (else the rows were
   hidden anyway, §6.2; re-checked here because Settings' snapshot can be
   stale). `config_set_device()` succeeds and `devices[DEVICE_CONFIG]`
   has `FEAT_WRITE|FEAT_CONFIG_DEVICE` with non-NULL
   `statFile/readFile/writeFile/closeFile/deleteFile/renameFile`
   (`deviceHandler.h:116-126,189-220`) — else `NO_CONFIG_DEVICE`. Boot
   target resolves (§6.3) — else `TARGET_NOT_FOUND` / `TARGET_AMBIGUOUS`.
   Stale `<target>.new` / `apploader.img.new` from an interrupted run are
   deleted. Free space via `info()->freeSpace` (`deviceHandler.h:75-79`)
   is recorded; if known and below the manifest sizes (step 3), fail
   `NO_SPACE` before downloading a byte. (The backup is a rename, so no
   second copy's space is needed.) Known means what the FAT handler
   reports: `freeSpace` 0 with a nonzero total is a **full** card and
   refuses; only free and total both 0 (or no `info`) is unknown.
2. **Network up.** `init_network()` (`bba.c:113-119`). False →
   `NO_NETWORK`, one message covering no-adapter/no-DHCP/no-link (the SDK
   does not distinguish them). Note this call also waits for Sync Clock
   (bounded 3 s) — inherited behaviour, not new latency of ours. There is
   no cancel during this join; the net thread is what it is, and the box
   says "Starting network…".
3. **Manifest.** Transport `fetch_manifest` → `UIUpdate_ParseManifest`
   (§3 rules). Errors: transport errors (§5), `MANIFEST_TOO_BIG`,
   `MANIFEST_INVALID`, `FORMAT_UNSUPPORTED`, `CHANNEL_MISMATCH`.
4. **Decide** (`UIUpdate_Decide`, pure, table-tested):
   - `MinBuildTime` present and `GIT_TIMESTAMP < MinBuildTime` → refuse
     `MIN_BUILD` ("Install vX.Y.Z first", from `MinVersion`).
   - `Commit[0:7] == GIT_COMMIT` → **up-to-date**, stop (no download).
   - `BuildTime > GIT_TIMESTAMP` → **update available**.
   - otherwise (different commit, not newer) → **older/different build**:
     still offered, but the confirm box says "older than this build"
     in so many words. Never silently treat "different" as "newer".
     A build made without git carries `GIT_TIMESTAMP` 0 (the Makefile's
     numeric fallback, so a tarball build compiles): unknown is never
     "newer" — it lands here, warned — and it cannot prove a
     `MinBuildTime` floor, so a stepping stone still goes on from a
     computer first.
5. **Confirm #1 — download.** Shows: new `Version` + `Date`, commit (7),
   running commit (7), total size (DOL + apploader, MiB), source
   (`http://…` base or `smb:/indigo-updates/`), and the resolved absolute
   target (e.g. `sda:/ipl.dol`). A = download, B = stop. Nothing has been
   written yet except stale-`.new` cleanup.
6. **Stage.** For the DOL, then the apploader (if in manifest): open
   `<target>.new` / `<config>:/swiss/patches/apploader.img.new` on
   `DEVICE_CONFIG` via its `writeFile` (creating `swiss/patches` with
   `ensure_path`, `files.c:211-234`), stream 256 KiB heap chunks
   (`memalign(32, …)`, copy-core pattern), SHA-256 incrementally, exact
   byte-count enforcement, one read retry per chunk on the source side,
   progress with speed/ETA, B = cancel. Finish with the copy core's
   flush idiom (`writeFile(h, NULL, 0)` then `closeFile`, both checked,
   `swiss.c:6570-6580`). Any failure or cancel → delete the `.new`
   (partial-drop pattern, `swiss.c:6088-6104`), error, stop. The running
   install is untouched through this whole step.
7. **Verify (what landed, not what was sent).** Streamed SHA-256 must equal
   the manifest hash. Then the staged DOL is read back whole into RAM
   (the `boot_dol` read pattern, `swiss.c:5849-5872`): SHA-256 over the
   RAM copy must match again (this is the read-back proof against SD
   write corruption) **and** `wiiload_image_valid(buf, size, false)` must
   pass (every named section inside the file, entry point sane —
   `wiiload.c:238-289`). Apploader: size + both hashes (it is not a DOL;
   no structural validator exists for it). Failures → `HASH_MISMATCH` /
   `IMAGE_INVALID`, `.new` files deleted, stop.
8. **Confirm #2 — install.** Mandatory because there is no TLS: shows
   that the download matches its update list, with the first 16 hex of
   the DOL's SHA-256 (the same truncation the website download button
   shows) and the target path, inviting a cross-check against the
   release page on a computer. Manifest and file travel the same
   channel, so this proves integrity against corruption and casual
   substitution — not who published them (§2, §7.3); the screen's
   wording says "matches its update list", never "verified" alone.
   A = install, B = stop (`.new` deleted). This is the last cancel point.
9. **Apply (no cancel, "Installing… do not switch off").** Order is
   apploader first, boot DOL last, so the single point of no return is
   the final rename:
   for each file: delete old `<file>.bak` if present → rename current
   `<file>` to `<file>.bak` (absent current apploader is fine; absent
   boot DOL cannot happen — step 1 resolved it) → rename `<file>.new`
   to `<file>` (delete-then-rename semantics per `config.c:222-228`).
   - Rename-to-`.bak` fails → nothing changed for that file; delete
     `.new`, `SWAP_FAIL`, stop (if the apploader already swapped, roll
     it back from its `.bak` first).
   - Final rename fails → **immediate rollback**: rename `.bak` back.
     Rollback succeeds → `SWAP_FAIL` ("nothing was changed" — said only
     after re-probing that the boot file is in place; if it is somehow
     absent the box names the missing file and its `.bak` instead).
     Rollback also fails → `SWAP_FAIL_MANUAL`, a box that stays on
     screen naming the file that failed and which of its `.bak`/`.new`
     are provably still on the card ("rename `ipl.dol.bak` to `ipl.dol`
     on a computer"). Rollback is attempted, never guaranteed: a card
     that refuses the second rename may refuse the third.
   - Implementation caution (found after this design was written): the
     FAT/KunaiGC/FlippyDrive `renameFile`s copy the new name into the
     source handle on success. The swap therefore snapshots all three
     names before any rename, refills each handle from the snapshot
     before every call, and never re-reads a renamed handle's name —
     reading `file.name` after the first rename yields the `.bak` name,
     the second rename then fails onto itself, and the naive rollback
     renames `.bak` to itself. The sequence lives in the pure core
     (`UIUpdate_Swap`) and is host-tested against a fake that mutates
     handles exactly this way.
   - Power loss between the two renames leaves the boot file missing
     for a window of milliseconds: the loader's own fallback applies,
     and recovery is the same manual `.bak` rename. This window is
     inherent to FatFs "no rename onto existing" and is documented in
     the guide, not hidden.
10. **Done.** `config_unset_device()` if step 1 mounted a non-current
    device. Box: "Indigo vX.Y.Z installed. A Restart now · B Later".
    Restart = `SYS_ResetSystem(SYS_HOTRESET, 0, FALSE)` so the new build
    is proven through the real IPL/loader path. Chainloading with
    `boot_dol` is deliberately **not** offered: it would bypass exactly
    the boot path that was just replaced. `.bak` files are kept until
    the next successful update replaces them; the guide documents the
    manual restore drill.

### 4.3 Things the pipeline pointedly never does

- Never calls any device's `readDir` (fixed paths + `statFile` only) — so
  the `a647fd74` growth bug cannot be triggered by the updater itself.
- Never writes anywhere except: the two `.new` staging files, and the
  renames of §4.2.9, all on `DEVICE_CONFIG`. Never touches
  `devices[DEVICE_CUR]`, `curFile`, or the browser state; never reuses
  `manage_file_ex` (global `curFile`, its own UI and confirmations).
- Never deinitialises SMB or the network (browsing may own them; leaving
  `smb_initialized` set matches existing browse behaviour).
- Never retries silently in the background, never runs at boot, never
  checks on a timer. `bba.c` and `Init network at startup` are untouched.

## 5. The two transports

### 5.1 The channel base (HTTP) — grammar and defaults

One persisted string, Update Server (§6.1), parsed by pure
`UIUpdate_ParseBase`:

```
base     := ["http://"] ipv4 [":" port] ["/" path]
ipv4     := dotted quad, validated with inet_aton semantics (pure parser mirrors it)
port     := 1..65535, default 80
path     := segments of [A-Za-z0-9._~-], no "..", no spaces, no "?" or "#"; trailing "/" optional
```

Fetches are `GET <base>/<channel>.txt` and `GET <base>/<File>`. Rejected
with specific messages, never a silent fallback: `https://` ("Indigo
cannot use HTTPS: there is no TLS in this program"), a non-IPv4 host
("Use an IPv4 address: the console has no DNS"), a bad port/path
(`BAD_SERVER`). Empty setting ⇒ compile-time `UPDATE_DEFAULT_SERVER` in
new `gui/update.h`, mirroring `TIME_SYNC_DEFAULT_SERVER`
(`gui/time_sync.h:8`) and its empty-means-default row display
(`settings.c:1174`). **It ships as `""`** — no public plain-HTTP endpoint
exists yet (§8, D2), and inventing one would strand consoles on a wrong
host forever. Empty setting + empty default ⇒ Check for updates reports
`NOT_CONFIGURED` and points at the Update Server row and the guide. The
on-screen keyboard can type the whole grammar:
`ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA` lower case has `.` and `/`, upper case
has `:` (`FrameBufferMagic.c:8466-8467`); the tooltip says where the colon
lives. Stored plain in `global.ini` like every other network field (it is
not a secret).

### 5.2 HTTP fetch (new, ~150 lines in `update.c`)

Modelled on `rt4k_init`'s connect (`rt4k.c:98-123`) and wiiload's bounded
reads (`wiiload.c:93-109`); the response-head parser lives in the pure
core so it is host-tested.

1. `net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP)` → `net_connect` to
   `htons(port)` (per `time_sync.c:33-38`). `net_connect` is blocking with
   the lwIP default timeout — its exact bound is an SDK property not
   inspectable from the repo (D5); the box shows "Connecting…" and B
   cannot interrupt this one call. Stated, not papered over.
2. Send `GET <path> HTTP/1.0\r\nHost: <ipv4[:port]>\r\nAccept: */*\r\n\r\n`
   (HTTP/1.0 requests close-delimited simplest-server behaviour).
3. Read the head (≤4096 B) with `net_select` **10 s idle** timeouts,
   B-cancel polled between reads, and a 60 s overall deadline for the
   head (likewise the manifest body): idle patience alone lets a
   dribbling server wait out the user forever.
   Parse (`UIUpdate_ParseHttpHead`, pure): status must be `200`;
   `Content-Length` is **required** and must equal the expected size
   (manifest: ≤4096; files: exactly the manifest `Size`);
   `Transfer-Encoding: chunked`, any `3xx`, or a missing length →
   `HTTP_UNSUPPORTED` naming the cause (v1 targets static file servers
   and LAN mirrors; redirect/TLS hosts such as GitHub Releases are out
   of reach of this platform, §2). Other statuses → `HTTP_STATUS`
   showing the code.
4. Stream the body in 256 KiB chunks straight into the staging writer,
   10 s idle timeout per read, pad poll per chunk for B-cancel, SHA-256
   incremental, and a generous 600 s whole-file deadline (a slow LAN
   server is legitimate; an endless dribble is not). Bytes that arrived
   with the head are clamped to the remaining Content-Length, as the
   manifest read clamps its carry. Short/over-long body →
   `SIZE_MISMATCH`. The SMB fetch checks the same deadline between
   chunks — all it can promise, since one SMB read blocks in the device.
5. `net_close` on every path. One connection per file (manifest, DOL,
   apploader = up to 3); no keep-alive, no pipelining.

### 5.3 SMB fetch (no new protocol code)

Reuses the browsing configuration as-is — `SMBHostIP`, `SMBShareName`,
`SMBUserName`, `SMBPassword` (`swiss.h:196-199`; ini `config.c:617-620`
write, `:1801-1812` read):

1. Pre-check: share and host non-empty, else `NOT_CONFIGURED` ("Set SMB
   Host IP and SMB Share above first") — the same condition
   `deviceHandler_SMB_init` reports as `E_CHECKCONFIG`
   (`deviceHandler-SMB.c:193-198`).
2. `__device_smb.init(__device_smb.initial)` — which itself calls
   `init_network()` then `smbInit(user, password, share, ip)`
   (`deviceHandler-SMB.c:71-86,193-215`). Map `file->status`:
   `E_NONET` → `NO_NETWORK`; `E_CONNECTFAIL` → `SMB_REFUSED` with the
   SMB 1.0/CIFS note ("the server must allow SMB1"; `deviceHandler-SMB.c:296`).
   The handler's own "Reading filesystem info" box (`:59-68`) appears as
   it does when browsing.
3. Fixed locations, built with `concat_path` (`files.c:149-173`):
   `smb:/indigo-updates/<channel>.txt`, then
   `smb:/indigo-updates/<File>` / `<ApploaderFile>`. `statFile` first
   (size pre-check against the caps, `deviceHandler-SMB.c:144-153`),
   then `seekFile`/`readFile` 256 KiB chunks with one retry per chunk
   into the same staging writer as HTTP. Missing folder/file →
   `SMB_NOT_FOUND` naming the full expected path (the fix is on the
   server, so the message spells it out).
4. `closeFile` on every handle; never `deinit` (§4.3).

FTP would work identically through `__device_ftp` but is **not** built:
two transports are the requirement, and every extra one is another
error surface to test on hardware.

## 6. Settings and config additions

Precedent copied exactly: **Sync Clock** — three tail rows in Setup ›
Network (`settings.c:690-696`), three struct fields (`swiss.h:213-215`),
three ini keys (`config.c:634-636` write, `:1852-1859` read), tooltips
(`settings.c:192-194`), docs rows (`docs/SETTINGS.md:197-201`), a pure
`ui_*` core and a header `#define` default. The updater adds the same
shape, five rows at the Network tail:

| On screen | Kind | `global.ini` key | Struct field (`swiss.h`, after `timeZone:215`) | Default |
| --- | --- | --- | --- | --- |
| `Update Channel:` | Cycle `Stable` / `Release candidate` | `UpdateChannel` (value string, the `RecentListLevel` pattern, `config.c:640,1870-1877`) | `int updateChannel` (0=Stable) | `Stable` (memset 0; no `main.c` change, house pattern) |
| `Update Server:` | Text (`ENTRYMODE_NUMERIC\|ENTRYMODE_ALPHA`) | `UpdateServer` | `char updateServer[64]` | empty ⇒ `UPDATE_DEFAULT_SERVER`; row shows the default when empty (`settings.c:1174` pattern), or "Not set" while the default is `""` |
| `Update Boot File:` | Cycle `Auto` / `ipl.dol` / `boot.dol` / `apps/indigo.dol` | `UpdateBootFile` (value string) | `int updateBootFile` (0=Auto) | `Auto` |
| `Check for updates` | Action → HTTP transport (§5.2) | — (actions are not settings) | — | — |
| `Update from SMB share` | Action → SMB transport (§5.3) | — | — | — |

### 6.1 Wiring checklist (every site; missing one fails a gate in §10)

1. `gui/settings.h`: append `SET_UPDATE_CHANNEL, SET_UPDATE_SERVER,
   SET_UPDATE_BOOTFILE, SET_UPDATE_CHECK, SET_UPDATE_SMB` to
   `enum SETTINGS_NETWORK` **after `SET_RT4K_PORT`, before
   `SET_PAGE_3_BACK`** (`settings.h:112-141`) so no existing option
   renumbers. Declare `updateChannelStr[]` / `updateBootFileStr[]` beside
   the other `*Str[]` externs.
2. `include/swiss.h`: the three fields above, with the defaults stated in
   comments (house style, `swiss.h:213-217`).
3. `gui/settings.c`:
   - value tables with count asserts (pattern `:79-108`);
   - tooltip entries in `tooltips_network[]` (`:185-209`), plain words:
     Channel ("Stable is the latest finished release…"), Server ("An IPv4
     address… Names don't work: the console has no DNS. `:` is on the
     UPPERCASE keyboard."), Boot File ("Auto looks for ipl.dol, boot.dol,
     then apps/indigo.dol on the Configuration Device"), both actions
     (what runs, "nothing is downloaded until you say so");
   - row refs appended to `networkRows[]` **after `SET_TIME_ZONE`**
     (`:690-696`); rename `SETTINGS_SYNC_CLOCK_ROWS` →
     `SETTINGS_NETWORK_TAIL_ROWS` with value 3+5=8 so every
     adapter-dependent row hides together (§6.2); update its one use at
     `:2931`;
   - `settingsDescribeRow` PAGE_NETWORK arms (`:1165-1192`): Channel →
     `rowCycle`, Server → `rowText` (default-or-"Not set" when empty),
     Boot File → `rowCycle`, both actions → `rowAction(..., netEnable)`;
   - `settings_toggle` PAGE_NETWORK arms (`:1866-1941`): cycles step with
     modulo; Server opens `DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA,
     "Update Server", …, sizeof-1)`; actions run **only on
     `direction == 0`** (A), no-op on Left/Right (Reset precedent), calling
     `updateRun(UPDATE_SOURCE_HTTP)` / `updateRun(UPDATE_SOURCE_SMB)`;
   - `settingsRowIsAction()` (`:2415-2434`): PAGE_NETWORK gains
     `|| option == SET_UPDATE_SERVER || in_range(option, SET_UPDATE_CHECK,
     SET_UPDATE_SMB)` (text row included, matching existing semantics);
   - `settingsInputMayBlock()` (`:2862-2897`): PAGE_NETWORK gains the
     Server row and both actions, so menu input is re-initialised and the
     dismissing press consumed after the child loop returns;
   - focus hint: `settingsDescribeFocus` prints "A Reset" for every
     ACTION row (`:1329-1332`) — special-case the two new options to
     "A Check" / "A Update".
4. `gui/ui_settings_layout.h`: `UI_SETLAYOUT_ROWS_NETWORK` 23 → 28
   (`:41`); `settings.c:41` assert becomes `== SET_UPDATE_SMB + 1`.
   No geometry change (the 6-row window scrolls).
5. `config/config.c`: three `fprintf` lines in `config_update_global`
   after `TimeZone` (`:636`); three parse arms in `config_parse_global`
   after the `TimeZone` arm (`:1858-1860`), strings via `strlcpy`, cycles
   matched against the `*Str[]` tables (unknown value leaves the default,
   as other cycles do). The legacy `swiss.ini` importer (`:1157+`) is
   **not** touched — these keys have no legacy form. Side effect to
   document: the keys become settable via DOL arguments for free
   (`config_parse_args` shares the parser; `docs/SETTINGS.md:69-80`
   precedence).
6. Docs in the same PR: `docs/SETTINGS.md` Network table (§6 table above,
   format of `:188-218`), `docs/guide/settings.md` every-setting entries
   plus an "Updating Indigo over the network" walkthrough (server setup,
   `indigo-updates/` folder, SMB1 requirement, `.bak` restore drill),
   `docs/examples/global.ini` if it enumerates keys, `CHANGELOG.md`
   `## Unreleased`. Note in the PR which guide pictures are stale if they
   cannot be re-recorded (`AGENTS.md:112-118`).
7. Transactions: Settings edits persist only on Save & Exit
   (`settings.c:3067-3127`); the two actions use the **in-RAM** values,
   including unsaved edits — consistent with every other row, and stated
   in the guide. Update settings live in `global.ini`, never SRAM.

### 6.2 No adapter / no network / offline behaviour

- **No BBA-class adapter:** all five rows hide with the Sync Clock trio
  via `UISetLayout_HideTail` (§6.1.3) — the house graceful behaviour,
  snapshot at Settings open (`settingsNetAdapter`, `settings.c:698-699,2930`).
  Users without an adapter simply never see the feature. The runtime
  re-check in §4.2.1 covers an adapter removed mid-session.
- **Adapter present, network down / DHCP unanswered:** rows show dimmed
  (`netEnable`, `settings.c:1166-1191`) but remain usable — pressing an
  action starts the network on demand via `init_network()` (lazy start
  is how every network device already behaves). Failure → one
  `NO_NETWORK` box, no partial state, return to the row.
- **Offline mid-run:** every network read is bounded (HTTP 10 s idle;
  SMB reads surface the handler's own errors); every failure path
  deletes staging and leaves the install untouched (§4.2).
- Timeouts summary: net start = SDK join (unbounded by us, + ≤3 s Sync
  Clock wait); HTTP connect = lwIP default (D5); HTTP idle = 10 s;
  no total-download deadline (B-cancel instead); apply phase has no
  timeout because it has no network in it.

### 6.3 Boot-target resolution (glue, in `update.c`)

Candidates on `DEVICE_CONFIG`, by `statFile` only: Auto probes
`ipl.dol`, `boot.dol`, `apps/indigo.dol` (the three names
`sd_package.sh:96-118` documents). Exactly one present → target; none →
`TARGET_NOT_FOUND` ("No ipl.dol, boot.dol or apps/indigo.dol on the
Configuration Device — update from a computer, or set Update Boot
File"); more than one → `TARGET_AMBIGUOUS` naming the found files (never
guess which one the loader starts). An explicit Boot File row value
skips probing but must still `statFile`-exist. Known limitation, stated
in the guide: the boot file is assumed to live on the Configuration
Device (true for PicoBoot/SD, GC Loader, FlippyDrive and
apps-launched installs whose settings card is the boot card); an install
booting from a different device than it keeps settings on must update
from a computer in v1. A free-text path setting is rejected for v1
(no need proven; path entry + traversal validation is surface without a
user).

## 7. UI states and safety surface

### 7.1 States (all boxes `DrawMessageBox` `D_INFO/D_WARN/D_FAIL`,
`FrameBufferMagic.h:26-28`; confirm boxes follow `settingsConfirmReset`'s
release-then-A/B loop so the choosing press cannot self-confirm)

| State | Presentation |
| --- | --- |
| Checking | Progress box "Starting network…" → "Checking for updates…" (indeterminate; SMB also shows the handler's own filesystem-info box) |
| Up-to-date | D_INFO "Indigo is up to date. `<running commit7>`" · A |
| Available | D_INFO confirm #1 (§4.2.5): version, date, commits, total size, source, target · A Download / B Cancel |
| Older build available | Same, D_WARN, first line "This build is older than the one running." |
| Downloading | `DrawProgressBarDetail`: %, speed, elapsed, ETA, current filename · B Cancel |
| Verifying | Progress "Verifying…" % of read-back pass |
| Verified | D_WARN confirm #2 (§4.2.8): SHA-256 first 16 hex, target · A Install / B Cancel |
| Applying | Progress "Installing… do not switch off." (no buttons live) |
| Done | D_INFO "Installed Indigo vX.Y.Z." · A Restart now / B Later |
| Cancelled | D_INFO "Update cancelled. Nothing was changed." · A |

### 7.2 Errors (each D_FAIL, one cause, one next step; staging always cleaned)

`NOT_CONFIGURED` (server empty / SMB fields empty — names the row to fill) ·
`BAD_SERVER` (incl. the two explained rejections of §5.1) · `NO_NETWORK` ·
`CONNECT_FAIL` · `TIMEOUT` · `HTTP_STATUS <code>` · `HTTP_UNSUPPORTED`
(chunked / redirect / no length — "serve the files as plain static files") ·
`SMB_REFUSED` (SMB1 note) · `SMB_NOT_FOUND <path>` · `MANIFEST_TOO_BIG` ·
`MANIFEST_INVALID` · `FORMAT_UNSUPPORTED` ("this update needs a newer
Indigo; update from a computer once") · `CHANNEL_MISMATCH` · `MIN_BUILD`
("Install vX.Y.Z first") · `NO_CONFIG_DEVICE` · `TARGET_NOT_FOUND` ·
`TARGET_AMBIGUOUS <names>` · `NO_SPACE <have/need>` · `READ_FAIL` ·
`WRITE_FAIL` · `SIZE_MISMATCH` · `HASH_MISMATCH` ("the download does not
match its manifest — nothing was installed") · `IMAGE_INVALID` ("the
download is not a whole Indigo program") · `SWAP_FAIL` ("nothing was
changed") · `SWAP_FAIL_MANUAL <paths>` (persistent box, §4.2.9).

### 7.3 Honesty constraints (from the evidence, not negotiable in copy)

- No row label, box, or doc may claim downloads are "secure" or
  "verified genuine": with no TLS and no signing key in existence,
  verification proves the file matches the manifest it arrived with.
  Confirm #2 + the published per-file hash (§8) are the cross-check.
- No silent background auto-update, and no "check at startup" toggle in
  v1: adding one later touches `bba.c` (an `UPSTREAM`-listed file) and
  boot-time blocking rules — a separate design if ever wanted.
- Downgrades are labelled downgrades; a different commit is never
  presented as "newer" on commit-difference alone (§4.2.4).
- The SMB1 requirement and the plain-HTTP requirement are stated in the
  action tooltips and the guide, not discovered from failures.

## 8. Release-side (server) work — what must exist before a default ships

The console half is useless without files to fetch. Smallest creation,
reusing artifacts the release already builds:

1. **New `buildtools/update_manifest.py`** (buildtools is
   owned): inputs = tag, full commit SHA, committer epoch, date, and the
   built `dist/Indigo-<tag>.zip`; it extracts `ipl.dol` and
   `swiss/patches/apploader.img` from the zip with `zipfile`, hashes
   them, and writes into `dist/update/`: `indigo-<tag>.dol`,
   `apploader-<tag>.img`, and `<channel>.txt` per §3 (`NotesURL` built
   from the repo + tag pattern the release notes already use,
   `buildtools/release_notes.sh:48-63`). Channel from the same tag
   regexes as `release.yml:50-59`. Refuses to run for a non-tag input —
   manifests are generated only from published tags, never from CI
   artifacts of a branch.
2. **`.github/workflows/release.yml`** (owned): build job runs the
   generator after `sd_package.sh` + `check_package.py` (`:123-132`);
   `gh release create` (`:199`) additionally attaches the three update
   files (and `dist/provenance.json`, closing the known unpublished-
   provenance gap in the same line). The zip itself is unchanged, so
   `check_package.py` and the install story are untouched. Dry-run
   `workflow_dispatch` builds the manifest too, so it is reviewable
   before any tag.
3. **SMB users (works the day 1+2 land):** download the three update
   assets from the Release on a computer, copy them into
   `<share>/indigo-updates/` — documented in the guide. A PC-side sync
   helper is a follow-up, not part of this design.
4. **HTTP host (operator follow-up, blocks only the default value):**
   mirror `dist/update/` per channel to a plain-HTTP static path
   reachable by IPv4 literal. Where that lives (an `indigo.norvitech.com`
   origin whose source could not be found under `~/Kleos`, or the
   norvitech.com origins — neither verified for this purpose) is open
   decision D2. Until it exists, `UPDATE_DEFAULT_SERVER ""` ships and
   HTTP users type their own LAN mirror — the feature is fully usable
   that way, including for the test checklist in §12.
5. `docs/RELEASING.md`: one step added ("check the dry-run manifest
   before tagging; refresh the mirror after publishing", beside the
   existing site-refresh step `:74-75`).

## 9. Exact files, in implementation order

All console paths are Indigo-owned (`check_upstream.py:20-24` OWN covers
`gui/`, `config/`, `include/swiss.h`, `docs/`, `buildtools/`, `.github/`),
so **no `UPSTREAM` edit and no upstream-file edit** is needed anywhere in
this feature. New `.c` files under `gui/` need no Makefile change
(`cube/swiss/Makefile:20-50` SOURCES + `:110` wildcard). Sequencing:
**PR-0** = Swiss pin-move (§1.2 items 1-2, its own PR per house rules);
**PR-1** = console updater (files 1-12 below, one PR into `beta`, commits
in the order listed); **PR-2** = release side (files 13-15; can land
before PR-1 — assets without a consumer are harmless).

**Create:**

1. `cube/swiss/source/gui/sha256.h` + `sha256.c` — vendored single-file
   public-domain SHA-256 (incremental `init/update/final`), provenance
   and licence comment at top; licence text added under
   `docs/licenses/` and to `NOTICE` if the chosen source requires it.
   First run §13-D5's SDK check: if the pinned image already ships a
   linkable SHA-256, delete this file from the plan and link that
   instead (one-line `cube/swiss/Makefile` change; that file is already
   `UPSTREAM`-listed, reason updated in the same PR).
2. `cube/swiss/source/gui/ui_update.h` + `ui_update.c` — pure core
   (stdbool/stddef/stdint only, model `ui_time_sync.h:1-10`):
   - `typedef struct ui_update_manifest { … }` — fixed buffers
     (version 24, commit 41, date 11, file 65, sha256[32] binary +
     apploader trio, notes 128, flags for optionals).
   - `ui_update_err_t UIUpdate_ParseManifest(const char *text, size_t len, ui_update_manifest *out)` — §3 rules; distinguishes INVALID vs FORMAT_UNSUPPORTED vs CHANNEL handling by caller.
   - `ui_update_decision_t UIUpdate_Decide(const ui_update_manifest *, const char runningCommit7[8], int64_t runningBuildTime)` — §4.2.4 table.
   - `bool UIUpdate_ParseBase(const char *, ui_update_base_t *out, ui_update_base_err_t *why)` — §5.1 grammar; `why` distinguishes HTTPS / NON_IP / BAD for the three messages; outputs `in_addr`-ready dotted string + port + path (no `<arpa/inet.h>` in the pure file: dotted-quad validation is manual, octet bounds checked).
   - `bool UIUpdate_ParseHttpHead(const char *head, size_t len, int *status, uint32_t *contentLength, bool *chunked)` — case-insensitive header names, first `Content-Length` wins, duplicate length with different value = fail.
   - `bool UIUpdate_HexToBytes32(const char *, uint8_t[32])`, `void UIUpdate_Hex16(const uint8_t[32], char[17])` (confirm-#2 display), `UIUpdate_ChannelName(int)` → `"stable"/"rc"`, `UIUpdate_SafeBasename(const char *)`.
3. `cube/swiss/source/gui/update.h` — `#define UPDATE_DEFAULT_SERVER ""`
   (comment: why empty, D2), `enum update_source { UPDATE_SOURCE_HTTP,
   UPDATE_SOURCE_SMB }`, `void updateRun(enum update_source);`.
4. `cube/swiss/source/gui/update.c` — glue (the only file touching
   sockets/devices/UI):
   - `updateRun` — §4.2 state machine; owns cleanup on every exit.
   - `updateResolveTarget(file_handle *dol, file_handle *apploader)` — §6.3 + `swiss/patches/apploader.img` via `concat_path`.
   - `updateStageOpen/Write/Close` — `.new` writer over `DEVICE_CONFIG` (flush idiom of `swiss.c:6570-6580`); `updateDropStaging()` partial-drop.
   - `updateVerifyStaged(...)` — read-back RAM pass + `wiiload_image_valid` (§4.2.7).
   - `updateSwapOne(cur, new, bak)` + `updateRollbackOne(...)` — §4.2.9.
   - `updateConfirm(title, body)` — release-then-A/B loop (model `settings.c:2444-2471`); `updateTell(type, text)` box + `wait_press_A` (model `swiss.c:5844-5848`); `updateProgress(...)` throttled `DrawUpdateProgressBarDetail` + pad poll returning cancel.
   - HTTP: `httpFetchManifest/httpFetchFile` over `httpGet(base, leaf, sink)` using §5.2; sink = memory (manifest) or staging writer.
   - SMB: `smbFetchManifest/smbFetchFile` over `smbReadAll/smbStream` using §5.3.
   - Buffers: 256 KiB chunk + 4096 B manifest, both heap (`memalign(32,…)` for the chunk, copy-core pattern); staged-image RAM buffer `memalign(32, size + 0x400)` freed before apply. No stack arrays >256 B (`.su` ceilings).
5. `buildtools/ui/tests/test_ui_update.c` + Makefile lines
   (`TESTS += test_ui_update`, `test_ui_update.src := $(GUI)/ui_update.c
   $(GUI)/sha256.c`, pattern `buildtools/ui/tests/Makefile:21-33,80`) —
   cases in §10.
6. `buildtools/ui/tests/fuzz/fuzz_update.c` + seeds under
   `fuzz/corpus/update/` — feeds bytes to `UIUpdate_ParseManifest` and
   `UIUpdate_ParseHttpHead`; add its build line + target to
   `run_fuzz.sh` (pattern `:49`, time_sync) and valid/invalid seed
   manifests (including the §3 example).
7. `buildtools/update_manifest.py` — §8.1.
8. `buildtools/ui/tests/test_update_manifest.py` (or the repo's Python
   source-audit location used by sibling checks) — builds a fixture zip
   with the two members, asserts manifest fields/hashes/channel choice,
   asserts refusal on non-tag input.

**Modify (PR-1, in order):**

9. `cube/swiss/source/gui/FrameBufferMagic.c:8704-8706` — `strcpy` →
   bounded copy using the existing `size` parameter (§1.2 item 3).
   First commit; independently reviewable.
10. `cube/swiss/include/swiss.h` (after `:215`) — three fields (§6).
11. `cube/swiss/source/gui/settings.h` (enum after `SET_RT4K_PORT`,
    `*Str[]` externs); `cube/swiss/source/gui/settings.c` — every site in
    §6.1.3; `cube/swiss/source/gui/ui_settings_layout.h:41` — 23→28.
12. `cube/swiss/source/config/config.c` — write after `:636`, parse after
    `:1858-1860` (§6.1.5). `buildtools/ui/tests/test_settings_file.py` —
    its key sets are derived from the writer source (`:465`), so the new
    keys appear automatically; add the two cycles' values to its
    `VALUES`/kinds tables so every listed value round-trips
    (`:580-590` tests), and no other change. Then the docs of §6.1.6 and
    this design copied to `docs/dev/software-update-design.md`.

**Modify (PR-2):**

13. `.github/workflows/release.yml` — generator step after `:123-132`;
    assets on the `:199` create line (3 update files + provenance.json).
14. `docs/RELEASING.md` — §8.5 step.
15. `docs/guide/` server-setup page content rides PR-1's guide update;
    PR-2 adds the release-asset names to it once they exist.

**Explicitly not touched:** `bba.c`, `main.c`, `httpd.c`, `wiiload.c`,
`swiss.c`, device handlers, `sd_package.sh`, `check_package.py`, the zip
layout, `UPSTREAM`, CubeBrowser. (`swiss.c` needs no hook: Settings is
reached through existing wiring, and `updateRun` is called from
`settings.c`.)

## 10. Build and test steps

Host-side, from a clone **with tags** (`AGENTS.md:75`):

```bash
# Pure-core + settings gates (needs Python3+Pillow/NumPy, C compiler, zlib):
buildtools/ui/tests/run_tests.sh all
# Fuzzers incl. the new target (clang + libFuzzer):
buildtools/ui/tests/fuzz/run_fuzz.sh 30
# Source gates CI runs on a PR into beta:
buildtools/check_whitespace.sh origin/beta
python3 buildtools/ci/check_upstream.py
buildtools/ci/source_checks.sh
# The DOL, pinned image (writes cube/swiss/swiss.dol):
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  ghcr.io/extremscorner/libogc2@sha256:7c91503254afcf0acebc8edb6f81fc8535860c42ae31ca33dd1573464eb12565 make dev
# Package sanity (zip layout must be unchanged):
buildtools/sd_package.sh dev cube/swiss/swiss.dol .
```

`test_ui_update.c` minimum cases: SHA-256 known-answer vectors (empty,
"abc", 1 MiB streamed in odd chunk sizes vs one-shot); manifest: §3
example parses; missing each required key fails; duplicate key fails;
4097th byte rejected by caller contract; `File=../x`, `File=a/b`,
`Size=0`, `Size` >16 MiB, non-hex/short `SHA256`, apploader trio partial,
`FormatVersion=2` → FORMAT_UNSUPPORTED, CRLF and LF both parse, no
trailing newline parses; Decide matrix (equal commit / newer / older /
equal-time-different-commit / min-build refuse / min-build pass);
ParseBase (`1.2.3.4`, `:8080`, `/path/`, `http://` prefix, `https://` →
HTTPS, `example.com` → NON_IP, `..`, port 0/65536, empty); ParseHttpHead
(200+length, chunked, 301, duplicate lengths, lowercase names, head
without terminator). Generator test per file 8. Settings: the existing
`test_settings_file.py` round-trip + key-agreement tests must pass with
the new keys (`:517-600`) — that is the gate that catches a missed
§6.1 site.

CI then covers what the host cannot: pinned-image build, `verify_dol.py`
structure/size/commit, `check_stack_usage.py`, reproducible second
build, and the Dolphin menu walk (`AGENTS.md:66-73`). If the emulator
walk is extended to open Setup › Network (recommended: proves the five
rows render and hide without an adapter), its `LABEL_BOX`-style
coordinates in `buildtools/ui/emulator/run.py` are updated in the same
PR per `AGENTS.md:146-152`.

## 11. The size gate (decide before PR-1 merges, measure first)

Implementation step 0, before writing glue: build current `beta` in the
pinned image and record `stat -c%s cube/swiss/swiss.dol` against the
4,456,448 B tripwire (`verify_dol.py:29-32`). The updater (SHA-256 +
pure parsers + HTTP + pipeline + ~40 message strings, `-Os -flto`) will
not fit in ~5.7 KiB. Options, in order: (1) raise `MAX_DOL_BYTES` as the
explicit decision its comment anticipates, updating `AGENTS.md:78-82`
in the same commit and recording the new headroom — the real ceilings
are far away (24 MiB RAM; 16 MiB `wiiload_image_valid` cap); (2) only
if the maintainer refuses (1), scope down to SMB-only (drop §5.2 and
the Server row) and re-measure. PR-1 does not merge red on `verify_dol`,
and the PR description states before/after DOL bytes.

## 12. Manual test checklist (emulator first, then hardware)

Fixtures to prepare once: a LAN static server (`python3 -m http.server`
on a LAN IPv4 address is sufficient) and an SMB1-enabled test share,
each holding a `dist/update/`-shaped tree built from a throwaway tag;
variants with one flipped DOL byte, a truncated file, a manifest whose
hash is wrong, an older `BuildTime`, and a `MinBuildTime` in the future.
Use the console lab / LogHost (`docs/SETTINGS.md:220-221`) to capture
`print_debug` during hardware runs. Nothing here is claimed until run;
record results against the build's commit, per house validation rules.

**Emulator (Dolphin, CI-adjacent):**

- [ ] No network adapter configured: Setup › Network ends at Time Zone;
      the five rows are absent; nothing else moved.
- [ ] Settings round-trip on a virtual card: set Channel/Server/Boot
      File, Save & Exit, reset, values persist in
      `swiss/settings/global.ini`; Discard & Exit drops them.
- [ ] Keyboard: type `192.168.1.10:8080/indigo` into Update Server
      (colon from the UPPERCASE page); value survives save/load intact.

**Hardware — transports and decisions** (PicoBoot + `ipl.dol` +
SD2SP2 config device as the primary rig; repeat the happy path once on
GC Loader `boot.dol` and once launched as `apps/indigo.dol`):

- [ ] HTTP happy path end-to-end: update installs, Restart boots the new
      build (System › System Information commit changes), settings and
      saves intact, IGR (Apploader mode) returns to the **new** Indigo.
- [ ] SMB happy path, same assertions, server = SMB1-enabled share.
- [ ] Up-to-date: manifest commit == running → no download offered.
- [ ] Downgrade manifest → D_WARN older-build box; B leaves install
      untouched; A installs and boots.
- [ ] `MinBuildTime` future manifest → refusal naming `MinVersion`.
- [ ] DHCP on and DHCP off (static IP) both complete the HTTP path;
      cable unplugged / wrong gateway → single `NO_NETWORK` box, no hang.
- [ ] BBA removed after Settings opens → action fails gracefully.
- [ ] ENC28J60 / W5500 adapter in a memcard slot (if available): rows
      appear, HTTP path completes (adapter identification `bba.c:47-93`).

**Hardware — failure injection** (install must survive every one):

- [ ] Server killed mid-download → error box, `.new` gone, old build boots.
- [ ] One flipped byte in served DOL → `HASH_MISMATCH`, nothing installed.
- [ ] Truncated body vs `Content-Length` → `SIZE_MISMATCH`.
- [ ] Chunked-encoding server / 301 responder → `HTTP_UNSUPPORTED`.
- [ ] SMB2-only server (SMB1 disabled) → `SMB_REFUSED` with SMB1 note.
- [ ] Share without `indigo-updates/` → `SMB_NOT_FOUND` with full path.
- [ ] Card filled to < update size free → `NO_SPACE` before download.
- [ ] B-cancel during download and at each confirm → clean state.
- [ ] Power pulled during Downloading and during Verifying → old build
      boots; next run cleans stale `.new`.
- [ ] Power pulled during Installing (repeat ×5) → either old or new
      build boots, or loader fallback + manual `.bak` rename recovers;
      record which, every time (this validates §4.2.9's window honesty).
- [ ] Manual rollback drill: after a successful update, on a computer
      rename `.bak` files back → previous build boots, apploader pairs.
- [ ] Both `ipl.dol` and `apps/indigo.dol` present with Boot File=Auto →
      `TARGET_AMBIGUOUS`; setting the row explicitly then updates the
      right file only.
- [ ] Long-download soak: throttle the HTTP server to ~50 KiB/s → no
      idle-timeout false positives; progress/ETA sane; B still cancels.

## 13. Open decisions and unresolved evidence (named, not assumed)

- **D1 — trust anchor (owner decision).** v1 ships hash+confirm (§7.3)
  because no signing key, TLS, or CA story exists. Stronger authenticity
  (e.g. a pinned Ed25519 public key verifying a manifest signature)
  needs key custody + release-pipeline signing + a verifier's bytes in
  the budget. The manifest's unknown-key rule already permits adding a
  `Signature=` line later without a format break; the decision is
  whether to fund it before v1 or after.
- **D2 — default HTTP host (operator decision).** Which origin serves
  `dist/update/` over plain HTTP by IPv4, and what value
  `UPDATE_DEFAULT_SERVER` then takes. Neither candidate origin was
  verified for this (the packs host's source is absent under `~/Kleos`;
  the site's canonical repo `kleos/norvitech-site` has no local checkout
  — clone before any site work, never edit the stale GitHub mirror).
  Blocks only the default value, nothing else.
- **D3 — DOL budget raise amount (§11)**, after measuring.
- **D4 — tracker item.** No OpenProject item for an on-console updater
  exists in the top-15 tracker search (re-checked 2026-11-19; nearest
  OP#1195 console lab, OP#1259 manual pack, OP#969 held site cleanup).
  Create one when PR-1 opens, per the ship-in-one-call ceremony.
- **D5 — SDK facts only visible inside the pinned image** (one bounded
  `docker run --rm …img… sh -c 'ls $DEVKITPRO/portlibs/gamecube/include
  …; grep -rn reallocarr …/include | head'` resolves all): whether a
  SHA-256 or mbedTLS portlib is already linkable (§9 file 1), whether
  `reallocarr` is declared under libogc2 r2500 (§1.2 item 1), whether
  `net_gethostbyname` exists and `if_configex` exposes a DHCP DNS
  server (would relax §5.1 later — not assumed now), and the effective
  blocking-`net_connect` timeout (§5.2.1). Also still unverified from
  the audits: TinySMB behaviour beyond `smbInit/smbClose`, and any
  on-hardware throughput/SMB1 figures (all §12 items are unrun).
- **D6 — upstream HEAD drift.** `96e21433` was pushed the day of the
  audit; PR-0 re-checks `ls-remote` and re-triages anything newer
  before pinning.

## 14. Risks, ranked

1. **Boot-file swap is the only irreversible-feeling step.** Mitigated
   by staging + double verification + `.bak` + documented manual
   restore; residual risk is the milliseconds rename window and a
   loader with no fallback (some PicoBoot/PicoLoader chips ignore
   `ipl.dol` entirely — `sd_package.sh:103-105` already warns; for
   those, Auto-probing may target a file the chip never reads, which
   "succeeds" harmlessly but does not update — the guide's existing
   check applies).
2. **Size tripwire** (§11) can force the SMB-only scope cut if refused.
3. **SMB1-only servers** will be the top support question (§1.2 item 4);
   the error and guide say so up front.
4. **Plain-HTTP trust** (D1): a LAN attacker who controls manifest+file
   defeats hash-only verification; confirm #2's hash display is the
   mitigation that exists, and the copy never overclaims (§7.3).
5. **A same-milliseconds paired-files mismatch** (new DOL + old
   apploader or vice versa) if power dies between the two swaps: blast
   radius is IGR-Apploader returning through a mismatched stub, not the
   boot path; the next successful update re-pairs them.
6. **Settings wiring is 7+ sites** (§6.1); the existing host gates catch
   omissions, but only if PR-1 actually runs `run_tests.sh all` before
   pushing — listed first in §10 for that reason.
