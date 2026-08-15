# What crosses, what is derived, and what is carried

Every field either has a counterpart on the other side, is computed from
something that does, or is written into the sidecar. This file says which, for
all of them, and why in the cases where the answer is not obvious.

Three words are used precisely throughout:

- **mapped** — both games have the field; it is translated.
- **derived** — one game stores it and the other computes it at load; Dragoman
  computes it too rather than leaving it empty.
- **carried** — only one game has it. It is written into the sidecar
  (`dragoman_sidecar/` in a GD5 map, `dragoman/` inside a `.odmap`), which both
  loaders ignore, and restored on the return trip.

## The container

| Open Doctrines | Greater Diplomacy 5 |
|---|---|
| one zip archive, `.odmap` | one directory |
| `provinces.png`, `land_sea.png` | `id_map.png`, `terrain.png`, `political.png`, `cores.png` |
| ~15 JSON files keyed by province id or ISO code | `map_data.json`, `meta.json`, `history.json` |
| `scripts/*.txt` | `scripted_events` on each nation in `meta.json` |

Both loaders read a fixed list of names. Open Doctrines' is the `needed[]` array
in `Game_Loading.cpp` plus a scan for the `scripts/`, `licenses/`, `symbols/`
prefixes; GD5's is a handful of `os.path.join(load_path, …)` calls. Neither
looks at anything else, which is the property the sidecar depends on.

## Province identity

This is the part that has to be exact, and is.

Both games paint one province id per pixel and disagree only about how a pixel
spells the id:

| | packing | province id 1 |
|---|---|---|
| Open Doctrines | big-endian: `id = (R << 16) \| (G << 8) \| B` | `#000001` |
| Greater Diplomacy 5 | little-endian: `id = R \| (G << 8) \| (B << 16)` | `(1, 0, 0)` |

Checked against every province of every shipped map on both sides — 1248 of
Open Doctrines' and 2523 of GD5's — with no exception. The conversion between
the two rasters is therefore an exact swap of the red and blue channels, and
**no province is ever renumbered**. GD5 keys its province table by the string
Python prints for the colour tuple, `"(1, 0, 0)"`, spaces included.

## Provinces

| Concept | Open Doctrines | GD5 | How |
|---|---|---|---|
| id | `provinces.json` key | `map_data.json` `id` | mapped |
| name | `provinces.json` `name` | `name` | mapped |
| owner | `iso_a3` → `countries.json` | `owner`, a nation_data key | mapped |
| cores | — | `cores` | carried |
| claims | `claims.json` (per nation) | `claims` (per nation) | mapped |
| population | `population.json` | — | carried |
| industry | `resources.json` `industry.level` | `buildings` (factories counted) | mapped, approximately |
| fortification | `resources.json` | — | carried |
| port | `ports.json` | — | carried |
| resources | five fixed deposits, each a surface and reserve figure | free-form named quantities | carried |
| garrison | `armies.json` — a headcount | `units` — typed divisions | mapped, approximately |
| terrain | — (land/sea raster only) | `terrain`, 13-colour palette | derived + carried |
| neighbours | — (derived at load) | `neighbors` | **derived** |
| centre | — (derived at load) | `center` | **derived** |
| coastal | — | `is_coastal` | derived |

### Claims are not cores

GD5 stores both: a nation's `claims` and a province's `cores`. They mean
different things — what a country wants, and what it already considers its own.
Open Doctrines has only the first. Treating them as one fact invented cores
nobody had declared *and* discarded claims that happened not to be cores, so
they are kept separate: claims are mapped, cores are carried.

### Armies

Open Doctrines counts an army in people — 1,174,826 of them in one province of
the 1914 map. GD5 counts a division in hit points, 1200 for the infantry of the
same year. Dragoman uses 1000 men per point so that an army which crosses looks
like an army; the exact original figure and the division's full record (type,
attack, custom name, standing order) ride in the sidecar, so a round trip
restores them exactly and nothing is actually decided by that constant.

### The sea

The two games disagree about what the sea *is*. GD5 divides water into
provinces, gives them to a nation called Ocean, and sails fleets between them.
Open Doctrines does not put provinces in the water at all — its 1914 map has
1247 provinces and every one is land — and moves ships by latitude and longitude
over a land/sea mask.

