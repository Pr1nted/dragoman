# Round Trips

`dragoman roundtrip <map>` converts a map to the other format and back, and
fails if anything was lost. This page says exactly what that covers, because a
guarantee nobody can check is not one.

```bash
dragoman roundtrip 1914.odmap                 # to GD5 and back
dragoman roundtrip "base_maps/GD4"            # to .odmap and back
dragoman roundtrip 1914.odmap --no-sidecar    # fails, on purpose
```

## The sidecar

The two formats are not equally expressive, so a straight translation always
loses something. Everything the destination cannot hold is written beside the
map instead:

- `dragoman_sidecar/` in a GD5 map directory
- `dragoman/` inside a `.odmap` archive

This is safe because of how both games load. Open Doctrines reads a fixed
`needed[]` list plus a scan for the `scripts/`, `licenses/` and `symbols/`
prefixes; GD5 reads a handful of specific filenames. Neither looks at anything
else, so the sidecar is invisible to both and costs the converted map nothing
but disk.

`--no-sidecar` writes only what the destination can express. The result is
smaller and entirely playable — it simply stops being reversible. The test suite
asserts that this mode *does* fail the round trip, so the two modes cannot
quietly become the same thing.

## What is preserved

- every province: id, name, owner, population, industry, fortification, port,
  resources, cores, garrison, and anything carried in `extra`
- every nation: key, display name, colour, treasury, leader, flag, relations
  (including the non-aggression pacts and guarantees GD5 cannot store), claims
- every script, every scripted event, and their order
- the map's name, description, author, licence and date
- every file carried opaquely — `policies.json`, flag images, the symbol set,
  the thumbnail, GD5's terrain layer and history log
- the province raster, compared by digest

## What is not promised

**Byte-identical container files.** Two zip archives holding identical members
are different files: deflate is not reproducible across implementations, and
Open Doctrines packs with Python's zlib while Dragoman packs with miniz. Every
file *inside* is preserved exactly; the envelope around them is not.

## Two deliberate asymmetries

**Derived geometry is excluded from the comparison.** Adjacency, centres and
coastal flags are not facts a map states — they are functions of the raster,
recomputed identically by whichever side needs them, and the raster itself is
compared. Including them would fail a round trip for having worked out, on the
way home, a neighbour list the Open Doctrines map never had to store.

**A round trip may come home richer.** The check is containment, not equality:
every fact in the original must be present and equal in the result, and the
result may hold more. Crossing to GD5 and back picks up that game's hand-painted
terrain layer, which the returning `.odmap` then carries so the *next* crossing
keeps it. That is a gain. Losing anything still fails.

## From code

```c
dg_report* report = NULL;
int same = dg_roundtrip_check("1914.odmap", DG_FORMAT_GD5, &opts, &report);
/* 1 identical, 0 something changed, -1 could not be checked */
```

```python
identical, report = dragoman.roundtrip_check("1914.odmap", to="gd5")
```

A failure names the field:

```
error roundtrip.differs: the round trip did not preserve
      provinces -> [1030] -> owner -> value changed
```

## Coverage

Every map both games ship: 28 GD5 maps and 6 Open Doctrines maps. Each of those
found bugs a hand-built fixture could not — a nation named "Unclaimed" that
really is a country, two nations sharing a display name, claims that are not
cores, GD5 event types this library has never heard of.

```bash
python3 tools/conformance.py ~/games
```
