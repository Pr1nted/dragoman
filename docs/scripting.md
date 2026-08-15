# Translating the two scripting systems

They are not the same kind of thing.

**Open Doctrines** (`#OD/MapEngine/1`) is imperative and line-based. A script
runs top to bottom when the map loads; `waitUntil` suspends it until a condition
holds. So a script is really a sequence of stages, each gated by one comparison.
It also has `if`/`else`, `foreach`, `while`, arrays, linked lists and `include`.

**Greater Diplomacy 5** is declarative. Each nation owns a list of events; each
event is a set of conditions joined by `AND`/`OR`/`XOR`/`NOR`/`NAND` and a set of
actions, tested once per turn.

The overlap is the shape both can express: **a gate, and things that happen when
it opens**. Everything outside it is reported by name and carried unchanged
rather than half-translated.

## Open Doctrines → GD5

Each top-level `waitUntil` starts a new stage, and each stage becomes one event.

```
#OD/MapEngine/1
set country.GER.treasury 5000       ← stage 0, fires at load
waitUntil map.turn >= 12
set country.GER.at_war_with RUS true  ← an event: Turn Number >= 12, Declare War
set province.42.owner GER             ← and Give Territory, in the same event
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

**Not translated**, reported as `script.unsupported` and carried verbatim:
`foreach`, `while`, `if`/`else`, `array`, `list`. GD5's event system has no
control flow, so a loop over a country's provinces has no honest rendering as
an event, and guessing one would silently change what the map does. `include`
is reported as `script.include`: the library is carried but not inlined.

`set country.X.treasury`, `set province.N.population` and `set map.date` have no
GD5 action; they are reported as `script.action` and left out of the event they
appeared in, while the script itself survives in the sidecar.

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
so `OR`, `XOR`, `NOR` and `NAND` cannot be expressed: the first condition
becomes the gate, the rest are written into the script as comments, and
`script.chain` says so. Actions with no counterpart (`Spawn Unit`,
`Queue Claims`, `Edit Color`, `Edit Flag`) become comments too, under
`script.action`.

### Unknown event types pass through

GD5 has around thirty condition types and twenty action types, and gains more.
Any this library does not model is read under its own name with a `gd5:` prefix
and written straight back out unchanged. So **GD5 → Open Doctrines → GD5 is
lossless for every event type**, including ones added to the game after this was
written. Only the Open Doctrines side has to understand a condition to express
it; the GD5 side only has to carry it.

## Libraries

A file whose first non-blank line is `#OD/MapEngine/1` is an entry point and
runs on load. A file without it is a library, only ever reached through
`include`. Dragoman never translates a library into an event: doing so would
start running code that was only meant to be included.

## Diagnostic codes

| Code | Meaning |
|---|---|
| `script.unsupported` | control flow or collections; the script was not translated |
| `script.include` | an `include` was carried but not inlined |
| `script.condition` | a condition with no counterpart on the other side |
| `script.action` | an action with no counterpart; written as a comment |
| `script.chain` | conditions chained with something other than `AND` |
| `script.dropped` | how many events could not be written as GD5 events |
| `script.owner` | an event belonged to a nation not on this map |
| `script.translated` | how many events became Open Doctrines scripts |
