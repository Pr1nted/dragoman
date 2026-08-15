# Scripts and Events

The two scripting systems are not the same kind of thing.

**Open Doctrines** (`#OD/MapEngine/1`) is imperative and line-based. A script
runs top to bottom when the map loads; `waitUntil` suspends it until a condition
holds. So a script is really a sequence of stages, each gated by one comparison.
It also has `if`/`else`, `foreach`, `while`, arrays and `include`.

**Greater Diplomacy 5** is declarative. Each nation owns a list of events; each
event is a set of conditions joined by `AND`/`OR`/`XOR`/`NOR`/`NAND` and a set
of actions, tested once a turn.

The overlap is the shape both can express: **a gate, and things that happen when
it opens.** Everything outside it is reported by name and carried unchanged
rather than half-translated.

## Open Doctrines → GD5

Each top-level `waitUntil` starts a new stage, and each stage becomes one event.

```
#OD/MapEngine/1
set country.GER.treasury 5000          ← stage 0, fires at load
waitUntil map.turn >= 12
set country.GER.at_war_with RUS true   ← Turn Number >= 12, Declare War
set province.42.owner GER              ← and Give Territory, same event
```

The event's owner is inferred from the `set country.X.…` lines it contains.

| Open Doctrines | GD5 condition |
|---|---|
| `map.turn <op> N` | `Turn Number` |
| `var.NAME <op> V` | `Variable` |
| `country.X.at_war_with Y` | `At War With` |
| `country.X.allied_with Y` | `In Faction With` |

| Open Doctrines | GD5 action |
|---|---|
| `set country.X.at_war_with Y true` / `false` | `Declare War` / `Send Ceasefire` |
| `set country.X.allied_with Y true` | `Join Faction` |
| `set province.N.owner ISO` | `Give Territory` |
| `set var.NAME V` | `Set Variable` |
| `set country.X.name "…"` | `Edit Name` |

**Not translated:** `foreach`, `while`, `if`/`else`, `array`, `list`. GD5's
event system has no control flow, so a loop over a country's provinces has no
honest rendering as an event and guessing one would silently change what the map
does. Reported as `script.unsupported`; the script itself survives in the
sidecar and returns intact.

`set country.X.treasury`, `set province.N.population` and `set map.date` have no
GD5 action and are reported as `script.action`.

## GD5 → Open Doctrines

Each event becomes one entry script.

```
#OD/MapEngine/1
# Barbarossa
# Translated from a Greater Diplomacy 5 scripted event owned by GER.
waitUntil map.turn >= 24
set country.GER.at_war_with RUS true
```

Consecutive `waitUntil`s are gates in sequence, which is an `AND` over time, so
`AND` chains translate exactly. Open Doctrines has no boolean operators at all,
so `OR`, `XOR`, `NOR` and `NAND` cannot be expressed: the first condition becomes
the gate, the rest are written into the script as comments, and `script.chain`
says so. Actions with no counterpart (`Spawn Unit`, `Queue Claims`, `Edit Color`)
become comments under `script.action`.

## Unknown event types pass through

GD5 has around thirty condition types and twenty action types, and gains more.
Any this library does not model is read under its own name with a `gd5:` prefix
and written straight back out unchanged.

So **GD5 → Open Doctrines → GD5 is lossless for every event type**, including
ones added to the game after this was written. Only the Open Doctrines side has
to understand a condition to express it; the GD5 side only has to carry it.

## Libraries never run on their own

A file whose first non-blank line is `#OD/MapEngine/1` is an entry point. A file
without it is a library, reached only through `include`. Dragoman never turns a
library into an event — doing so would start running code that was only ever
meant to be included.

## Turning it off

`--no-scripts` leaves scripts and events alone entirely. They are still carried,
so nothing is lost; they simply are not translated into the other shape.
