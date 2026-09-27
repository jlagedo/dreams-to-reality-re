# lib9660 source used by ODShared

Upstream: [erincandescent/lib9660](https://github.com/erincandescent/lib9660),
commit `17704f87e833f3de46b1546864d47ef4a6d12b97`.
The copyright and ISC-variant permission notice remain in the C files; `LICENSE`
is the upstream license text. This directory contains no game data.

Local changes to the two upstream source files:

- Give the C API C linkage when included from C++.
- Advance and bound the primary-volume-descriptor scan, check its version,
  logical sector size and root record, and reject bad directory record lengths.
- Fix multi-component path lookup and reject invalid seek/read ranges.
- Avoid signed-shift overflow and sector-position overflow on malformed input.

The ODShared wrapper additionally checks the data-track length, mirrored ISO
extent fields, volume and entry bounds, cycles, source identity, and bounded
file reads. See `shared/disc/image.cpp` and `tests/disc_tests.cpp`.