So Dragoman draws them. When a map's water is not already made of provinces,
converting to GD5 **grows** the sea into provinces rather than cutting it:
seeds are laid on a lattice, nudged off it by a hash of their own coordinates
so the result is not a grid but is still the same every run, snapped to the
nearest water, and then grown outwards all at once through water only. Each
province is the water nearest one seed, which follows the coast instead of
being laid over it — GD5's own hand-drawn sea provinces score 0.66 on
bounding-box fill, a grid scores 1.00, and these score 0.56.

Growing through water also settles what a grid could only approximate: a
province cannot cross land, so the Mediterranean and the Atlantic are separate
however close two seeds fall, and no fleet steps over an isthmus. Water no seed
reached becomes a province per connected piece, above a minimum size — without
that floor the one- and two-pixel scraps in river mouths turned 917 sea
provinces into 1362, six hundred of which nothing could ever enter.

The world map gets 917 of them: one network of 747 that is every ocean joined
together, and 163 lakes. They are owned by `Ocean`, which is added to
the nation roster alongside — every GD5 map has that entry and no Open
Doctrines map does.

These provinces are an invention, not a translation, so their ids go into the
sidecar and the crossing back deletes exactly them: an Open Doctrines map that
has been to GD5 and returned has the provinces it started with. A sea province
the map maker has since drawn in GD5's own editor is a real one and stays.
`--no-ocean` turns the whole thing off, and then `gd5.nosea` warns instead.

Ship positions are carried and return intact regardless.

Open Doctrines places ships on an equirectangular full-globe projection —
`lon = x/w*360 − 180`, `lat = 90 − y/h*180` — which is how a fleet's position
resolves to a province and back. GD5's maps are not equirectangular and not
2:1, so a GD5 map converted to Open Doctrines renders correctly but its
latitudes and longitudes are nominal.

### An unpainted pixel means opposite things

This one is worth stating on its own, because getting it wrong is invisible
until somebody looks at the map.

| | a pixel with province id 0 means |
|---|---|
| Open Doctrines | **water.** 67% of its 1914 raster is blank and its land mask agrees to within 0.0001%: province coverage *is* the landmass, and every land pixel belongs to a province. |
| Greater Diplomacy 5 | **not painted yet.** Its map painter samples every third pixel, so the border between any two provinces is left blank — 12% of its world map, 37% of its 1914 scenario. |

Carried across unchanged, every GD5 provincial border becomes a sea channel
three pixels wide, and the continents arrive in Open Doctrines shot through
with water. So gaps are filled from the nearest province before an Open
Doctrines map is written — a breadth-first expansion from every painted pixel
at once, so each gap is claimed by the province actually nearest it — and the
mask of what was filled goes into the sidecar, so the crossing back restores
the borders exactly and the round trip still holds.

Which meaning a raster carries is decided by counting rather than by trusting
the source: if most of the map's water is covered by sea provinces then the
game that drew it paints its oceans and what is left blank is border. If the
water is mostly blank then the blanks *are* the water. The fill is also bounded
to eight pixels as a backstop, so even a misread cannot march a province colour
out across the Atlantic.

The effect on GD5's 1914 scenario: land goes from 20.3% of the map (province
interiors only) to 24.4% once borders are closed, with zero unpainted pixels
left inside the landmass — the same invariant Open Doctrines' own maps hold.

## Nations

| Concept | Open Doctrines | GD5 | How |
|---|---|---|---|
| identity | ISO 3166 alpha-3 | the key in `nation_data` | mapped, see below |
| display name | `countries.json` `name` | `name` field | mapped |
| numeric id | `countries.json` `id` | — | preserved |
| colour | `#rrggbb` | `[r, g, b]` | mapped |
| treasury | `treasury` | `materials` | mapped |
| leader, adjective | — | `leader_name`, `leader_title`, `adjective` | carried |
| flag | a PNG path into the archive | raw 60x40 pixels, base64, in `flag_data` | mapped, see below |
| alliance / war | `relations.json` | `allied_with`, `at_war_with` | mapped |
| non-aggression, guarantee | `relations.json` | — | carried |
| research, faction, manpower, fuel | — | `research`, `faction`, … | carried |

### Treasury and materials are not the same number

