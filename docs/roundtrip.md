# What the round-trip guarantee actually says

`dragoman roundtrip <map>` converts a map to the other format and back, and
succeeds only if nothing was lost. This file says precisely what that covers,
because a guarantee nobody can check is not one.

## The claim

Converting `A → B → A` preserves:

- every province: id, name, owner, population, industry, fortification, port,
  resources, cores, garrison, and everything carried in `extra`;
- every nation: key, display name, colour, treasury, leader, flag, relations
  (including non-aggression pacts and guarantees GD5 cannot store), claims, and
  everything in `extra`;
- every script, every scripted event, and their order;
- the map's name, description, author, licence and date;
- every file carried opaquely — `policies.json`, flag images, the symbol set,
  the thumbnail, GD5's terrain layer and history log;
- the province raster, compared by digest.

## The claim is not

**Byte-identical container files.** Two zip archives holding identical members
are different files: deflate is not required to be reproducible across
implementations, and Open Doctrines packs with Python's zlib while Dragoman
packs with miniz. Every file *inside* the archive is preserved exactly; the
envelope around them is not promised.

## Two deliberate asymmetries

**Derived geometry is excluded.** Province adjacency, centres and coastal flags
are not facts a map states — they are functions of the raster, recomputed
identically by whichever side needs them, and the raster itself is compared.
Including them would fail a round trip merely for having derived, on the way
home, a neighbour list the Open Doctrines map never had to store.

**A round trip may come home richer.** The check is containment, not equality:
every fact in the original must be present and equal in the result, and the
result may hold more. Crossing to GD5 and back picks up that game's
hand-painted terrain layer, which the returning `.odmap` then carries so the
*next* crossing keeps it. That is a gain. Failing the check for it would punish
the library for working. Losing anything still fails.

An empty value in the original asserts nothing, for the same reason: Open
Doctrines has no terrain field, so every province it describes has an empty
terrain, and the map that comes home from GD5 has `"ocean"` and `"plains"` in
those slots. A value that *was* set and came back empty is the opposite case,
and still fails.

## Running it

```bash
dragoman roundtrip 1914.odmap --to gd5
dragoman roundtrip base_maps/GD4 --to odmap
dragoman roundtrip 1914.odmap --no-sidecar   # fails, on purpose
python3 tools/conformance.py ~/games         # every map under a tree
```

From C, `dg_roundtrip_check` returns 1, 0 or −1. From Python,
`dragoman.roundtrip_check(path)` returns `(identical, report)`. A failure names
the field: `the round trip did not preserve provinces -> [1030] -> owner`.

## Turning the sidecar off

`--no-sidecar` writes only what the destination format can express. The
conversion is smaller and entirely usable in the destination game; it simply
stops being reversible. The test suite asserts that this mode *does* fail the
round trip, so the two modes cannot quietly become the same thing.

## Coverage

Every map both games ship: 28 GD5 maps (12 base maps, 16 scenarios) and 6 Open
Doctrines maps. Each of those found bugs a hand-built fixture could not — a
nation named "Unclaimed" that really is a country, two nations sharing a display
name, claims that are not cores, GD5 event types this library has never heard
of. The fixture in `tests/Fixture.h` covers the shapes that remain awkward
(a crescent province, one straddling the wrap seam, a nation with no ISO code)
so the suite still means something without the games installed.
