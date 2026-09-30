#!/usr/bin/env python3
"""Check a map this library wrote against Unciv's own ruleset.

    tools/validate_unciv.py <map.json> <Terrains.json>...

NOT a substitute for opening it in the game, and it does not pretend to be. A
map can satisfy every check here and still play badly. What it does catch is
the class of mistake a converter makes on its own:

  - a terrain name Unciv does not have
  - a FEATURE written as a base terrain, which fails the game's own ruleset
    validation and stops the map loading. Hill, Forest, Jungle and Marsh are
    features; the six land bases are Grassland, Plains, Tundra, Desert,
    Mountain and Snow
  - tiles that are not where HexMath.getTileCoordsFromColumnRow puts them,
    which is a world whose continents the game cuts apart while the file
    itself looks perfectly well formed

The ruleset is Unciv's own JSON, passed in rather than vendored here: a copy in
this repository would drift from the game silently, which is the failure this
script exists to prevent.
"""
import json, re, sys, math
from collections import Counter

def load_ruleset(path):
    """Unciv's JSONs carry comments and trailing commas; json refuses both."""
    s = open(path, encoding="utf-8").read()
    # Unciv's rulesets carry BOTH comment styles -- whole entries are commented
    # out with /* */ -- and trailing commas. json refuses all three.
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    s = re.sub(r',(\s*[}\]])', r'\1', s)
    return json.loads(s)

def terrains(path):
    by_type = {}
    for t in load_ruleset(path):
        by_type.setdefault(t.get("type", "?"), set()).add(t["name"])
    return by_type

def unciv_hex(column, row):
    two = row * 2 + (1 if abs(column) % 2 == 1 else 0)
    return ((two - column) // 2, (two + column) // 2)

def check(map_path, ruleset_path, name):
    m = json.load(open(map_path))
    by_type = terrains(ruleset_path)
    bases = by_type.get("Land", set()) | by_type.get("Water", set())
    feats = by_type.get("TerrainFeature", set())

    problems = []
    tiles = m.get("tileList", [])
    if not tiles:
        problems.append("no tileList")

    used_bases, used_feats = Counter(), Counter()
    for t in tiles:
        b = t.get("baseTerrain")
        used_bases[b] += 1
        if b not in bases:
            if b in feats:
                problems.append(f"{b!r} is a TerrainFeature, used as a baseTerrain")
            else:
                problems.append(f"{b!r} is not a terrain in this ruleset")
        for f in t.get("terrainFeatures", []):
            used_feats[f] += 1
            if f not in feats:
                problems.append(f"{f!r} is not a TerrainFeature in this ruleset")

    # the grid, against Unciv's own arithmetic
    params = m.get("mapParameters", {})
    size = params.get("mapSize", {})
    W, H = size.get("width", 0), size.get("height", 0)
    if W * H != len(tiles):
        problems.append(f"mapSize says {W}x{H}={W*H} but there are {len(tiles)} tiles")
    wrong = 0
    for i, t in enumerate(tiles):
        row, col = divmod(i, W) if W else (0, 0)
        want = unciv_hex(col, row)
        got = (t["position"]["x"], t["position"]["y"])
        if want != got:
            wrong += 1
    if wrong:
        problems.append(f"{wrong} of {len(tiles)} tiles are not where HexMath puts them")
    if len({(t['position']['x'], t['position']['y']) for t in tiles}) != len(tiles):
        problems.append("two tiles share a hex")

    print(f"=== {name} ===")
    print(f"  tiles          {len(tiles)}  ({W}x{H})")
    print(f"  base terrains  {dict(sorted(used_bases.items()))}")
    print(f"  features       {dict(sorted(used_feats.items())) or '{}'}")
    print(f"  problems       {len(set(problems))}")
    for p in sorted(set(problems)):
        print(f"    - {p}")
    return len(set(problems))

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    map_path, rulesets = sys.argv[1], sys.argv[2:]
    bad = 0
    for rs in rulesets:
        bad += check(map_path, rs, f"{map_path} against {rs}")
    sys.exit(1 if bad else 0)