Both mean "what this country has to spend", and they are scaled and seeded
differently. GD5 starts its nations with an empty stockpile and lets them
accumulate — its 1914 scenario gives 765 of 766 nations nothing at all — while
Open Doctrines expects a starting endowment, with a median of 10 and a maximum
of 712 on its own 1914 map.

Translated faithfully, zero stays zero, and a converted map bankrupts its entire
world on the first simulated turn. Dragoman reports this as `od.treasury` rather
than inventing a number and calling it a translation: the remedy is to set
starting treasuries in Open Doctrines' map editor, and only a person who knows
what the scenario is for can choose them.

### A flag is not a file in GD5

`flag_data` is base64 of **raw pixel bytes at exactly 60x40**, handed straight
to `pygame.image.fromstring` — RGBA when the payload is 9600 bytes, RGB when it
is 7200. It is not a PNG. Give it base64 of one and `fromstring` raises,
`decode_b64_to_surf` swallows the exception, and the nation is drawn as a blank
white rectangle.

So an Open Doctrines flag is decoded, resampled to 60x40 and written as raw
pixels. That is lossy in one direction — 60x40 is smaller than the flags Open
Doctrines ships — so the original image is kept in the sidecar and preferred on
the way home; without it, every crossing would permanently shrink a nation's
flag to GD5's icon size.

### A nation's identity is not its name

GD5 identifies a nation by the key it sits under in `nation_data`. Every
province owner, every core and every entry in `at_war_with` is that key — and
the key and the `name` field are allowed to disagree. GD5's own 1914 scenario
has two *separate* nations whose `name` field both read "German Empire".
Treating the name as the identity merges them and hands 121 provinces to
nobody, which is exactly what an earlier version of this library did.

Crossing to Open Doctrines needs an ISO code, which GD5 does not have. The
order is:

1. the code agreed on an earlier crossing, from the sidecar;
2. `src/IsoTable.inc`, 252 name → code pairs generated from the `countries.json`
   of every map Open Doctrines ships — the only authority on the codes it uses
   for nations ISO never registered (`AUH`, `OTT`, `SOV`);
3. failing both, a code invented from the name's initials, checked for
   uniqueness against every code already handed out, and then recorded in the
   sidecar so it is reused rather than reinvented.

Step 3's uniqueness check covers codes from steps 1 and 2 as well. GD5's world
map names 556 nations, and the lookup table will cheerfully give two of them the
same code; without the check the second overwrote the first.

`UNC` is a real country in both games, not a blank: Open Doctrines gives 104
provinces of its 1914 map to a nation whose ISO code is `UNC` and whose name is,
exactly, "Unclaimed". GD5 spells the same idea `"Unclaimed"` in `nation_data`.
They map onto each other; neither is treated as "no owner".

## The map itself

| Concept | Open Doctrines | GD5 | How |
|---|---|---|---|
| name, description, author, licence | `metadata.json` | — | carried |
| date | `"July 1914 AD"`, month resolution | `{day, month, year}`, month 0-based | mapped (day and turn carried) |
| scenario settings | — | `scenario_settings` | carried |
| history log | — | `history.json` | carried |

## Layers written

Converting **to GD5** writes `id_map.png`, `political.png` and `cores.png` from
the raster and the owner colours. `terrain.png` is passed through unchanged when
the map came from GD5 — it may hold shading the 13-colour palette does not name
— and generated from per-province terrain otherwise.

GD5's political and cores layers are not plain pictures: water is painted
`(255, 0, 255)`, which its renderer then sets as the surface's colorkey so the
terrain beneath shows through (`COLOR_CHROMA_PINK` in `data/constants.py`).
Dragoman writes that key rather than an ordinary blue, or GD5 gets an opaque
ocean pasted over its own map; and a nation whose colour is exactly the key is
nudged one step off it, the same guard `map_utils.avoid_chroma()` applies on
GD5's own side. Its stored `political.png` is otherwise a cache — shaded to
70% and regenerated by `refresh_political_map` — so it is written for
correctness rather than treated as authoritative on read.

Converting **to Open Doctrines** writes `provinces.png` and `land_sea.png`.
Land is written as 200 and sea as 40, because the game's only question of that
layer is whether the red channel exceeds 128, and those are the values its own
editor saves. `political.png` is not written at all: Open Doctrines rebuilds it
at load, and its own packer stopped shipping it for the same reason.
