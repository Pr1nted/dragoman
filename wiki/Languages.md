# Languages

The core is C++17 behind a flat **C99 ABI**, so anything with an FFI can call
it. The full contract — ownership, threading, error handling — is in
[docs/abi.md](https://github.com/Pr1nted/dragoman/blob/main/docs/abi.md).

## The escape hatch

Rather than an accessor per field, `dg_world_to_json` hands back the entire
interchange model as text. A binding parses it, edits it, and hands it back with
`dg_world_from_json`. That is why the ABI is thirty functions rather than three
hundred, and why a binding in a new language is an afternoon. The schema is in
[docs/model.md](https://github.com/Pr1nted/dragoman/blob/main/docs/model.md).

The province raster is the one thing not in that document: a world map is
8192×4096, and thirty-three million ids as JSON text is a hundred megabytes to
restate what the PNG beside it already says. It appears as its dimensions and a
digest.

## Python

Pure ctypes — no compiler needed at install time, which matters because Greater
Diplomacy 5 is a pygame project whose players install it by unzipping it.

```bash
pip install ./bindings/python
```

```python
import dragoman

report = dragoman.convert("1914.odmap", "base_maps/1914", to="gd5")
for d in report.warnings:
    print(d.code, d.message)

world = dragoman.load("1914.odmap")
print(world.name, len(world.provinces), "provinces")
for p in world.provinces[:5]:
    print(p["id"], p["name"], p["owner"], p["population"])

identical, _ = dragoman.roundtrip_check("1914.odmap", to="gd5")
```

`World` is a context manager, and every option is a keyword argument:

```python
with dragoman.load("1914.odmap") as w:
    w.save("out/1914", to="gd5", synthesise_ocean=False, strict=True)
```

If it cannot find the shared library, set `DRAGOMAN_LIBRARY` to its path.

## C

```c
#include <dragoman/dragoman.h>

dg_options opts;
dg_options_defaults(&opts);

dg_report* report = NULL;
if (dg_convert("1914.odmap", "base_maps/1914", DG_FORMAT_GD5, &opts, &report) != 0) {
    fprintf(stderr, "%s\n", dg_last_error());
}
for (int i = 0; i < dg_report_count(report); ++i) {
    printf("%s: %s\n", dg_report_code(report, i), dg_report_message(report, i));
}
dg_report_free(report);
```

```bash
cc example.c -I include -L build -ldragoman -lstdc++ -Wl,-rpath,"$PWD/build" -o example
```

Full example: [bindings/c/example.c](https://github.com/Pr1nted/dragoman/blob/main/bindings/c/example.c).

## C++

A header-only RAII wrapper over the same ABI — it owns handles and turns
failures into exceptions, and adds no behaviour of its own.

```cpp
#include <dragoman/dragoman.hpp>

dragoman::Options opts;
auto report = dragoman::convert("1914.odmap", "base_maps/1914",
                                dragoman::Format::Gd5, opts);
for (const auto& d : report.warnings()) std::cerr << d.message << "\n";

dragoman::World world("1914.odmap", dragoman::Format::Odmap, opts);
std::cout << world.name() << " " << world.provinceCount() << "\n";
```

Full example: [bindings/cpp/example.cpp](https://github.com/Pr1nted/dragoman/blob/main/bindings/cpp/example.cpp).

## Anything else

Rust, Go, C#, Java, Lua and WebAssembly all bind the same header. Two mistakes
cost real time, so they are worth stating:

1. **Declare every signature.** ctypes and its equivalents assume an `int`
   return, which truncates a 64-bit handle on the first call that returns one.
   The failure looks like a corrupt map, not a binding bug.
2. **Test by loading, not by building.** A symbol that is not exported still
   compiles and still passes every C++ test; it fails at the first `dlsym`. CI
   imports the Python binding and converts a real map on every platform for
   exactly this reason — that bug happened here.

## Versions

```c
dg_version_string()   /* "0.2.0" — the release */
dg_abi_version()      /* 2 — moves only when a symbol changes meaning */
```

A binding should check the ABI version and refuse a library it cannot speak to.
Adding a symbol does not move it; changing or removing one does. See
[docs/versioning.md](https://github.com/Pr1nted/dragoman/blob/main/docs/versioning.md).
