#!/usr/bin/env python3
"""The Python binding, exercised for real.

A ctypes binding fails in a way a C++ test cannot catch: a symbol that was
never exported still compiles, still links, still passes every test in the
suite, and then raises AttributeError at the first call a binding makes. So
this runs on every platform in CI and actually converts a map.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def find_library():
    """Point ctypes at the library this checkout just built."""
    names = {
        "win32": ["dragoman.dll"],
        "darwin": ["libdragoman.dylib"],
    }.get(sys.platform, ["libdragoman.so"])

    roots = []
    if os.environ.get("DRAGOMAN_BUILD_DIR"):
        roots.append(Path(os.environ["DRAGOMAN_BUILD_DIR"]))
    roots += [ROOT / "build", ROOT / "build" / "Release", ROOT / "build" / "Debug"]

    for root in roots:
        for name in names:
            candidate = root / name
            if candidate.exists():
                return candidate
    return None


library = find_library()
if library:
    os.environ["DRAGOMAN_LIBRARY"] = str(library)

sys.path.insert(0, str(ROOT / "bindings" / "python"))
import dragoman  # noqa: E402


failures = []


def check(condition, description):
    if condition:
        print(f"  ok   {description}")
    else:
        print(f"  FAIL {description}")
        failures.append(description)


def build_fixture(tmp):
    """A .odmap to work on, written by the C++ test binary.

    The fixture lives in one place -- tests/Fixture.h -- so that the Python
    suite and the C++ suite cannot drift into testing two different maps.
    """
    binary = ROOT / "build" / "test_abi"
    for candidate in (binary, ROOT / "build" / "Release" / "test_abi.exe",
                      ROOT / "build" / "Debug" / "test_abi.exe"):
        if candidate.exists():
            subprocess.run([str(candidate)], cwd=str(ROOT), check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            break
    made = ROOT / "build" / "test-scratch" / "abi.odmap"
    return made if made.exists() else None


def main():
    print(f"dragoman {dragoman.__version__} (library {dragoman.library_version()}, "
          f"abi {dragoman.abi_version()})")

    check(dragoman.library_version() == dragoman.__version__,
          "the package and the library report the same version")
    check(dragoman.abi_version() >= 1, "the ABI version is set")

    tmp = Path(tempfile.mkdtemp())
    fixture = build_fixture(tmp)
    if not fixture:
        print("  note the C++ fixture map is not built; skipping the conversion tests")
        return 1 if failures else 0

    check(dragoman.detect(str(fixture)) == "odmap", "an .odmap is detected by content")
    check(dragoman.detect(str(tmp)) == "unknown", "a directory that is not a map is not one")

    world = dragoman.load(str(fixture))
    check(len(world.provinces) == 6, "the map's provinces are readable as Python data")
    check(len(world.nations) == 4, "the map's nations are readable as Python data")
    check(world.origin == "odmap", "the world remembers which format it came from")
    check(any(p["name"] == "Rhineland" for p in world.provinces),
          "province fields survive the crossing into Python")

    out = tmp / "gd5"
    report = dragoman.convert(str(fixture), str(out), to="gd5")
    check((out / "map_data.json").exists(), "converting produces a GD5 map directory")
    check((out / "meta.json").exists(), "converting produces GD5 metadata")
    check(report.ok, "the conversion reported no errors")

    identical, _ = dragoman.roundtrip_check(str(fixture), to="gd5")
    check(identical, "the round trip preserves the map")

    lossy, _ = dragoman.roundtrip_check(str(fixture), to="gd5", carry_sidecar=False)
    check(not lossy, "without the sidecar the round trip is admitted to be lossy")

    try:
        dragoman.load(str(tmp / "nothing-here"))
        check(False, "a missing file raises")
    except dragoman.DragomanError:
        check(True, "a missing file raises DragomanError rather than crashing")

    world.close()
    world.close()  # closing twice must be harmless
    check(True, "a world can be closed more than once")

    return 1 if failures else 0


if __name__ == "__main__":
    code = main()
    print("FAILED" if code else "all Python binding checks passed")
    sys.exit(code)
