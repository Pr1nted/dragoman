# The interchange model

Neither game's layout is used as the pivot. Open Doctrines stores a map as a
dozen parallel JSON objects keyed by province id; GD5 stores one object per
province with everything inline. Translating directly between them would need a
function per pair of fields, and would double again the day a third game
appeared. The model is instead the union of what the two describe, and each
format owns exactly one reader and one writer against it.

The rule for a field: it lives in the model if both games have a concept for it,
even under different names. If only one does, it rides in `extra` and, on the
way out, in the sidecar.

This is the schema `dg_world_to_json` emits and `dg_world_from_json` accepts.
It is versioned with the ABI: fields may be added, and existing ones do not
change meaning without `DRAGOMAN_ABI_VERSION` moving.

```jsonc
{
  "schema": "dragoman/world/1",
  "name": "The Powder Keg",
  "description": "…",
  "author": "OpenDoctrines",
  "license": "CC-BY-4.0",
  "origin": "odmap",                  // "odmap" | "gd5" | "unknown"
  "date": { "year": 1914, "month": 7, "day": 1, "turn": 0, "ad": true },

  // The pixels are not here — see docs/abi.md. The digest is FNV-1a over the
  // id stream, enough to tell two rasters apart.
  "raster": { "width": 8192, "height": 4096, "pixels": 33554432, "digest": 12345 },

  "provinces": [{
    "id": 3,
    "name": "Bohemia",
    "owner": "AUH",                   // a nation key, or "" for unowned
    "is_sea": false,
    "is_coastal": false,              // derived from the raster
    "terrain": "plains",
    "population": 681010,
    "industry": 0,
    "fortification": 2,
    "port_level": 0,
    "center": [4609, 1639],           // derived; absent if unknown
    "neighbors": [2, 4],              // derived; absent if unknown
    "cores": ["GER"],                 // absent if empty
    "resources": { "gold": { "a": 9.6, "b": 3.8 } },
    "extra": { }                      // fields only one game has
  }],

  "nations": [{
    "key": "AUH",                     // ISO 3166 alpha-3; the identity
    "name": "Austria-Hungary",        // display name
    "adjective": "",
    "color": "#c6a44b",
    "treasury": 57.21,
    "leader_name": "", "leader_title": "",
    "playable": true,
    "political_axis": 70,               // -100 libertarian .. +100 authoritarian;
                                       // omitted when the map did not say. The
                                       // SIGN follows Open Doctrines' compass
                                       // FILE and GD5's political_value, not
                                       // Open Doctrines' in-memory compass,
                                       // which is their negation. See mapping.md
    "flag": "flags/AUH.png",
    "flag_bytes": 510,                // size only; the bytes stay in the library
    "claims": [1005, 1006],
    "relations": { "GER": { "ally": true } },
    "extra": { }
  }],

  "events": [{                        // the declarative shape; see scripting.md
    "name": "Barbarossa",
    "owner": "GER",
    "trigger": "ai",                  // "ai" | "player" | "both"
    "fire_once": true,
    "conditions": [{ "kind": "turn", "op": ">=", "value": "24",
                     "subject": "", "chain": "AND" }],
    "actions": [{ "kind": "declare_war", "target": "RUS", "message": "" }]
  }],

  "scripts": [{                       // the imperative shape, as written
    "name": "opening.txt",
    "entrypoint": true,
    "text": "#OD/MapEngine/1\n…"
  }],

  "sidecar": { },                     // carried payloads, by origin
  "sidecar_blobs": { "od/thumb.png": 29807 }   // name → size
}
```

## Notes on particular fields

**`owner` and `key`.** Provinces refer to nations by `key`, never by name. The
key is an ISO alpha-3 code; for maps that came from GD5 it may be one Dragoman
invented, in which case the pairing is recorded in the sidecar and reused. See
[mapping.md](mapping.md#a-nations-identity-is-not-its-name).

**`extra`.** Namespaced by origin: keys beginning `od` hold Open Doctrines
facts (`od_minorities`, `od_compass`, `od_industry`), keys beginning `gd5` hold
GD5 facts. `garrison` is shared, holding both a headcount and the full GD5
division record.

**`center` and `neighbors`** are absent when the source did not state them and
derivation was switched off. They are functions of the raster, not assertions
the map makes, which is why `dg_roundtrip_check` ignores them.

**Ordering is canonical.** Provinces are sorted by id, nations by key, cores
alphabetically, and events stably by owner. Neither format promises an order —
Open Doctrines keys by id in a JSON object and GD5 by a colour tuple — so
without this the same map read through the two readers produces two orderings.
