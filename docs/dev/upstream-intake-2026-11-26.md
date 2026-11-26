# Swiss r2176 intake

Indigo follows the seven commits from `a647fd74` through `31796322`.
Swiss v0.7.0 was tagged at `92e63a8f`. [Upstream comparison](https://github.com/emukidid/swiss-gc/compare/a647fd74880f2cb31c8edbb9b9ba6a9d6e26a2f5...317963226816f2545dfa2842a90acdc881056950)

| Swiss commit | How Indigo uses it |
| --- | --- |
| `8bdd31f8` | Apply the build-date floor to opt-in network sync. Check the local time against the unsigned 2000-based counter after applying the zone offset. A failed running-clock write leaves the saved bias alone. |
| `ed6ab9fd` | Swiss corrects its process-wide zone adjustment. Indigo resolves the local offset for each accepted reply and keeps the console and IPL on local time. |
| `11a44a41` | Swiss adds manual date and time editors. Indigo keeps its existing Settings controls and records the stock switch arms in the Settings audit. The file rename dialog retains Indigo's text-entry mode. |
| `96e21433` | The archived Swiss SNTP source uses the server transmit timestamp. Indigo's active client already reads that field and matches the response to its request. |
| `92e63a8f` | Swiss preserves the wall-clock time when advancing an old RTC on boot. Indigo reads the user's console clock at startup. Sync Clock remains opt-in. |
| `c6af5ad1` | Swiss suppresses deprecation warnings ahead of an SDK change. Indigo's pinned libogc2 r2500 still supports the SD adapter interfaces in use. |
| `31796322` | Take all five changed XZ Embedded decoder files verbatim. The executable packer uses them to unpack PowerPC BCJ and LZMA2 streams. |

The newer libogc2 adds `reallocarr` and marks the legacy SD interfaces
deprecated. Indigo already supplies the checked array resize for its directory
readers. The SDK pin, including the locally patched audio and video drivers,
stays at `cb7745aa` for this intake.

`test_packer_decoder.py` runs the decoder on a packed PowerPC stream and
checks failure on damaged or incomplete input and insufficient output space.
`test_upstream_clock_intake.py` exercises the firmware clock setter at the
build and counter boundaries, on a failed write and after game launch begins.
