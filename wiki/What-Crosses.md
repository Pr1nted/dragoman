# What Crosses

Every field does one of three things.

- **mapped** — both games have it; it is translated.
- **derived** — the destination needs it and the source never stored it, so it
  is computed.
- **carried** — only one game has it. It goes in the sidecar and comes back on
  the return trip. See [Round Trips](Round-Trips.md).

Field-by-field detail, including why each decision was made, is in
[docs/mapping.md](https://github.com/Pr1nted/dragoman/blob/main/docs/mapping.md).
This page is the short version.

## Province identity is exact

Both games paint one province id per pixel and differ only in how a pixel spells
it: Open Doctrines packs it big-endian (`#000001` is province 1), GD5
little-endian (`(1, 0, 0)` is province 1). Verified across every province of
every shipped map on both sides — 1248 and 2523 of them — with no exception.

**No province is ever renumbered.** The two rasters differ by swapping red and
blue, and nothing else.

## Provinces

| | Open Doctrines | GD5 | |
|---|---|---|---|
| owner, name, claims | ✅ | ✅ | mapped |
| population, ports, fortification | ✅ | — | carried |
| minorities, political compass, policies | ✅ | — | carried |
| cores | — | ✅ | carried |
| terrain, neighbours, centres, coastal | — | ✅ | **derived** |
| industry | `resources.json` level | factories in `buildings` | mapped, roughly |
| garrison | a headcount in `armies.json` | typed divisions | mapped, roughly |

Adjacency and centroids are computed from the province raster when converting to
GD5, because Open Doctrines derives both at load and never stores them. The
centroid finder handles crescent-shaped provinces, whose average pixel falls
outside themselves — otherwise GD5 would draw unit counters in the sea.

## Nations

| | Open Doctrines | GD5 | |
|---|---|---|---|
| identity | ISO 3166 alpha-3 | the key in `nation_data` | mapped |
| name, colour, treasury | ✅ | ✅ | mapped |
| flags | a PNG in the archive | raw 60×40 pixels, base64 | mapped |
| alliance, war | ✅ | ✅ | mapped |
| non-aggression, guarantees | ✅ | — | carried |
| leader, adjective, faction, manpower, fuel | — | ✅ | carried |
| research | — | ✅ | **derived**, see below |

**A GD5 nation's identity is its key in `nation_data`, not its `name` field.**
The two are allowed to disagree, and in GD5's own 1914 scenario two separate
nations both call themselves "German Empire".

Crossing to Open Doctrines needs an ISO code, which GD5 does not have. Dragoman
uses, in order: a code agreed on an earlier crossing (from the sidecar); a table
of 252 pairs generated from Open Doctrines' own maps; or one invented from the
name's initials, checked for uniqueness and then recorded so it is reused rather
than reinvented.

## The sea

The two games disagree about what the sea *is*. GD5 divides water into
provinces, gives them to a nation called `Ocean`, and sails fleets between them.
Open Doctrines does not put provinces in the water at all — its 1914 map has
1247 provinces and every one is land — and moves ships by latitude and longitude
over a land/sea mask.

So converting **to GD5** grows the water into provinces: seeds on a lattice,
displaced by a hash of their own coordinates, snapped to the nearest water, then
grown outwards all at once through water only. Each province is the water
nearest one seed, so they follow coastlines rather than being laid over them,
and none can cross land — the Mediterranean and the Atlantic stay separate. The
world map gets 917: one network of 747 that is every ocean joined together, plus
163 lakes.

They are an invention, so their ids go in the sidecar and the return trip
deletes exactly them. A sea province **you** draw in GD5's editor is real and
stays. `--no-ocean` turns the whole thing off.

## An unpainted pixel means opposite things

| | a pixel with province id 0 |
|---|---|
| Open Doctrines | **water.** Province coverage *is* the landmass |
| GD5 | **not painted yet.** Its painter samples every third pixel, so every province border is blank — 37% of its 1914 scenario |

Carried across unchanged, every GD5 provincial border became a sea channel three
pixels wide and the continents arrived shot through with water. Gaps are now
filled from the nearest province before an Open Doctrines map is written, and
the mask of what was filled goes in the sidecar so the return trip restores the
borders exactly.

Which meaning a raster carries is decided by counting, not by trusting the
source. The two populations are nowhere near each other: GD5's maps run 0.35 to
7.8 on the ratio of sea-province pixels to blank ones; Open Doctrines' all sit
at 0.0001.

## Research

Open Doctrines stores **no research in a map at all**. Its tree is built in C++
in `Game_Research.cpp` and each country's starting nodes come from a hardcoded
list of ISO codes.

What both formats carry is the date, and GD5 already knows what to do with one:
its `get_time_appropriate_research(year)` gives each technology a level equal to
the number of its introduction years that have passed. Dragoman applies that
rule, reading the tech tree out of the GD5 installation being written into —
not a copy kept in this library, which would go stale the moment anyone modded a
technology.

A nation that arrives *with* research keeps exactly what it had. Going the other
way there is nowhere to put it, so it rides in the sidecar and returns intact.

## Scripts

See [Scripts and Events](Scripts-and-Events.md).
