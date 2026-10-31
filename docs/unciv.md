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

```
dragoman convert world.odmap out.json --to unciv       # a map
dragoman convert world.odmap MyMod   --to unciv-mod    # a map and its nations
```

## Two ways to write one, and why there are two

Unciv takes its civilizations from the **ruleset**, never from the map. So a
plain map can only seat a nation that matches one of the two dozen Civ V
ships: on the Modern Day world map that is twelve nations out of a hundred and
twelve that have a seat at all.

`--to unciv-mod` writes an extension mod instead:

```
MyMod/jsons/Nations.json     every nation, as a civilization
MyMod/maps/MyMod.json        the map, declaring the mod it needs
```

Each nation arrives with its own name, its map colour as `outerColor`, a
contrasting `innerColor`, and its provinces as city names. All 112 nations
with a seat then hold a starting location. Drop the folder in Unciv's `mods`
directory.

**The mod's name is its folder's name.** `RulesetCache` does
`modRuleset.name = modFolder.name()` and nothing else, so the map declares the
directory it was written into rather than the world's title. A mod whose map
declares anything else loads against plain vanilla, and every starting
location in it names a civilization the ruleset has never heard of -- which
fails silently, since Unciv is perfectly happy to load such a map.

A province the generator numbered rather than named -- "Afghanistan #1608" --
is not written as a city. Open Doctrines' own world map is entirely of those,
so those nations fall back to a single city named after the country; its
admin_1 maps have real names and get real cities.

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

Three things besides terrain now cross onto the hexes themselves:

| model | Unciv | kept back |
|---|---|---|
| fortification | `improvement: Fort` | the level (Unciv has one fort) |
| gold, gemstones, metal, oil | `resource`, plus `resourceAmount` for the two strategic ones | the exact figures; **rubber entirely** |
| owner | `startingLocations`, for civs the ruleset has | every other nation |

Each is refused rather than forced where Unciv would not accept it. `Fort` has
a `terrainsCanBeBuiltOn` list without mountain, marsh or water on it;
`terrainsCanBeFoundOn` keeps Gems to jungle, forest and hill and Gold Ore off
grassland and plains entirely. On a world map that refuses more deposits than
it places -- 625 against 802 -- which is the honest outcome of a hex grid
sampled from provinces that were not drawn with Civ V's rules in mind.

**Rubber is not substituted.** Civ V vanilla has no rubber, and borrowing Dyes
for it would mean a map that came back had rubber where it had none and none
where it had rubber. It stays in the sidecar, like the climate that is invented
rather than read.

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

## Coming back

The original rides **inside the map file**, under a top-level `dragoman` key.
Unciv's loader is configured `ignoreUnknownFields = true` (`UncivJson.kt`), so
it reads past the key without complaint, and there is no companion file for
anybody to lose.

Reading restores that record and then reads the hexes for what **changed**: any
tile whose base terrain differs from what this library would have written for
the province beneath it is an edit, and the edit wins. An unedited map comes
home exactly as it set out; an edited one comes home with the edits and
everything a hexagon cannot hold — nations, names, scripts, population,
province shapes — intact from the record.

The raster is carried as a PNG rather than rebuilt, because province *shapes*
are precisely what a hex grid destroys.

A hex whose province had **no** terrain is skipped: it was given an invented
climate on the way out, and adopting that back would quietly turn a latitude
guess into map data.

### Edits made in Unciv

Terrain changed in Unciv's editor is adopted over the carried original. So are
a **fort added** and a **deposit added**: a hex that gains `improvement: Fort`
fortifies its province, and one that gains a resource this library translates
gives its province that deposit, with the surface figure squared back out of
`resourceAmount` (a luxury carries no amount, so its quantity is simply
recorded as present rather than invented large).

A fort or deposit **cleared** in Unciv is counted and reported, not adopted. A
province covers several hexes and they all carry its fort; clearing one of them
says a hex changed, not that the province is unfortified. The asymmetry is
deliberate and `unciv.notadopted` says how much it passed over.

A starting location moved in Unciv is not adopted either, for a simpler reason:
the model has nowhere to put it. Neither game records a seat, so there is no
field for one to come home to.

### The one thing that does not survive

**A save from Unciv's own map editor.** libGDX serialises from the `TileMap`
object, which has no field for this, so `json().toJson(tileMap)` writes a file
without the record. Loading is safe; re-saving strips it.

There is no way round that from this side. `TileMap.description` is the only
free-text field Unciv round-trips, and it holds a short marker saying where the
map came from — a megabyte of base64 there would be unreadable in the editor
and probably unusable.

So a stripped map is **refused, and named**, rather than answered with province
boundaries invented by merging same-owner hexes into regions that never
existed:

```
unciv.stripped: this map carries no dragoman record, so it has been re-saved by
Unciv itself ... only the terrain grid survives
```

## Verified by the game itself

Unciv is open source and ships a jar, so the questions below were answered by
running its own code headlessly rather than by reading it and guessing:

| question | answer |
|---|---|
| does plain, ungzipped JSON load? | **yes** — `mapFromSavedString` tries unzip and falls back |
| does an unknown top-level key survive a re-save? | **no** — 5.18 MB in, 208 KB out, record gone |
| does `description` survive a re-save? | **yes** |
| does the map pass `TileMap.setTransients`? | **yes**, against the real 29-terrain ruleset |

`tools/unciv/ValidateWithUnciv.java` is that check, and CI runs it over every
map the suite writes. Two traps if you run it by hand:

- Gdx's `HeadlessFiles` resolves `internal` paths against the **working
  directory**, not the classpath, so the jar's `jsons/` must be extracted
  first. Without that the rulesets load with **zero terrains** and every map
  fails with `Terrain Ocean does not exist in ruleset!` — which reads exactly
  like a broken map and is nothing of the kind.
- `getVanillaRuleset()` returned an empty ruleset here; the loop picks whichever
  cached ruleset actually has terrains.

It earned its place immediately: `description` was being written onto
`mapParameters`, which has no such field, so Unciv read it back as an empty
string. Nothing in the file looked wrong.

## Still not verified

The map loads, and the engine accepts it. Nobody has **played** one. Two things
remain open, and both need the game with a screen in front of it:

- **Starting locations.** These maps now carry them for the nations Civ V has
  a civilization for -- about twenty on a world map, seated on each nation's
  most populous hex, since neither game records a capital. The remaining
  hundred-odd nations still have none and Unciv places those civs itself, as it
  did for every map before. `ValidateWithUnciv` reports the count the game
  actually read, and a game starts from them; whether the *seats* are good ones
  to play from is still a question only playing answers.
- Whether a world resampled to hexes is any *good* to play, which no check
  here can speak to.
