# open-dragoman for Zig

```zig
const dragoman = @import("dragoman");

var outcome = try dragoman.convert(allocator, "world.odmap", "out", .gd5, .{});
defer outcome.deinit();
for (outcome.notes) |note| {
    std.debug.print("[{s}] {s}\n", .{ note.code, note.message });
}
```

The C declarations come from the header itself through `@cImport`, so unlike
every other binding here there is no second copy of the ABI to drift from it.

Requires **Zig 0.15**. Zig's standard library and build API are not stable
between releases; `std.ArrayList` changed shape in 0.15, which this binding
sidesteps by allocating the note array exactly once from `dg_report_count`.

## Building

This binds a C library it does not build — duplicating CMake here would give two
build systems that can disagree about what was built.

```
zig build test -Ddragoman-lib=/path/to/build -Ddragoman-include=/path/to/include
```

## Ownership

`Outcome` owns its notes and must be `deinit`ed. The strings are **copied** out
of the report rather than pointed into it, because the report is freed before
`collect` returns — a `[]const u8` into freed memory reads fine for a while and
then does not.

The report itself is freed on every path out of `collect`, including the failure
path, which is why it is one function rather than being inlined at each call
site.

Zig's testing allocator checks all of this: the suite fails on a leak.

## Two return conventions, not one

| call | success is |
|---|---|
| `convert` | a C return of **0** |
| `roundTripCheck` | a C return of **1** — 0 means "differed", -1 means the check could not run |

Reading the second the way you read the first calls a holding round trip a
failure. That bug shipped in the JVM binding before a real map exposed it, which
is why `convert` returns `Error!Outcome` and `roundTripCheck` returns a struct
carrying a `RoundTrip` — the two cannot be confused by a reader of either.

## Tests

```
zig build test -Ddragoman-lib=/path/to/build \
  -Ddragoman-include=/path/to/include
DRAGOMAN_TEST_MAP=/path/to/world.odmap zig build test ...
```

Without `DRAGOMAN_TEST_MAP` the conversion test **prints a skip and returns**,
rather than passing quietly and implying coverage it does not have.

### A caveat about macOS

`zig build` cannot run on a current macOS with either 0.13 or 0.15: the build
runner itself fails to link against the newer SDK, with
`undefined symbol: __availability_version_check` and friends. An empty
`build.zig` fails the same way, so it is the toolchain and not this package.

The binding itself is fine there — it was developed and tested on macOS by
invoking the compiler directly:

```
zig test -target aarch64-macos.14.0.0 \
  -L/path/to/build -rpath /path/to/build -ldragoman -lc \
  --dep dragoman -Mroot=src/test.zig -Mdragoman=src/dragoman.zig \
  -I/path/to/include
```

`build.zig` is therefore exercised by CI on Linux rather than by hand here.
