# Versioning

Two numbers, moving for different reasons.

## Library version — semver, in `VERSION`

The release. `0.1.0` today. Bumped for every published build:

- **patch** — a bug fixed, no field mapped differently.
- **minor** — a field that used to be carried is now mapped, a new diagnostic
  code, a new ABI symbol. A map converted by the old version still converts.
- **major** — an existing mapping changes meaning, or a symbol is removed.

While the major version is 0, the minor acts as the major: `0.2.0` may change a
mapping that `0.1.x` had.

## ABI version — `DRAGOMAN_ABI_VERSION`, an integer

Bumped **only** when an existing symbol changes meaning or disappears. Adding a
symbol does not move it. A binding calls `dg_abi_version()` and refuses to
continue if it is higher than it was built for, without having to parse a semver
string:

```c
if (dg_abi_version() != DRAGOMAN_ABI_VERSION) { /* refuse */ }
```

The shared library's `SOVERSION` is the ABI version, not the release version, so
the loader enforces the same rule.

## Sidecar version — `DRAGOMAN_SIDECAR_VERSION`

Bumped when the sidecar's own layout changes. Separate again, because a 0.1.x
and a 0.3.x Dragoman should still be able to read each other's carried data.

## Where the version lives, and how it stays consistent

| File | What it holds |
|---|---|
| `VERSION` | the source of truth |
| `include/dragoman/dragoman.h` | `DRAGOMAN_VERSION_{MAJOR,MINOR,PATCH,STRING}` |
| `bindings/python/dragoman/_version.py` | `__version__` |
| `CMakeLists.txt` | reads `VERSION`, so correct by construction |

Two independent checks fail if any copy drifts:

- `tools/check_version.py` — no compiler needed, runs as its own CI job.
- `tests/test_version.cpp` — reads the same files from inside the build and also
  checks that `dg_version_string()` agrees with the three integers.

## Releasing

1. Edit `VERSION`.
2. Mirror it into `dragoman.h` and `_version.py`.
3. Bump `DRAGOMAN_ABI_VERSION` **only** if an existing symbol changed meaning.
4. `python3 tools/check_version.py`
5. Update `CHANGELOG.md`.
6. Tag `vX.Y.Z` and push. The release workflow builds Linux, macOS and Windows
   binaries and attaches them.

Step 4 is not optional; it is the step that catches steps 2 and 3.
