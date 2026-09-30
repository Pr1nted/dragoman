# Dragoman

Translate maps between **Open Doctrines** (`.odmap`) and **Greater Diplomacy 5**
(map directories), in both directions, losing as little as the two formats
allow — and nothing at all on a round trip.

A *dragoman* was the interpreter attached to an embassy: the person through
whom two powers who shared no language could nonetheless sign something. This
is that, for map files.

```bash
pip install open-dragoman

dragoman convert 1914.odmap base_maps/1914 --to gd5
dragoman convert base_maps/GD4 gd4.odmap   --to odmap
dragoman roundtrip 1914.odmap              # prove nothing was lost
```

```python
import dragoman

dragoman.convert("1914.odmap", "base_maps/1914", to="gd5")

world = dragoman.load("1914.odmap")
print(world.name, len(world.provinces), "provinces")
```

**[Read the wiki →](https://github.com/Pr1nted/dragoman/wiki)** — what it is,
how to use it, what every message means, and what it cannot do.

## Status

Every map both games ship round-trips without losing a single modelled field:

| Suite | Result |
|---|---|
| Greater Diplomacy 5 maps (12 base maps + 16 scenarios) | **28 / 28** |
| Open Doctrines maps (5 scenarios + the world map) | **6 / 6** |
| Unit tests (raster, scripts, ABI, versioning, round trip) | **5 / 5** |

Verified against Open Doctrines and against
[GitGetGot415/Greater-Diplomacy-5](https://github.com/GitGetGot415/Greater-Diplomacy-5).
Reproduce with `python3 tools/conformance.py <directory of maps>`.

Converted maps have also been **played in both games**, not merely loaded by
this library. Open Doctrines' world map converted to GD5 boots through GD5's
own `Controller` and `Map` state with all 23 screens, its flags drawn, its
oceans navigable and every province centre inside its own province; a GD5 map
converted to Open Doctrines plays five AI turns under `OpenDoctrines
--simulate`. Nearly everything this library gets right, it gets right because
somebody opened the result in the game and looked at it — the flag encoding,
the ocean, the political layer's colour key and the province-border fill were
all found that way.

One thing crossing to Open Doctrines still needs a human: GD5 starts its
nations with an empty stockpile and Open Doctrines expects a starting
endowment, so a converted map bankrupts its world on the first turn unless
treasuries are set. Reported as `od.treasury` rather than guessed at.

## What "lossless" means here

The two games are not the same game, so a straight translation always loses
something: Open Doctrines models a province's population, its port and its
ethnic minorities, and GD5 has no field for any of them; GD5 models terrain,
province adjacency, unit rosters and research, and Open Doctrines has no field
for those.

Dragoman writes the difference down. Everything the destination cannot hold
goes into a **sidecar** beside the map — `dragoman_sidecar/` in a GD5
directory, `dragoman/` inside a `.odmap` archive. Both games read a fixed list
of filenames and ignore everything else, so the sidecar is invisible to them
and costs the converted map nothing.

The result:

- **Converting one way** is as faithful as the target format permits, and every
  approximation is reported by a stable code you can act on.
- **Converting back** restores what the target could not hold. `A → B → A` keeps
  every province, nation, relation, claim, core, script and carried file, and
  the province raster hashes identical.

`dragoman roundtrip <map>` asserts exactly that. Pass `--no-sidecar` for a
smaller, genuinely lossy conversion — the round trip then fails, on purpose.

Not promised: byte-identical container files. Two zip archives holding
identical members are different files, because deflate is not reproducible
across implementations, and Open Doctrines packs with Python's zlib while this
packs with miniz. Every file *inside* is preserved exactly.

## What crosses

| | Open Doctrines | Greater Diplomacy 5 |
|---|---|---|
| Province raster | `provinces.png`, id packed big-endian | `id_map.png`, id packed little-endian |
| Province identity | preserved exactly — the rasters differ only by swapping red and blue |||
| Owner, name, claims | ✅ | ✅ |
| Cores | — | ✅ (carried) |
| Sea provinces | — (**synthesised** for GD5) | ✅ |
| Population, ports | ✅ | — (carried) |
| Fortification | ✅ 0–5 | ✅ `Fort Lvl N`, 1–20 (**scaled** ×4) |
| Political axis | ✅ `auth`, −100..100 | ✅ `political_value`, −10..10 (**scaled** 10:1) |
| Guarantees, truces | ✅ | ✅ |
| Minorities, economic axis, policies | ✅ | — (carried) |
| Terrain, adjacency, province centres | — (**derived** from the raster) | ✅ |
| Units, buildings, research, factions | — (armies carried) | ✅ |
| Flags | a PNG in the archive | raw 60x40 pixels, base64 |
| Relations | ally, non-aggression, guarantee | war and alliance only (rest carried) |
| Scripts | imperative `#OD/MapEngine/1` | declarative scripted events |

Adjacency and centroids are computed from the province raster when converting
to GD5, since Open Doctrines derives both at load and never stores them. The
centroid finder deliberately handles crescent-shaped provinces, whose mean
pixel falls outside themselves.

Full field-by-field detail: [docs/mapping.md](docs/mapping.md).

## Scripts

Open Doctrines' scripting is imperative and line-based, with loops and
`waitUntil` suspension points. GD5's is a list of declarative events, each a
set of conditions and a set of actions. The overlap is the shape both express:
a gate, and things that happen when it opens.

An Open Doctrines script written as top-level `waitUntil` stages becomes GD5
events, one stage at a time. A GD5 event becomes an entry script with a
`waitUntil` and some `set` lines. Anything outside that overlap — a `foreach`
over a country's provinces, conditions chained with `XOR` — is reported by name
and carried unchanged rather than half-translated. GD5 event types this library
has never heard of pass through untouched, so a GD5 → OD → GD5 trip is lossless
even for conditions added to the game after this was written.

Details and the full vocabulary: [docs/scripting.md](docs/scripting.md).

## Installing

```bash
pip install open-dragoman
```

The wheel carries the compiled library inside the package, so there is no
compiler needed at install time and nothing to locate afterwards. You get both
the Python API and a `dragoman` command.

Prebuilt binaries for people who want nothing to do with Python are attached to
each [release](https://github.com/Pr1nted/dragoman/releases). The full set of
options — release archives, source builds, CMake `FetchContent` — is on
[the Installing page](https://github.com/Pr1nted/dragoman/wiki/Installing).

## Building from source

Needs CMake 3.16 and a C++17 compiler. There are no external dependencies —
miniz, stb and nlohmann/json are vendored, all MIT or public domain.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

This produces `libdragoman.a`, a shared `libdragoman.{so,dylib,dll}`, and the
`dragoman` command line tool.

## Using it from your language

The core is C++17 behind a flat **C99 ABI**, so anything with an FFI can call
it. The whole interchange model is available as JSON through
`dg_world_to_json`, which is how a binding reads or edits a map without needing
an accessor per field.

- **C** — `#include <dragoman/dragoman.h>`, link `dragoman`. See
  [bindings/c/example.c](bindings/c/example.c).
- **C++** — `#include <dragoman/dragoman.hpp>` for an RAII wrapper over the same
  ABI. See [bindings/cpp/example.cpp](bindings/cpp/example.cpp).
- **Python** — `pip install open-dragoman`. Pure ctypes, and the wheel carries the
  library, so no compiler is needed at install time.
- **Java and Kotlin** — JNA, so there is no JNI shim to build and the floor is
  Java 8 rather than Panama's 22. One API serves both languages. See
  [bindings/jvm/README.md](bindings/jvm/README.md).
- **Rust** — `dragoman::convert(..)`, no bindgen: the surface is small enough to
  declare. See [bindings/rust/README.md](bindings/rust/README.md).
- **JavaScript and TypeScript** — koffi, with hand-written types beside it. See
  [bindings/js/README.md](bindings/js/README.md).
- **Zig** — `@cImport` of the header itself, so there is no second copy of the
  ABI at all. See [bindings/zig/README.md](bindings/zig/README.md).
- **Anything else** — Go, C#, Lua and WebAssembly all bind the same header.
  [docs/abi.md](docs/abi.md) documents the contract.

Every binding is run against a freshly built library by CI's `bindings` job.
Each wraps the same ABI and each can get it wrong in its own way: the JVM
binding read `dg_roundtrip_check` with `dg_convert`'s convention and reported a
holding round trip as a failure, which compiled and looked correct.

## More than two games

Unciv is a third destination, and a different kind of one: it has a hexagon per
place where the other two paint provinces onto a raster, so crossing is a
resampling rather than a field mapping. Converting **to** Unciv works and
produces a playable map; reading one back is refused rather than guessed at.
See [docs/unciv.md](docs/unciv.md).

## Versioning

Two numbers that move for different reasons:

- **Library version** (`VERSION`, semver) — the release. (Look in VERSION file to find current latest version).
- **ABI version** (`DRAGOMAN_ABI_VERSION`) — bumped only when an existing symbol
  changes meaning, so a binding can refuse to load a library it cannot speak to
  without parsing semver.

The version lives in `VERSION` and is mirrored into the C header, the Python
package and the CMake project. `tools/check_version.py` and
`tests/test_version.cpp` both fail if any copy drifts, and CI runs them on every
push. Details: [docs/versioning.md](docs/versioning.md).

## Licence

Dragoman is MIT. It is an independent implementation written from observing
both file formats; no code from either game is copied into it. Open Doctrines
and Greater Diplomacy 5 remain under their own licences (Open Doctrines
Non-Commercial, and GPL-3.0 respectively), and neither project's maps are
redistributed here.
