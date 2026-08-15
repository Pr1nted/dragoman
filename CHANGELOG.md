# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [semver](https://semver.org/), with a separate ABI version — see
[docs/versioning.md](docs/versioning.md).

## 0.2.2 — 2026-08-15

The C++ wrapper was unusable and nothing here noticed, because nothing here
used it the way a consumer does. The ABI is unchanged at 2.

### Fixed

- **The C++ wrapper corrupted memory when linked against the static library.**
  `dragoman.hpp` defines `Options`, `Report`, `Diagnostic` and `World` in
  namespace `dragoman` — and the library's own implementation already has types
  with those exact names, which the static archive exports. Two definitions of
  `dragoman::Report` in one program, implicit copy constructors and destructors
  that mangle identically over different layouts, and the linker keeps one.
  Constructing a `World` and letting it fall out of scope segfaulted, with the
  map's name sitting in the bytes where the handle belonged.

  The wrapper is now in an `inline namespace api`, so callers still write
  `dragoman::World` while the symbols mangle apart. Only the static library was
  affected: the shared one exports nothing but the C ABI, which is why the
  Python binding and the CLI were always fine.

- **`find_package(dragoman)` now works.** The export set was declared and never
  installed, and no package config was generated, so `cmake --install` put
  files on disk and left every CMake consumer to hardcode paths.

- **The installed static archive links on its own.** miniz, stb and
  nlohmann/json are compiled into it rather than left in a second archive that
  was never installed at all.

- **macOS wheels build.** cibuildwheel tagged them `macosx_10_9` while CMake
  built for 11.0, and delocate rejected the mismatch — which is why 0.2.1 never
  reached PyPI.

### Changed

- **A release is gated on CI passing that same commit.** A tag used to start
  the release workflows alongside CI, so a release could be built and published
  while its tests were still running, or after they had already failed.

- **CI builds a real consumer against the installed library** — a separate
  project, `find_package`, public headers only. It is the only check that
  tests the install rather than the build tree, and the only one that
  reproduced the namespace collision above.

## 0.2.1 — withdrawn

Tagged and then withdrawn: the release workflows still ran alongside CI rather
than behind it, the macOS wheel failed, and nothing reached PyPI. The GitHub
release and its tag are deleted, so there is no version of this anyone can
install. Everything below shipped in 0.2.2.

The first release published from CI. Everything here is packaging and
portability — no map is converted differently by this version, and the ABI is
unchanged at 2.

### Fixed

- **Wheels are usable by more than one interpreter.** They were being tagged
  `cp314-cp314-macosx_26_0_arm64`: CPython 3.14 only, macOS 26 only. Nothing
  here compiles against CPython's ABI — the library is plain C loaded by ctypes
  — so that tag was an accident of whichever interpreter ran the build. Now
  `py3-none-macosx_11_0_arm64`: any Python 3, macOS 11 and up. 0.2.0 on PyPI is
  effectively installable by nobody; this is the release to use.
- **A turn counter no longer narrows on Windows.** `Date::turn` was `long`,
  which is 64-bit on Linux and macOS and 32-bit on MSVC, so reading one out of
  JSON truncated. It is `int64_t` now, like every other integer in the model.
  Caught by CI's first Windows run.
- **The Windows build links the right library.** A DLL produces an import
  library beside it, and the shared target is named `dragoman`, so both it and
  the static archive wrote `dragoman.lib` into the same directory. The tests
  ended up linked against the DLL's import library, which exports only the C
  ABI, and every internal C++ symbol was unresolved. The static archive is
  `dragoman_static.lib` on Windows now; consumers are unaffected, since CMake
  links by target name.

## 0.2.0 — 2026-08-15, PyPI only

Both changes here came from opening converted maps in the games themselves and
looking at what was wrong.

### Added

- **Sea provinces are invented when the source game does not draw any.** Open
  Doctrines leaves its oceans unpainted; GD5 can neither render nor sail across
  what is not a province, so a converted map arrived with a black sea and no
  fleet could move.

  The water is **grown** into provinces, not cut: seeds are laid on a lattice,
  displaced by a hash of their own coordinates so the shapes are irregular but
  a map still converts identically twice, snapped to the nearest water, and
  grown outwards all at once through water only. Each province is the water
  nearest one seed, so it follows the coast — 0.56 on bounding-box fill against
  the 0.66 of GD5's own hand-drawn ones, where a grid would be 1.00. A province
  cannot cross land, so the Mediterranean and the Atlantic are separate however
  close two seeds fall.

  The world map gets 917: one network of 747 that is every ocean joined
  together, and 163 lakes. The ids are recorded in the sidecar and deleted
  again on the way back, so the round trip is unaffected. New
  `synthesise_ocean` option, default on.

- **Research is derived from the map's date.** Open Doctrines stores none in a
  map — its tree is compiled into the game and seeded from a hardcoded list of
  ISO codes — so converted maps reached GD5 with every nation at level zero in
  everything. GD5 already knows how to turn a year into research levels, so
  that rule is applied against the tech tree read out of the GD5 installation
  being written into, rather than a copy kept here that would go stale. Output
  matches GD5's own `get_time_appropriate_research` exactly for 1914 and 2000.
  Nations that arrive with research keep it.

### Fixed

- **Flags now render in GD5.** `flag_data` is not base64 of a PNG, which is
  what this library was writing; it is base64 of *raw pixel bytes* at exactly
  60x40, handed to `pygame.image.fromstring`. A PNG makes that raise,
  `decode_b64_to_surf` swallows the exception, and every nation showed a blank
  white rectangle. Flags are now decoded, resampled to 60x40 and written as raw
  pixels, and the original full-size image is preserved in the sidecar so a
  crossing does not permanently shrink it to GD5's icon size.
- **One rule for "does this map draw its water"**, shared by both writers,
  replacing two that disagreed. Deciding it by whether any province is marked
  sea was wrong in both directions: Open Doctrines maps have a handful of
  coastal provinces that sit mostly under the mask, so no ocean was ever
  synthesised, while GD5 maps with lots of unpainted border did not get those
  borders filled. It is now a pixel count, and the two populations are not
  close — GD5's maps run 0.35 to 7.8 on the ratio, Open Doctrines' all sit at
  0.0001.

### Changed

- **ABI version 2.** `dg_options` gained a field, which changes the struct's
  size, so anything compiled against ABI 1 must be rebuilt. `SOVERSION` moves
  with it.

## 0.1.0 — never released

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

Only 0.2.2 has a git tag and a GitHub release. 0.2.0 exists on PyPI because it
was uploaded by hand before the release workflow worked; 0.1.0 was never
published at all, and 0.2.1 was withdrawn. Releases:
<https://github.com/Pr1nted/dragoman/releases>
