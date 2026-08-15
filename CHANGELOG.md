# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [semver](https://semver.org/), with a separate ABI version — see
[docs/versioning.md](docs/versioning.md).

## [0.1.0] — 2026-08-15

First release. Converts maps between Open Doctrines `.odmap` archives and
Greater Diplomacy 5 map directories, in both directions.

### Added

- **Both readers and writers**, against a shared interchange model rather than
  directly between the two layouts.
- **Lossless round trips.** Everything the destination format cannot hold is
  written into a sidecar beside the map — a directory both games ignore — and
  restored on the way back. `dragoman roundtrip` asserts it; `--no-sidecar`
  turns it off for a smaller, deliberately lossy conversion.
- **Derived geometry.** Province adjacency, centroids, coastal flags and the
  land/sea mask are computed from the province raster, because Open Doctrines
  derives all of them at load and GD5 requires them stored. The centroid finder
  handles crescent-shaped provinces, whose mean pixel falls outside themselves.
- **Script translation** in both directions for the shared subset — a gate and
  the things that happen when it opens. Constructs outside it are reported by
  name and carried verbatim. GD5 event types the library does not model pass
  through unchanged, so GD5 → OD → GD5 is lossless for all of them.
- **C99 ABI** with a C++ RAII wrapper, a ctypes Python package needing no
  compiler, and a `dragoman` command line tool.
- **CI** across Linux, macOS and Windows, building, testing, and importing and
  exercising the Python binding on each — a symbol that is not exported passes
  every C++ test and fails at the first `dlsym`.
- **Version consistency checks**, from inside the build and from a standalone
  script, over `VERSION`, the C header, the Python package and CMake.

### Verified

Every map both games ship round-trips with no modelled field lost: 28 of 28
Greater Diplomacy 5 maps (12 base maps, 16 scenarios) and 6 of 6 Open Doctrines
maps, plus the unit suite. Reproduce with `tools/conformance.py`.

Facts established against the real data, each of which cost a bug first:

- Province ids survive exactly. Open Doctrines packs an id into a pixel
  big-endian and GD5 little-endian, verified across all 1248 and 2523 provinces
  of the two games' maps with no exception, so the rasters differ only by
  swapping red and blue.
- A GD5 nation's identity is its key in `nation_data`, not its `name` field. The
  1914 scenario has two separate nations both named "German Empire"; treating
  the name as the identity merged them and handed 121 provinces to nobody.
- `UNC` / "Unclaimed" is a real country in both games, not a blank — 104
  provinces of Open Doctrines' 1914 map belong to it.
- A nation's claims and a province's cores are different facts in GD5. Merging
  them invented cores and discarded claims.
- Open Doctrines does not divide water into provinces at all, so a map crossing
  to GD5 arrives with nothing to sail on. Reported as `gd5.nosea` rather than
  passed over in silence.

[0.1.0]: https://github.com/Pr1nted/dragoman/releases/tag/v0.1.0
