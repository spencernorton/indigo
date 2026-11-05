# Experimental VIDEO driver

`video_540_driver.c` is libogc2's VIDEO driver at the exact revision and
SHA-256 in `provenance.json`, with its original BSD 3-clause notice retained.
Only a new timing object and its `__gettiming` case are added. The header
marks this as a modified copy. `test_sdk_540p.py` reconstructs and hashes the
original file to check that stock code and timings remain intact.

The new timing comes from libogc-rice's HD60 enhanced non-interlaced entry,
translated into libogc2's format numbering and horizontal-field convention.
Do not copy libogc-rice's mode constants into libogc2; the encodings differ.

This object is linked only by `UI_540P=1` builds. A normal build uses the
VIDEO object from its SDK library. See
[the experiment](../../../docs/dev/experimental-540p.md) for build controls,
fallbacks and qualification boundaries. Binary distributions of this variant
must carry the driver's notice, conditions and disclaimer from `NOTICE`.
