# Dragoman

Move a map between **Open Doctrines** and **Greater Diplomacy 5**, in either
direction, and get it back unchanged if you send it home again.

A *dragoman* was the interpreter attached to an embassy — the person through
whom two powers who shared no language could still sign something.

```bash
pip install dragoman

dragoman convert 1914.odmap "base_maps/1914" --to gd5
dragoman convert "base_maps/GD4" gd4.odmap    --to odmap
dragoman roundtrip 1914.odmap                 # prove nothing was lost
```

## Start here

| | |
|---|---|
| [Installing](Installing.md) | `pip install dragoman`, a release binary, or from source |
| [Getting Started](Getting-Started.md) | Convert your first map, and check the result |
| [What Crosses](What-Crosses.md) | Which fields translate, which are computed, which ride along |
| [Round Trips](Round-Trips.md) | What "lossless" means here, and what it does not |
| [Scripts and Events](Scripts-and-Events.md) | The two scripting systems, and where they meet |
| [Languages](Languages.md) | C, C++, Python, and anything else with an FFI |
| [Diagnostics](Diagnostics.md) | Every message the converter can print, and what to do |
| [Troubleshooting](Troubleshooting.md) | When something did not work |

## What it is

The two games describe overlapping but unequal worlds. Open Doctrines models a
province's population, its port, its ethnic minorities and its position on a
political compass. Greater Diplomacy 5 models terrain, province adjacency, unit
rosters, research and factions. Neither is missing anything it wants — they are
different games — but a map crossing between them would be quietly poorer each
way if nothing wrote the difference down.

Dragoman writes it down. Everything the destination cannot hold goes into a
**sidecar** beside the map, which both games ignore and neither notices. So a
one-way conversion is as faithful as the target format allows, and a return trip
restores what the target could not hold.

It also fills in what the destination *needs* and the source never stored:
province adjacency and centroids, a land/sea mask, sea provinces for a game that
draws none, and research levels appropriate to the map's own date.

## Status

Every map both games ship converts and comes home with nothing lost.

| Suite | Result |
|---|---|
| Greater Diplomacy 5 maps (12 base + 16 scenarios) | **28 / 28** |
| Open Doctrines maps (5 scenarios + the world map) | **6 / 6** |
| Unit tests | **5 / 5** |

Converted maps have been **played in both games**, not merely loaded: through
GD5's own `Controller` and `Map` state with all 23 screens, and through
`OpenDoctrines --simulate` for five AI turns. Most of what this library gets
right, it gets right because somebody opened the result in the game and looked
at it.

## Two things it cannot do

- **Treasuries.** GD5 starts its nations with an empty stockpile; Open Doctrines
  expects a starting endowment. A GD5 map converted to Open Doctrines will
  bankrupt its world on the first turn until you set them. Reported as
  `od.treasury`.
- **Research, going to Open Doctrines.** A `.odmap` has nowhere to put it. See
  [What Crosses](What-Crosses.md#research).
