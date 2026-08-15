#!/usr/bin/env python3
"""Round-trip every real map under a directory and report what survived.

    python3 tools/conformance.py ~/games

Walks the tree looking for Open Doctrines `.odmap` archives and Greater
Diplomacy 5 map directories, converts each to the other format and back, and
fails if anything was lost. This is the strongest check the project has and
the one its own test suite cannot make on its own: neither game's maps belong
to this repository, so the suite ships a hand-built fixture and this script
covers the real thing whenever someone has the games installed.
"""

import argparse
import subprocess
import sys
from pathlib import Path


def find_binary(root: Path) -> Path:
    for candidate in (root / "build" / "dragoman",
                      root / "build" / "Release" / "dragoman.exe",
                      root / "build" / "Debug" / "dragoman.exe"):
        if candidate.exists():
            return candidate
    sys.exit("dragoman is not built; run: cmake -S . -B build && cmake --build build")


def find_maps(where: Path):
    """Every map under `where`, as (path, target format) pairs."""
    for path in sorted(where.rglob("*.odmap")):
        yield path, "gd5"
    for path in sorted(where.rglob("id_map.png")):
        # A GD5 map is the directory holding the raster and the province table.
        if (path.parent / "map_data.json").exists():
            yield path.parent, "odmap"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="a tree containing maps of either game")
    parser.add_argument("--verbose", action="store_true", help="print each map's diagnostics")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    binary = find_binary(root)

    maps = list(find_maps(args.directory))
    if not maps:
        sys.exit(f"no Open Doctrines or GD5 maps found under {args.directory}")

    passed, failed = 0, []
    for path, target in maps:
        result = subprocess.run(
            [str(binary), "roundtrip", str(path), "--to", target, "--quiet"],
            capture_output=True, text=True)
        name = path.name
        if result.returncode == 0:
            passed += 1
            print(f"  ok   {name} -> {target}")
        else:
            failed.append(name)
            print(f"  FAIL {name} -> {target}")
            for line in (result.stdout + result.stderr).splitlines():
                if "differs" in line or "error" in line:
                    print(f"         {line.strip()}")
        if args.verbose:
            print(result.stdout)

    print(f"\n{passed} of {len(maps)} maps round-tripped without loss")
    if failed:
        print("failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
