# Diagnostics

Every message the converter prints has a stable code, so you can act on
`script.unsupported` without matching on English. Severity is one of **note**
(something was done for you), **warning** (something could not be done exactly —
the message always says what happened instead), or **error** (it was not done).

```bash
dragoman convert a.odmap out/          # everything
dragoman convert a.odmap out/ --quiet  # warnings and errors only
dragoman convert a.odmap out/ --strict # any warning is a failure
```

## Reading a map

| Code | | Meaning |
|---|---|---|
| `od.read` | note | How many provinces and nations came out of a `.odmap` |
| `gd5.read` | note | The same, for a GD5 map directory |
| `od.date` | warning | `metadata.json` had no readable `map_date`. Defaults to January 1 AD — set a date in Open Doctrines' Metadata tab |
| `od.badjson` / `gd5.badjson` | warning | A JSON file in the map would not parse and was skipped. The map still converts without it |
| `od.unfilled` | note | Province borders that had been filled in for Open Doctrines were unpainted again, so the raster matches what GD5 originally had |
| `sidecar.restored` | note | Fields with no place in this format were restored from the sidecar |

## Writing a map

| Code | | Meaning |
|---|---|---|
| `od.write` / `gd5.write` | note | How many provinces were written, and where |
| `gd5.derived` | note | Province adjacency and centres were computed from the raster, because Open Doctrines derives both at load |
| `gd5.ocean` | note | The water was divided into sea provinces, or those provinces were removed again on the way back |
| `gd5.research` | note or warning | Nations without research were given the level the map's date implies. As a **warning** it means GD5's tech tree was not found beside the destination — convert into the GD5 installation itself |
| `od.filled` | note | Unpainted pixels were filled from the nearest province, because Open Doctrines reads an unpainted pixel as open water |
| `gd5.nosea` | warning | The map has no sea provinces and ocean synthesis is off. GD5 will load it, but no fleet can move and the sea renders black |
| `gd5.nogeometry` | warning | Adjacency and centres are missing and `--no-geometry` was passed. GD5 cannot move units on the result |
| `gd5.relations` | warning | Non-aggression pacts and guarantees have no GD5 counterpart. Carried in the sidecar; they return intact |
| `gd5.namecollision` | warning | Two nations share a display name. GD5 identifies a nation by name, so the second was renamed |
| `gd5.flag` | warning | A flag image could not be decoded. GD5 draws its default one |
| `od.treasury` | warning | Almost no nation has a treasury. GD5 starts nations with an empty stockpile and Open Doctrines expects an endowment — see below |

### `od.treasury`

The one thing a converted map still needs a person for. GD5's 1914 scenario
gives 765 of its 766 nations nothing at all; Open Doctrines' own 1914 map hands
out a median of 10. Translated faithfully, that zero stays zero and the first
simulated turn bankrupts the world.

Set starting treasuries in Open Doctrines' map editor, or edit `countries.json`
inside the archive. Dragoman will not pick a number for you, because only
somebody who knows what the scenario is for can choose one.

## Scripts

| Code | | Meaning |
|---|---|---|
| `script.translated` | note | How many GD5 events became Open Doctrines entry scripts |
| `script.unsupported` | warning | A script uses loops, conditionals or collections. GD5's events have no control flow, so it was carried unchanged rather than half-translated |
| `script.include` | warning | An `include` was carried but not inlined |
| `script.condition` | warning | A condition with no counterpart on the other side |
| `script.action` | warning | An action with no counterpart. Written into the script as a comment |
| `script.chain` | warning | Conditions chained with something other than `AND`. Open Doctrines has no boolean operators, so only the first became a gate |
| `script.dropped` | warning | How many events could not be written as GD5 events |
| `script.owner` | warning | An event belongs to a nation that is not on this map |

See [Scripts and Events](Scripts-and-Events.md).

## Round trips

| Code | | Meaning |
|---|---|---|
| `roundtrip.ok` | note | The map came home with every modelled field and the raster unchanged |
| `roundtrip.differs` | error | Something was lost. The message names the field |
| `model.badjson` | error | A document handed to `dg_world_from_json` was not a valid interchange model |

## From code

```c
for (int i = 0; i < dg_report_count(report); ++i) {
    printf("%d %s: %s\n", dg_report_severity(report, i),
           dg_report_code(report, i), dg_report_message(report, i));
}
```

```python
for d in report:
    print(d.severity, d.code, d.message)
print(report.warnings, report.errors, report.ok)
```

The report is yours even when the call **failed** — that is usually where the
interesting diagnostics are.
