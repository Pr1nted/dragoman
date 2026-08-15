# Installing

Pick the row that describes you.

| You want to… | Do this |
|---|---|
| Convert maps, from a terminal or from Python | `pip install dragoman` |
| Convert maps, no Python at all | [Download a release binary](#a-release-binary) |
| Call it from your own C or C++ project | [Build from source](#from-source) or use CMake `FetchContent` |
| Work on Dragoman itself | [Build from source](#from-source) |

## pip

```bash
pip install dragoman
```

That is the whole thing. The wheel carries the compiled library **inside** the
package, so there is no compiler needed, no CMake, and nothing to locate
afterwards. You get both the Python API and a `dragoman` command:

```bash
dragoman convert 1914.odmap "base_maps/1914"
dragoman roundtrip 1914.odmap
```

```python
import dragoman
dragoman.convert("1914.odmap", "base_maps/1914", to="gd5")
```

Python 3.8 or newer. Linux, macOS and Windows.

### Straight from the repository

For a version that has not been released yet:

```bash
pip install git+https://github.com/Pr1nted/dragoman
```

This one *does* build the C++ library, so it needs CMake and a C++17 compiler.

## A release binary

If you have no interest in Python, every release attaches a prebuilt archive
per platform to [the releases page](https://github.com/Pr1nted/dragoman/releases):
the `dragoman` command, the shared library, and the header.

```bash
tar xzf dragoman-linux-x86_64.tar.gz
./dragoman version
```

## From source

CMake 3.16 and a C++17 compiler. No dependencies to install — miniz, stb and
nlohmann/json are vendored.

```bash
git clone https://github.com/Pr1nted/dragoman
cd dragoman
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

You get `build/dragoman`, `libdragoman.a`, and a shared
`libdragoman.{so,dylib,dll}`.

Install it system-wide with `cmake --install build`, which places the library,
the headers and the CLI under the usual prefixes.

## In a CMake project

```cmake
include(FetchContent)
FetchContent_Declare(dragoman
    GIT_REPOSITORY https://github.com/Pr1nted/dragoman
    GIT_TAG        v0.2.0)
set(DRAGOMAN_BUILD_TESTS OFF)
set(DRAGOMAN_BUILD_CLI OFF)
FetchContent_MakeAvailable(dragoman)

target_link_libraries(your_target PRIVATE dragoman)
```

Then `#include <dragoman/dragoman.hpp>` for the C++ wrapper, or
`<dragoman/dragoman.h>` for the C ABI.

## Anything else

Rust, Go, C#, Java, Lua and WebAssembly all bind the same C header against the
shared library from a release archive or a source build. See
[Languages](Languages.md).

## Build options

| Option | Default | |
|---|---|---|
| `DRAGOMAN_BUILD_TESTS` | ON | The test suite |
| `DRAGOMAN_BUILD_CLI` | ON | The `dragoman` command |
| `DRAGOMAN_BUILD_SHARED` | ON | The shared library the bindings load |
| `DRAGOMAN_WARNINGS_AS_ERRORS` | OFF | What CI builds with |

## Checking it worked

```bash
dragoman version
```

```python
import dragoman
print(dragoman.__version__, dragoman.library_version(), dragoman.abi_version())
```

The package version and the library version should match. If they do not, you
have a wheel and a system library from different releases — see
[Troubleshooting](Troubleshooting.md).
