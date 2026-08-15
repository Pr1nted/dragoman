# Getting Started

## Build

CMake 3.16 and a C++17 compiler. Nothing else — miniz, stb and nlohmann/json
are vendored.

```bash
git clone https://github.com/Pr1nted/dragoman
cd dragoman
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That gives you `build/dragoman` (the command line tool), `libdragoman.a`, and a
shared `libdragoman.{so,dylib,dll}` for the bindings.

Check it works:

```bash
ctest --test-dir build --output-on-failure
./build/dragoman version
```

## Convert a map

Dragoman works out what a file is by looking inside it, so you rarely need to
say. With no `--to`, it converts to whichever format the input is not.

**Open Doctrines → Greater Diplomacy 5.** The output is a directory, so put it
where GD5 looks for maps:

```bash
dragoman convert 1914.odmap "/path/to/GreaterDiplomacy5/base_maps/1914"
```

It will appear in GD5's map list under that folder name.

**Greater Diplomacy 5 → Open Doctrines.** The output is a single `.odmap`
archive. Drop it in `data/custom_maps/` and it shows up in the map browser:

```bash
dragoman convert "/path/to/GreaterDiplomacy5/base_maps/GD4" \
                 "/path/to/OpenDoctrines/data/custom_maps/GD4.odmap"
```

### Convert into the GD5 folder, not next to it

Research levels are read from GD5's own tech tree
(`data/json/research_template.json`) in the installation you are writing into.
Converting somewhere else still works, but every nation arrives with no research
and `gd5.research` tells you so.

## Read what it tells you

Every conversion prints what it did and what it could not do exactly:

```
$ dragoman convert map.odmap "base_maps/Dragoman World"
  warning gd5.relations: non-aggression pacts and guarantees have no GD5
          counterpart and were not written into the map; they are preserved
          in the sidecar and return intact
  note    gd5.ocean: divided the water into 917 sea provinces ...
  note    gd5.research: gave 185 nation(s) the research level GD5's own tech
          tree puts at 2000, across 35 technologies ...
```

A **note** is something it did for you. A **warning** is something it could not
do exactly, and always says what happened instead. Every message has a stable
code — see [Diagnostics](Diagnostics.md).

Add `--quiet` for warnings only, `--strict` to make any warning a failure (for
scripts and CI).

## Check nothing was lost

```bash
dragoman roundtrip 1914.odmap
```

Converts to the other format and back, and compares. See
[Round Trips](Round-Trips.md) for exactly what it checks.

To check a whole folder of maps at once:

```bash
python3 tools/conformance.py ~/games
```

## Look inside a map

```bash
dragoman inspect 1914.odmap            # summary
dragoman inspect 1914.odmap --json     # the whole interchange model
dragoman detect some-file              # odmap, gd5, or unknown
```

## Options

| Flag | What it does |
|---|---|
| `--to odmap` / `--to gd5` | Force the target format |
| `--no-sidecar` | Do not carry what the target cannot hold. Smaller, and no longer reversible |
| `--no-ocean` | Do not invent sea provinces for a game whose maps leave water unpainted |
| `--no-geometry` | Do not compute adjacency, centres or the land/sea mask |
| `--no-scripts` | Do not translate scripts or scripted events |
| `--strict` | Treat any warning as a failure |
| `--quiet` | Print warnings and errors only |
