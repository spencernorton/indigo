# Swiss clock and timezone intake

Reviewed upstream range: `abe21df5165e9cfe8cdbb908f9f44e137fcfae98` through
`d9159b024029d0dc2e8a2f5632983e01054e5221`. The original nine commits end at
`1822a324`; the directly related environment-lock follow-up is included too.

| Commit | Change | Indigo disposition |
| --- | --- | --- |
| `ccdac2d` | SNTP unicast client | Preserve Indigo's bounded, opt-in client, validated replies and launch/network timing fence. Upstream source is retained for provenance but excluded from compilation. Read `SNTPHostIP` as an alias of `NTPHostIP`. |
| `5ad51cd` | Timezone counter bias | Preserve Indigo's local-clock/RTC-bias model and existing POSIX rules. Avoid applying a second offset through process-wide `TZ`. |
| `45af0e6` | Common controller A/B masks | Apply the common A mask to the destination picker and B mask to System Information, retaining Indigo's interaction design. |
| `78b88ae` | Named system timezones | Import the timezone table unchanged. Resolve location names in the existing Time Zone setting, alongside POSIX strings. Read `System Time Zone` as an alias. Preserve empty/`Etc/Unknown` offset inference and Sync Clock's default off state. |
| `1af795a` | Ellipsis for long settings | Indigo already stages bounded, ellipsized setting labels/values. Keep that renderer and layout rather than adding the stock event/draw path. |
| `d778292` | lwIP licence packaging | Bundle the pinned stack's licence in legacy distributions and the SD-card ZIP. Vendor it because the pinned SDK lacks the newer standalone COPYING path. |
| `dd6d6f8` | Minimum RTC from commit time | Defer automatic RTC rewriting. Preserve the existing startup clock and pinned SDK; any RTC policy change needs separate compatibility and physical qualification. |
| `e8283d2` | Always set TZ | Reverted upstream by the following commit; review the final combined behavior. |
| `1822a32` | Revert always-set TZ | Preserve Indigo's existing environment/local-clock contract. |
| `d9159b0` | Lock environment operations | Lock configuration publication and DOL/ELF/Wii environment snapshots. Retain the pinned SDK's established reset and unsupported-environment cleanup. |

The SDK remains the pinned libogc2 r2482 image. It exports RTC get/set operations
but does not export `__SYS_InitTime`, which newer upstream configuration/reset
code expects. The new periodic SNTP client also writes the RTC repeatedly while
Indigo deliberately closes clock stepping before a game starts. These are
declared adaptations, not unqualified imports of a second clock owner.

Time Zone names use the imported table's recurring rules. They are not a complete
historical timezone database. The offset is chosen when Sync Clock runs; this
change does not introduce a background daylight-saving transition timer.

The settings semantics fixture incorporates the two new upstream clock arms
while retaining its five recorded Indigo motion/audio arms. The audit strips
only the exact mapped clock-arm bodies. The startup fixture carries upstream's
initialization cleanup while preserving Indigo's opt-in clock defaults.

Qualification covers location lookup and existing POSIX offsets, DST boundaries,
all supported table leaves, invalid names, bounded configuration aliases,
environment publication, source boundaries, host sanitizers and emulator routes.
Actual console clock persistence, launch/return and device behavior remain final
hardware gates. No installed boot files are changed by this intake.

## Independent timezone-data qualification

An independent check against IANA 2026f found Havana's temporary rule cannot be
represented by the imported recurring POSIX tail. The October 27 IANA release
cancels the November 2026 fallback and keeps UTC-04 until November 7, 2027.
Indigo adds a dated exception in its owned resolver, preserving explicit POSIX
settings and the upstream table bytes. The source is the IANA 2026f release;
the pinned source archive and offline fixture provenance are recorded with the
host test. The fixture checks all 446 names and both sides of future transitions
in the stated 2026-10-31 to 2028-01-01 horizon. Runtime uses no external database.
