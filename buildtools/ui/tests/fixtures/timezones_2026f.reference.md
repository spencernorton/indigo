# IANA timezone reference vectors

The TSV contains 2,374 independent expected offsets for all 446 supported
location names, across 2026-10-31 through 2028-01-01. It samples fixed winter/
summer times and both sides of every observed future offset transition. Havana
also probes its canceled November 2026 fallback explicitly.

Reference: https://www.iana.org/time-zones/releases/2026f
Archive: https://data.iana.org/time-zones/releases/tzdata2026f.tar.gz
SHA-256: 01b4a91cec5167ed48c11e8ab78f2048cead78ce94b0cafcad0ae8c5a3044d3d

Regenerate on a host with Python 3.9+, a C compiler and zic. Compile tzdata.c
as a native shared library (for example, cc -shared -fPIC with cube/swiss/include
on the include path). Run generate_timezone_reference.py with --archive naming
the downloaded archive, --table-library naming that native library, and --out
naming the TSV. The generator verifies the archive hash and builds its own
reference zone files. It uses Indigo's table only to enumerate names; all
expected offsets come from IANA through Python's ZoneInfo.

The offline test requires every supported name and compares the actual production
resolver with these frozen values. It requires no network or system timezone data.
The sampled horizon is explicit; it is not full historical timezone qualification.
