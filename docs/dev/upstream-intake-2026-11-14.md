# Swiss and libogc2 r2500 intake

Reviewed upstream Swiss range: `d9159b024029d0dc2e8a2f5632983e01054e5221`
through `4c9e30aefb367fc70e3fa55e98003e7bb2955358` (20 commits), paired with
the `libogc2 r2500.cb7745a` SDK image
(`sha256:7c91503254afcf0acebc8edb6f81fc8535860c42ae31ca33dd1573464eb12565`).

| Commit | Change | Indigo disposition |
| --- | --- | --- |
| `9cc94597` | Fix undefined behavior in executable packer | Adopt upstream's compile-time `DEC_IS_SINGLE` mode check across `cube/packer/source/xz/` and restore upstream `cube/packer/source/main.c`. Update `test_packer_decoder.py` to mutate `DEC_IS_SINGLE` directly. |
| `d8dc6424` | Update date/time display in SNTP client | Retain upstream `sntp.c` uncompiled for provenance and declare `__SYS_InitTime` in `swiss.h`; keep Indigo's opt-in bounded NTP client and local-clock model without `updateRTC`. |
| `5c1bed91` | Move `VAR_RMODE` and preserve POSIX time | Adopt `reservedarea.h`, `reservedarea.ld`, `patcher.c` and the widescreen perspective hooks so `0x800030F8` (`VAR_POSIX_TIMEBASE`) matches `libogc2 r2500`. |
| `321785f5` | Avoid rewriting RTC every hour in SNTP client | Retain in uncompiled `sntp.c` for provenance; Indigo's `time_sync.c` runs once per boot when enabled and never writes the hardware RTC. |
| `62a8b9a2` | Update Redump, No-Intro and `[T-En]` databases | Adopt in `cube/swiss/source/nkit.c`. |
| `9cf2a31b` | Reduce C++ exception/unwinding overhead | Adopt `-fno-exceptions -fno-rtti` on `CXXFLAGS` in `cube/swiss/Makefile`; prebuilt archives retain their registered unwind frames, verified by `check_unwind.sh`. |
| `670dfff6` | Fix Call of Duty 2: Big Red One return to menu | Adopt `frontend.dol` pre-patch exclusion in `cube/swiss/source/gcm.c`. |
| `6330836b` | Remove legacy migration and fix progress race | Split: adopt `atomic_fetch_add` on `atomic_int speed` in `DrawUpdateProgressLoading` (`FrameBufferMagic.c`) and the upstream `swiss_patches` / legacy memory-card path cleanup across device handlers and `gcm.c`; retain `config_parse_legacy` in `config.c` and `main.c` so existing `swiss.ini` files still migrate. |
| `fda972b0` | Split C++ allocator overrides into `libstdc++.cpp` | Adopt `cube/swiss/source/libstdc++.cpp`. |
| `1d02198d` | Adjust ARAM expansion size query for `libogc2` | Adopt `AR_GetExpansionSize()` in `deviceHandler-SYS.c` and `gui/info.c`. |
| `2ae78094` | Replace TinySMB with `libsmb2` | Decline `libsmb2` in `deviceHandler-SMB.c`, `Makefile` and `cube/swiss/Makefile`, retaining the SDK's TinySMB (`-ltinysmb`); adopt only the harmless `struct stat` variable rename in `deviceHandler-FSP.c` and `deviceHandler-FTP.c`. |
| `63932963` | Export SMB credentials to process environment | Decline in `config.c` so SMB credentials are never exported into launched homebrew environments. |
| `0502ea95` | Update `[T-En]` database | Adopt in `cube/swiss/source/nkit.c`. |
| `757ed4e9` | Derive version from Git tag in top-level `Makefile` | Not applicable: Indigo tags releases directly (`docs/RELEASING.md`) and verifies the exact 7-character commit embedded in `swiss.dol`. |
| `2cca98ff` | Generate `defines.h` in `cube/swiss/Makefile` | Not applicable: Indigo passes deterministic `GIT_COMMIT`, `GIT_REVISION` and `GIT_TIMESTAMP` flags directly in `cube/swiss/Makefile`. |
| `d00a2237` | Switch banner charset detection to `uchardet-lite` | Retain the SDK's pre-installed `libuchardet` in `filemeta.c` and `cube/swiss/Makefile` (listed in `UPSTREAM`); the production DOL remains well within the 4.25 MiB budget. |
| `dcba7863` | Add release changelog text in `swiss.c` | Not applicable: Indigo ships release notes from `CHANGELOG.md`. |
| `245ea76f` | Clamp RTC forward to build timestamp and add `updateTZ` | Decline automatic `GIT_TIMESTAMP` RTC clamping and process-wide `TZ` mutation; Indigo preserves the console's clock unless Sync Clock is explicitly enabled, and `config_init_environ()` clears any `TZ` inherited from a parent loader before calling `__SYS_InitTime()`. |
| `84b5b0b1` | Refresh time base after synchronous SRAM writes | Adopt `if (syncSram) __SYS_InitTime();` in `sram.c` alongside upstream `sidestep.c`'s `__SYS_InitTime()` handoff before `ARAMRunStub`. |
| `4c9e30ae` | Add disabled dummy date/time rows in stock Settings | Decline: touches stock Settings presentation only; Indigo has its own Settings views and live corner clock. |

## SDK pairing (`libogc2 r2500.cb7745a`)

- `libogc2 r2500` tracks `VAR_SYSTEM_TIMEBASE` (`0x800030D8`) and
  `VAR_POSIX_TIMEBASE` (`0x800030F8`) separately. Pairing `5c1bed91` with the
  SDK update prevents `VAR_RMODE` from colliding with `0x800030F8`, and
  restoring upstream `sidestep.c` refreshes both time bases from the RTC and
  SRAM counter bias right before handing off to a game or DOL.
- `cube/swiss/aesnd/UPSTREAM` moves to `cb7745aa099e65c0a3295f482f691ad63ba7b730`
  (`libogc2 r2500.cb7745a`); upstream `libaesnd` is byte-identical between
  `r2482` and `r2500`, and Indigo's local DSP reset patch continues to apply
  cleanly.
- `buildtools/video_lab/sdk/video_540_driver.c` and `provenance.json` move to
  `cb7745aa099e65c0a3295f482f691ad63ba7b730`, picking up upstream's balanced
  vertical blanking intervals during blackout while preserving the isolated
  540p60 timing entry verified by `test_sdk_540p.py`.
