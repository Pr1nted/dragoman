# Research

Greater Diplomacy 5 stores research in a map. Open Doctrines does not.

That asymmetry is the whole of this document. Dragoman translates research in
both directions and writes it into `.odmap` archives as `research.json`, but
**Open Doctrines does not read that file yet**, so the field is carried and
inert until the game is changed. Nothing here is required for a round trip —
the exact GD5 table rides in the sidecar and comes back untouched. This is
about making the data *usable* on the Open Doctrines side.

## What the two games have

GD5 gives every nation a table of technologies with integer levels:

```json
"German Empire": { "research": { "factory": 22, "infantry_type": 91, "artillery": 3 } }
```

Each technology has a ceiling (`max_lvl`) declared in the installation's tech
tree, and the ceilings are not uniform — they run from 1 to 101. A level of 22
means nothing until you know whether the ceiling is 25 or 101.

Open Doctrines has 78 named research nodes, built in C++ in
`Game_Research.cpp`. A country either holds a node or does not. Starting
research is assigned by hardcoded tier in `Game::initResearchTrees()`
(`Game_Research.cpp:373`): six great powers get one list, twenty-five middle
powers get a shorter one, everyone else gets `ind1` and `basic_training`. The
tiers are keyed on ISO codes written into the source. A map cannot change them.

## What crosses

Only the numbered ladders, because only they mean the same thing on both
sides. A ladder is treated as a prefix — `ind4` implies `ind1`–`ind4` — which
is how the dependencies in `Game_Research.cpp` already read.

| Open Doctrines | Greater Diplomacy 5 |
| --- | --- |
| `ind1`…`ind10` | `factory`, `fuel_refining`, `resource_refining`, `bergius_process`, `basic_factory` |
| `navy1`…`navy10` | `destroyer`, `submarine`, `aircraft_carrier`, `battleship`, `dreadnought` |
| `arty1`…`arty3` | `artillery` |
| `conscript1`…`conscript6` | `general_recruitment`, `recruitment_buildings`, `basic_recruitment` |
| `basic_training` | `infantry_type`, `militia` (any level at all) |

Progress along a ladder is the furthest any of its technologies has reached as
a fraction of its own ceiling, rounded up — a country with one factory has
industry, and saying it has none is the larger error. Going the other way, each
technology is set to that fraction of its ceiling.

## What does not cross, deliberately

- **`fort1`…`fort6` and `port1`…`port3`.** GD5 has no fortification or port
  technology. Any value would be invented.
- **The named army nodes** — `professional_army`, `combined_arms`, `total_war`
  and the rest. They are not a ladder: they branch, and several sit in mutex
  groups where taking one forecloses another. A prefix of that list would hand
  a country two nodes the game says it may not hold together.

The mapping is lossy in both directions and does not pretend otherwise.

## What `research.json` looks like

ISO alpha-3 to a sorted list of node ids:

```json
{
  "USA": ["arty1", "arty2", "arty3", "basic_training", "conscript1", "ind1", "ind2", "navy1"],
  "AFG": ["basic_training", "ind1"]
}
```

Written by every conversion that produces a `.odmap` and has research to write.
Read back by Dragoman on the next crossing, where it takes precedence over
research derived from the map's date.

### A caveat worth knowing

Converting Open Doctrines → GD5 derives research from the year, and the year is
the same for every nation, so every nation gets the same levels. Convert that
map back and all 185 nations hold identical ladders. That is faithful to what
was written, not a bug in the mapping — but it means a round trip through
Open Doctrines is not a good test of whether the mapping distinguishes nations.
A GD5 scenario with genuinely per-nation research is. Note that several stock
GD5 scenarios ship with research at or near zero (`1939 world` has Germany at
`factory: 0`, `infantry_type: 1`), and those correctly produce almost nothing.

## Making Open Doctrines read it

None of the existing extension points can do this. The Gearbox mod ABI's
`Research.Write` capability exposes `set_country_funding` and nothing else —
it can move money toward research, not grant a node. Applying `research.json`
needs a change to the game.

It is a small one. Two edits, both in files that already do this kind of work.

**1. `Game_Loading.cpp:1726` — add the file to the list the loader extracts.**

```cpp
    const char* needed[] = {"land_sea.png", "provinces.png", "provinces.json", "countries.json",
                            ...
                            "armies.json", "ships.json", "policies.json", "research.json"};
```

**2. `Game_Loading.cpp:1793` onward — stash it beside the other members.**

Alongside the existing `else if (e.name == ...)` branches:

```cpp
        else if (e.name == "research.json") {
            m_pendingResearchJson.assign(static_cast<char*>(e.data), e.size);
        }
```

with `std::string m_pendingResearchJson;` declared in `Game.h`.

**3. `Game_Research.cpp:373` — let the map override the hardcoded tiers.**

At the end of `initResearchTrees()`, after the tier loops have run:

```cpp
    // A map may state research outright. When it does it replaces the tiers
    // above, which are a default for maps that say nothing.
    if (!m_pendingResearchJson.empty()) {
        try {
            auto j = nlohmann::json::parse(m_pendingResearchJson);
            for (auto& [iso, nodes] : j.items()) {
                int cid = findCid(iso);
                if (cid < 0 || !nodes.is_array()) continue;
                m_countryResearched[cid].clear();
                for (auto& nid : nodes)
                    if (nid.is_string()) setResearched(cid, nid.get<std::string>());
            }
        } catch (...) {
            std::cerr << "  Failed to parse research.json" << std::endl;
        }
    }
```

`findCid` is the lambda already defined a few lines above, and `setResearched`
already puts the node where every effect query reads it. Maps without the file
behave exactly as they do today.

Note the ordering: `initResearchTrees()` is called from `Game_Loading.cpp:2491`,
and the loop immediately after it auto-unlocks technologies to match built
industry, forts and ports. That loop will still run and may add nodes on top of
what the map stated. That is probably what you want — a country with level 5
industry should have the technology for it — but it does mean the map's list is
a floor, not an exact set.
