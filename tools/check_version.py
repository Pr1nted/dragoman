#!/usr/bin/env python3
"""Assert that every copy of the version number agrees.

The version is written in four places, and three of them are easy to forget:

    VERSION                                  the source of truth
    include/dragoman/dragoman.h              the C macros a caller compiles against
    bindings/python/dragoman/_version.py     what `dragoman.__version__` reports
    pyproject.toml                           what PyPI and `pip show` report
    CMakeLists.txt                           read from VERSION, so checked by construction

A release that updates some but not all of them ships a library that reports a
version it is not. tests/test_version.cpp checks the same thing from inside the
build; this script exists so the check also runs on a machine that never
compiled anything.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def fail(message):
    print(f"version mismatch: {message}", file=sys.stderr)
    sys.exit(1)


def main():
    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        fail(f"VERSION holds {version!r}, which is not major.minor.patch")
    major, minor, patch = version.split(".")

    header = (ROOT / "include/dragoman/dragoman.h").read_text()
    for macro, want in (
        ("DRAGOMAN_VERSION_MAJOR", major),
        ("DRAGOMAN_VERSION_MINOR", minor),
        ("DRAGOMAN_VERSION_PATCH", patch),
    ):
        found = re.search(rf"#define\s+{macro}\s+(\d+)", header)
        if not found:
            fail(f"{macro} is not defined in dragoman.h")
        if found.group(1) != want:
            fail(f"{macro} is {found.group(1)}, but VERSION says {want}")

    found = re.search(r'#define\s+DRAGOMAN_VERSION_STRING\s+"([^"]+)"', header)
    if not found:
        fail("DRAGOMAN_VERSION_STRING is not defined in dragoman.h")
    if found.group(1) != version:
        fail(f"DRAGOMAN_VERSION_STRING is {found.group(1)!r}, but VERSION says {version!r}")

    py = (ROOT / "bindings/python/dragoman/_version.py").read_text()
    found = re.search(r'__version__\s*=\s*"([^"]+)"', py)
    if not found:
        fail("__version__ is not set in bindings/python/dragoman/_version.py")
    if found.group(1) != version:
        fail(f"the Python package says {found.group(1)!r}, but VERSION says {version!r}")

    toml = (ROOT / "pyproject.toml").read_text()
    found = re.search(r'^version = "([^"]+)"', toml, re.M)
    if not found:
        fail("pyproject.toml has no version")
    if found.group(1) != version:
        fail(f"pyproject.toml says {found.group(1)!r}, but VERSION says {version!r}")

    # The bindings added in 0.5.0 each carry the version in their own manifest,
    # and none of them was checked here -- so a bump could land in VERSION, the
    # header, pyproject and the Python package and leave Rust, npm and Zig
    # claiming the release before it. They are checked by the same rule as the
    # rest: whatever VERSION says.
    for path, pattern, what in (
        ("bindings/rust/Cargo.toml", r'^version = "([^"]+)"', "the Rust crate"),
        ("bindings/js/package.json", r'"version"\s*:\s*"([^"]+)"', "the npm package"),
        ("bindings/zig/build.zig.zon", r'\.version\s*=\s*"([^"]+)"', "the Zig package"),
        ("bindings/rust/Cargo.lock",
         r'name = "open-dragoman"\nversion = "([^"]+)"', "the Rust lockfile"),
    ):
        text = (ROOT / path).read_text()
        found = re.search(pattern, text, re.M)
        if not found:
            fail(f"{path} has no version")
        if found.group(1) != version:
            fail(f"{what} says {found.group(1)!r}, but VERSION says {version!r}")

    # The ABI version is deliberately not checked against the release version:
    # they move for different reasons and are supposed to disagree. It only has
    # to exist and be a positive integer.
    found = re.search(r"#define\s+DRAGOMAN_ABI_VERSION\s+(\d+)", header)
    if not found or int(found.group(1)) < 1:
        fail("DRAGOMAN_ABI_VERSION must be defined and at least 1")

    print(f"version {version} agrees everywhere (abi {found.group(1)})")


if __name__ == "__main__":
    main()
