# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [semver](https://semver.org/), with a separate ABI version — see
[docs/versioning.md](docs/versioning.md).

## 0.5.0 — 2026-09-27

Greater Diplomacy 5 gained domestic politics and guarantees. Three more things
cross because of it, and one deliberately does not.

### Added

- **The political axis.** Both games put a country on one
  authoritarian-to-libertarian line, so it now crosses: GD5's `political_value`
  and the `auth` figure in Open Doctrines' compass file.

  The scales differ by ten and the model keeps the finer. Because GD5's is
  coarser, an `auth` of 89 arrives there as 9 — and comes home as 89 again
  unless the value actually moved in GD5, in which case the map wins. Same rule
  as the forts.

  **The sign is worth checking before touching this.** Positive is
  authoritarian in GD5's field and in Open Doctrines' compass *file*; it is
  libertarian in that game's in-memory compass, whose loader negates both axes
  on the way in. This library reads the file, so no flip belongs here.
  `test_politics.cpp` pins it, and fails if it is inverted.

  GD5 has no economic axis, so `left` is carried untouched, and a country
  arriving from GD5 is written centrist there rather than having an economic
  position invented for it.

- **Guarantees.** A guarantee belongs to the guarantor on both sides — GD5
  keeps a list on the promising nation and Open Doctrines records it the same
  way round — so it crosses without needing to be paired up.

- **Truces.** The fact crosses; the number of turns left rides in the sidecar,
  because Open Doctrines has nowhere to put a countdown. A truce that goes out
  with five turns left comes home with five, not with a fresh twelve.

### Not translated, on purpose

- **Policies.** Both games have them and they are not the same set: five GD5
  domestic policy cards against fifty-nine Open Doctrines doctrines, sharing no
  name, requirement or effect. Pairing them by resemblance would be inventing a
  government's programme, so each side's are carried and a conversion says so
  once (`gd5.policies`, `od.policies`). The axis they are both gated on does
  cross, so a country arrives facing the right menu.

- **Non-aggression pacts.** Still no counterpart. A truce is the nearest thing
  GD5 has and it is not the same: a truce expires on a counter and a pact does
  not, so writing one as the other would invent an end date.

## 0.4.1 — 2026-08-30

### Fixed

- **An assignment's operator was being read as its value.** Open Doctrines'
  engine version 2 writes `set var.gold = 100` where version 1 wrote
  `set var.gold 100`. This library read the third word as the value either way,
  so it translated the variable as the literal string `"="` — with no warning,
  in a script that otherwise looked translated. `+=` produced `"+="` the same
  way.

  That is the failure this library is built to avoid: it is not a gap in
  coverage but a wrong answer stated confidently. Both games' block editors
  normalise assignments to that form, so anything written in one hit it.

### Added

- **The C-style assignment spellings are understood.** Version 2 makes `set`
  optional and allows `+=`, `-=`, `*=`, `/=`, `++` and `--`; the engine
  normalises all of them to a `set` line before doing anything else, sharing
  one normaliser between the engine, the linter and the block editor. This is a
  mirror of it, so `var.gold = 100` now translates where it was previously
  carried untranslated.

  Not everything crosses: the compound forms and an arithmetic right-hand side
  are refused, because a GD5 event sets a variable rather than computing one.
  See [docs/scripting.md](docs/scripting.md).

### Changed

- **A refused script names what it found.** The report said "loops,
  conditionals or collections" whatever the reason, which after version 2 is
  usually wrong — a script stopped by a `label` or a `dialog` sent the reader
  looking for a loop that was not there. Version 2's fifteen statements are
  each recognised and named.

- **Generated scripts declare `#OD/MapEngine/2`.** The body is still version 1
  syntax, which version 2 accepts, but declaring 1 pinned a script the block
  editor may reopen to a dialect the game is moving away from.

## 0.4.0 — 2026-08-30

### Added

- **Forts cross.** Greater Diplomacy 5 added forts as a building — `Fort Lvl N`,
  one per province — after this library was written, and Open Doctrines has
  always had a per-province `fortification`. The two describe the same thing,
  so it is now translated both ways instead of being carried in the sidecar and
  handed back unseen by the destination game.

  The ladders are different heights: GD5's `FORT_MAX_LEVEL` is 20, Open
  Doctrines clamps to 5. Levels are **scaled ×4 rather than clamped**, because
  clamping would flatten every GD5 level above 5 into the same Open Doctrines
  fort and lose the shape of a fortified world. Writing multiplies exactly and
  reading divides rounding up, so a round trip returns the level it set out as,
  and a GD5 fort of any level arrives as at least level 1 — rounding down would
  have deleted its levels 1–3 outright.

  Note that this is the fort, not the technology: GD5 has no fortification tech,
  so `fort1`…`fort6` still do not cross. See
  [docs/research.md](docs/research.md).

