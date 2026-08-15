# The C ABI

Everything crossing the boundary is a plain C type, so a caller in Python,
Rust, Go, C# or Java binds it with the FFI it already has and never meets a C++
symbol. The surface is deliberately narrow.

## Contract

- **Ownership** — every pointer the library returns is freed by the matching
  `*_free`, never by the caller's allocator. `dg_world_free`, `dg_report_free`,
  `dg_string_free`.
- **Nulls** — every function accepts a null handle and answers with a zero
  value rather than crashing. The `*_free` functions accept null.
- **Errors** — functions return a status (`NULL`, or non-zero) and never throw.
  `dg_last_error()` gives the reason, thread-locally, valid until the next
  failure on that thread.
- **Reports** — `out_report` is always optional. When you pass one you own the
  report and must free it, **including when the call failed** — that is usually
  where the interesting diagnostics are.
- **Threads** — no shared mutable state except the thread-local error buffer, so
  distinct `dg_world`s may be used from distinct threads.
- **Visibility** — the shared library is built with hidden visibility. Only the
  symbols in `dragoman.h` are exported; miniz, stb and the C++ runtime are not.

## The escape hatch

Rather than grow an accessor per field, `dg_world_to_json` returns the entire
interchange model as text. A binding parses it, edits it, and hands it back with
`dg_world_from_json`. The schema is documented in [model.md](model.md) and
versioned with the ABI.

The province raster is the one thing not in that document: a world map is
8192×4096, and thirty-three million ids as JSON text is a hundred megabytes to
restate what the accompanying PNG already says. It appears as its dimensions and
a digest, which is enough to tell two rasters apart. A world rebuilt from JSON
alone therefore has no raster, and every writer reports that rather than
silently emitting an empty map.

## Minimal use

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

## Options

| Field | Default | Effect |
|---|---|---|
| `carry_sidecar` | 1 | Write what the destination cannot hold beside the map. Turning this off makes a smaller, genuinely lossy conversion. |
| `derive_geometry` | 1 | Compute adjacency, centres and the land/sea mask from the raster. Off, GD5 cannot move units on the result. |
| `translate_scripts` | 1 | Translate scripts and scripted events. |
| `strict` | 0 | Treat any warning as a failure. For CI. |
| `reencode_images` | 0 | Re-encode images rather than passing original bytes through. |
| `synthesise_ocean` | 1 | Cut the water into sea provinces when converting to GD5 from a game that draws none. Off, the sea renders black and no fleet can move. |

## Diagnostics

Every entry carries a stable `code` as well as a sentence, so a caller can match
on `script.unsupported` without matching on English. Severity is
`DG_INFO`/`DG_WARNING`/`DG_ERROR`.

Codes are grouped by area: `od.*` and `gd5.*` for the readers and writers,
`script.*` for translation, `sidecar.*` for what was restored, `roundtrip.*` for
`dg_roundtrip_check`. The script codes are listed in
[scripting.md](scripting.md).

## Binding checklist

Two mistakes cost real time, so they are worth stating:

1. **Declare every signature.** ctypes and its equivalents assume an `int`
   return, which truncates a 64-bit handle on the first call that returns one.
   The failure looks like a corrupt map, not a binding bug.
2. **Test by loading, not by building.** A symbol that is not exported still
   compiles and still passes every C++ test; it fails at the first `dlsym`. CI
   imports the Python binding and converts a real map on every platform for
   exactly this reason.
