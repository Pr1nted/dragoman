# Unciv

A different kind of map from the other two, and the difference decides the
design.

| | geometry | a "place" is |
|---|---|---|
| Open Doctrines, Greater Diplomacy 5 | pixel raster | a province — an arbitrary region of many pixels |
| Unciv | hex grid | one tile |

So this is not a field mapping. It is a **resampling**, and a resampling loses
information however carefully it is done.

## What crosses

`dragoman convert world.odmap out.json --to unciv`

The raster is sampled onto an 80×50 hex grid — Unciv's largest rectangular
size. Each hex reads the province under its **centre** rather than taking a
majority vote of the pixels it covers. A vote sounds better and is worse: it
dissolves every province narrower than the sample step, which on a world map is
most islands and most of Europe.

Terrain is mapped from the model's thirteen tokens, which are GD5's:

| model | Unciv base | feature |
|---|---|---|
| ocean | Ocean | |
| coastal_sea | Coast | |
| inland_sea, lakes | Lakes | |
| mountain | Mountain | |
| hills | Plains | Hill |
| desert | Desert | |
| plains | Plains | |
| forest | Grassland | Forest |
| jungle | Plains | Jungle |
| swamp | Grassland | Marsh |
| tundra | Tundra | |
| frozen | Snow | |

**Hill, Forest, Jungle and Marsh are FEATURES, not base terrains.** Unciv has
six land bases — Grassland, Plains, Tundra, Desert, Mountain, Snow. Writing
`Hill` as a base gives a map whose ruleset validation fails and which the game
will not start.

## Choosing the grid size

`dg_convert` writes 80x50, Unciv's "Huge" -- the largest a world has any chance
of surviving into. For any other size:

```c
dg_convert_unciv(in, out, 40, 25, &opts, &report);
```

A separate CALL rather than a field on `dg_options`, and that is an ABI
decision. `dg_options` is allocated by the caller, and every binding declares
its six ints; a seventh field would have them hand over a struct smaller than
the library reads, and the library would read past the end of it. Adding a
function breaks nobody -- an older caller simply never calls it.

Sizes are clamped to 4..200 rather than refused: below four hexes there is no
world to speak of, and above two hundred no build of the game will open it.
`dg_convert_unciv(in, out, 1, 1, ...)` gives 4x4; `999, 999` gives 200x200.

The smaller the grid, the more of the world disappears -- at 24x15 a hex covers
roughly 340x270 source pixels, and the British Isles are gone.

## Terrain that is invented, and says so

Open Doctrines stores no terrain at all: its raster says land or sea and nothing
else. A map from there would arrive as an undifferentiated grassland world —
playable in the sense that it loads, and dull in every other sense.

So a climate is **invented from latitude**, which is the one thing an
equirectangular world map does tell us: snow above 75°, tundra to 60°, plains,
a desert band at the horse latitudes, grassland and jungle at the tropics. The
conversion reports this as `unciv.terrain` and names it as invention, because
no source field is being translated.

An equirectangular projection over-represents the poles, so a world map comes
out with more snow than Earth has. That is the projection, not the mapping.

## The grid is Unciv's, and it is worth saying why

`HexMath.getTileCoordsFromColumnRow` is transcribed from Unciv's Kotlin, not
derived:

```kotlin
var twoRows = row * 2
if (abs(column) % 2 == 1) twoRows += 1
return HexCoord.of((twoRows - column) / 2, (twoRows + column) / 2)
```

The first version of this was written from memory of how axial coordinates
usually work. It produced a file that parsed, had exactly 4000 tiles, had no
two tiles on the same hex, and agreed with Unciv **nowhere** — a world whose
continents the game would cut apart and scatter, with nothing about the output
looking wrong. `tests/test_unciv.cpp` writes the formula out a second time from
the Kotlin and compares every tile; reinstating the old one fails 3999 of 4000.

## Coming back is not implemented

Reading an Unciv map is refused, out loud, rather than done badly.

Reading is not the mirror of writing. Writing loses the province boundaries;
reading would have to invent them back by merging contiguous same-owner hexes
into regions that never existed in that shape. The result would load, and would
not be the map anybody drew.

What makes the outward direction honest is that the original rides in the
sidecar. Finishing the return trip needs two decisions this library has not
made: where a sidecar lives beside a single JSON file, and what a player's edits
**in Unciv** should mean when they come home — whether they win over the
original, as a fort does, or are refused.

## Validating a map without the game

```
tools/validate_unciv.py out.json /path/to/Unciv/android/assets/jsons/Civ*/Terrains.json
```

It checks the three things a converter gets wrong on its own: a terrain Unciv
does not have, a **feature written as a base terrain** -- which fails the game's
own ruleset validation and stops the map loading -- and tiles that are not where
`HexMath` puts them, which scatters the continents while the file still looks
well formed.

The ruleset is passed in rather than vendored here. A copy in this repository
would drift from the game silently, which is the failure the script exists to
prevent. CI fetches Unciv's own and runs this over every map the suite writes;
if the fetch fails it says so and skips, rather than passing quietly.

## Not yet verified in the game

The file is well-formed, the grid matches Unciv's arithmetic, and only Unciv's
own terrain names are written. Nobody has opened one in Unciv. Until somebody
does, "playable" is a claim about the file and not about the experience.
