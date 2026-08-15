#!/usr/bin/env python3
"""Regenerate src/IsoTable.inc from Open Doctrines' own maps.

    python3 tools/generate_iso_table.py ~/OpenDoctrines/data/STDmaps > src/IsoTable.inc

Greater Diplomacy 5 identifies a nation by name and Open Doctrines by ISO 3166
alpha-3, so crossing from GD5 means finding a code for a name. For a country
ISO has actually registered that is a lookup anyone could write; for the ones
it has not -- Austria-Hungary, the Ottoman Empire, the German Empire, the
Soviet Union -- the only authority on which code Open Doctrines uses is Open
Doctrines. So the table is generated from the countries.json of every map it
ships rather than typed out here from memory.

Names not in the table get a code invented for them at conversion time, once,
which is then recorded in the map's sidecar and reused on every later
crossing -- so widening this table improves the codes new maps get without
changing the ones existing maps already carry.
"""

import argparse
import json
import sys
import zipfile
from pathlib import Path

HEADER = '''/* name -> ISO 3166 alpha-3, as Open Doctrines itself pairs them.
 *
 * Generated from the countries.json of every map Open Doctrines ships, which
 * is the only authority on which code that project uses for a nation that ISO
 * never assigned one to -- AUH for Austria-Hungary, OTT for the Ottoman
 * Empire. {count} pairs.
 *
 * Regenerate with tools/generate_iso_table.py.
 */
static const struct {{ const char* name; const char* iso; }} kIsoTable[] = {{
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", type=Path, help="a directory of .odmap archives")
    args = parser.parse_args()

    archives = sorted(args.maps.rglob("*.odmap"))
    if not archives:
        sys.exit(f"no .odmap files under {args.maps}")

    pairs = {}
    for archive in archives:
        with zipfile.ZipFile(archive) as zf:
            if "countries.json" not in zf.namelist():
                continue
            for entry in json.loads(zf.read("countries.json")).values():
                name, iso = entry.get("name"), entry.get("iso_a3")
                # First map wins, so the order of the archives decides ties and
                # a rerun produces the same table.
                if name and iso and name not in pairs:
                    pairs[name] = iso

    out = [HEADER.format(count=len(pairs))]
    for name in sorted(pairs):
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    {{"{escaped}", "{pairs[name]}"}},\n')
    out.append("};\n")
    sys.stdout.write("".join(out))
    print(f"{len(pairs)} pairs from {len(archives)} maps", file=sys.stderr)


if __name__ == "__main__":
    main()
