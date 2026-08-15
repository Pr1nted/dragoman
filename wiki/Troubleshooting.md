# Troubleshooting

## The map does not appear in the game

**Greater Diplomacy 5** looks in `base_maps/` and `scenarios/<kind>/`. The
output of a conversion is a *directory*, and the folder name is the map name.

**Open Doctrines** looks in `data/custom_maps/`. A bare `<name>.odmap` dropped
there appears in the browser — that is exactly what the map editor's Export
produces.

Check the file is what you think it is:

```bash
dragoman detect /path/to/thing     # odmap, gd5, or unknown
```

## Every nation is bankrupt on the first turn

Expected, and reported as `od.treasury`. GD5 starts its nations with an empty
stockpile; Open Doctrines expects a starting endowment. Set treasuries in Open
Doctrines' map editor. Dragoman will not invent them — see
[Diagnostics](Diagnostics.md#odtreasury).

## The sea is black in GD5, and no fleet can move

The map has no sea provinces. Open Doctrines does not put provinces in the
water, and GD5 can neither draw nor sail across what is not a province.

This is handled automatically — unless you passed `--no-ocean`, in which case
`gd5.nosea` warns about exactly this. Convert again without it.

## Every nation starts with no research

GD5's tech tree is read from `data/json/research_template.json` in the
installation you are writing into. If you converted somewhere else, there was
nothing to read and `gd5.research` said so as a warning.

Convert directly into the GD5 folder:

```bash
dragoman convert map.odmap "/path/to/GreaterDiplomacy5/base_maps/My Map"
```

## Nation flags are blank white rectangles

You are on 0.1.0. GD5's `flag_data` is raw pixel bytes at 60×40, not a PNG, and
the old version wrote a PNG — `pygame.image.fromstring` raises, the game
swallows it, and you get a white box. Upgrade to 0.2.0 and convert again.

## A round trip says something changed

The message names the field:

```
error roundtrip.differs: the round trip did not preserve
      provinces -> [1030] -> owner -> value changed
```

Check first whether you passed `--no-sidecar`, which makes the conversion
deliberately one-way. If not, it is a bug worth reporting — include the map, the
direction, and that message.

## The Python binding cannot find the library

```
OSError: could not find the dragoman shared library
```

Build it, or point at it:

```bash
cmake -S . -B build && cmake --build build
export DRAGOMAN_LIBRARY=/path/to/libdragoman.dylib
```

## `AttributeError: dlsym(...): symbol not found`

The shared library was built without the symbol exported. It builds and passes
every C++ test in that state — the failure only shows at the first call. Rebuild
from a clean tree; if it persists on your platform, that is a bug.

## Province borders show as channels of sea

Fixed in 0.2.0. GD5 leaves the border between provinces unpainted, and Open
Doctrines reads an unpainted pixel as water, so every provincial boundary became
a three-pixel strait. Upgrade and convert again.

## Two nations merged into one

Fixed in 0.1.0's testing. GD5 identifies a nation by its key in `nation_data`,
not its `name` field, and the two are allowed to disagree — its own 1914
scenario has two separate nations both named "German Empire". If you see this on
0.2.0, report it.

## Getting a useful bug report together

```bash
dragoman version
dragoman inspect <map> --json > model.json
dragoman roundtrip <map> 2>&1 | tail -20
```

Those three, plus the map if you can share it, are enough to reproduce almost
anything.
