# open-dragoman for Rust

```rust
use dragoman::{convert, Format, Options};

let result = convert("world.odmap", "out", Format::Gd5, Options::default())?;
for note in result.problems() {
    println!("{note}");
}
# Ok::<(), dragoman::Error>(())
```

The package is `open-dragoman` and the crate is `dragoman`, matching the Python
package's `pip install open-dragoman` / `import dragoman`.

## Linking

This crate binds a C library it does not build — duplicating CMake in `build.rs`
would give two build systems that can disagree about what was built. Point the
linker at one:

```
DRAGOMAN_LIB_DIR=/path/to/build cargo build
```

Without it the usual system paths are searched, which is right for a machine
that has the library installed.

## Two return conventions, not one

This is the trap, and it is worth stating plainly because both readings compile
and look correct:

| call | success is |
|---|---|
| `dg_convert` | **0** — non-zero is failure |
| `dg_roundtrip_check` | **1** — 0 means "differed", -1 means the check could not run |

So `convert` returns `Result<Outcome, Error>` and `roundtrip_check` returns
`Result<(RoundTrip, Outcome), Error>`. Reading the second the way you read the
first reports a holding round trip as a failure and a broken one as success.

The test suite caught exactly that, in code already shipped in the JVM binding.

## Ownership

A report must be freed. `dg_convert` hands one back whether it succeeded or
failed, and it leaks unless every path releases it — **including the error
path**. `finish()` is one function for that reason rather than being inlined at
each call site.

## Tests

They need the real library, and a real conversion needs a real map, which is not
in this repository — Open Doctrines' maps are its own and GD5's are GPL.

```
DRAGOMAN_LIB_DIR=/path/to/build \
DYLD_LIBRARY_PATH=/path/to/build \
DRAGOMAN_TEST_MAP=/path/to/world.odmap \
cargo test
```

Without `DRAGOMAN_TEST_MAP` the conversion test **prints a skip and returns**,
rather than passing quietly and implying coverage it does not have.
