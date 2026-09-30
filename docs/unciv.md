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

## Not yet verified in the game

The file is well-formed, the grid matches Unciv's arithmetic, and only Unciv's
own terrain names are written. Nobody has opened one in Unciv. Until somebody
does, "playable" is a claim about the file and not about the experience.