### Changed

- **A fort built in Greater Diplomacy 5 survives coming home.** The sidecar
  remembers the level a map left with, and used to win unconditionally on the
  way back — correct when GD5 had no forts to build, and wrong now, because a
  player who built one watched it disappear. The map wins; the record only
  fills a gap. Level zero is still read from the record, since GD5 cannot say
  "razed" distinctly from "never had one".

- **Writing a map rewrites its fort** rather than passing through whichever one
  it arrived with, so a level changed in Open Doctrines is the level GD5 gets.
  Every other building is left exactly where it was.

## 0.3.2 — 2026-08-16

**No code changed.** The library is byte-for-byte what 0.3.1 built; only the
documentation is new. Said plainly because a version number usually implies
otherwise, and nobody should go looking for a behaviour change that is not
there.

### Added

- **[docs/locator.md](docs/locator.md) — how a game says where it is.**

  Translating a map means writing it where the other game will find it, which
  means knowing where the other game is. Both sides were guessing: a fixed list
  of the usual install folders, one level deep, and a folder picker when the
  guess missed. A game already knows exactly where it is; it only has to write
  it down.

  One small JSON file per game in a shared per-user directory, rewritten at
  every launch. Open Doctrines and Greater Diplomacy 5 both implement it, so
  neither has to search when the other has ever run.

  Most of the document is about the awkward half: a locator goes stale and
  nothing announces it. A moved game corrects its file on next launch, a
  deleted one never does, so a file pointing at nothing is the ordinary state
  rather than an edge case. Hence the rules — verify before offering, verify
  again before each use, never delete a file that is not yours, and never read
  one as permission to write.

## 0.3.1 — 2026-08-16

### Fixed

- **Every Python caller failed on maps with accented nation names.** Ten of
  Greater Diplomacy 5's twelve shipped base maps could not be converted by
  anything written in Python, while the command line tool converted all twelve
  — and the difference was the caller's locale, not the map.

  `<cctype>`'s `isalpha` and `toupper` answer according to the current locale.
  A C++ program starts in `"C"` and never leaves it unless it says so, so the
  CLI — and every test here, which is also a C++ program — saw ASCII rules and
  was right. Python calls `setlocale(LC_ALL, "")` at startup. Under `C.UTF-8`,
  `isalpha` accepts bytes above 0x7F and `toupper` maps them to *other* bytes
  above 0x7F, so building an ISO 3166 code by walking a nation's name byte by
  byte produced three mangled UTF-8 continuation bytes. The JSON writer then
  refused the whole map: `invalid UTF-8 byte at index 2`.

  ISO codes, file extensions, hex colours and table lookups are all defined in
  ASCII, so they are now done by ASCII rules that no locale can reach.

  The suite could not have caught this, because nothing in it ever set a
  locale. `test_locale` now runs its checks three times: in `"C"`, in whatever
  the environment says, and in an explicitly named UTF-8 locale.

## 0.3.0 — 2026-08-16

### Added

- **Research crosses into Open Doctrines.** A `.odmap` written from a GD5 map
  now carries `research.json`: ISO code to a sorted list of Open Doctrines
  research nodes, mapped from GD5's levelled technologies. Read back on the
  next crossing, where it takes precedence over research derived from the map's
  date.

  Open Doctrines does not read this file — no existing extension point can
  apply it, since the Gearbox `Research.Write` capability exposes only
  `set_country_funding`. The field is carried and inert until the game reads
  it; [docs/research.md](docs/research.md) sets out the change that would make
  it take effect, and what crosses and what deliberately does not.

  Round trips never depended on this and still do not: the exact GD5 table
  rides in the sidecar.

### Fixed

- **A single boolean unlock granted an entire research ladder.** Five GD5
  technologies have `max_lvl: 1` — `basic_factory`, `bergius_process`,
  `battleship`, `dreadnought`, `basic_recruitment` — and are held or not held.
  Read as a fraction of their ceiling each scored 1.0, so one of them carried a
  nation to `ind10` or `navy10` off an unlock it had at the start of the game.
  A flag now earns the first rung of its ladder and no more. An absent ceiling
  is treated separately again and grants nothing, which is not the same thing.

- **A ladder gained a rung every time a map changed hands.** Writing rounded to
  nearest and reading rounded up, so `ind3` went out as a level that came back
  `ind4`. Writing now rounds down, and every rung of every ladder survives the
  crossing exactly.

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
