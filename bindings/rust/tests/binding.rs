//! The binding, against the real library.
//!
//! Run with the library on the linker's path:
//!
//! ```text
//! DRAGOMAN_LIB_DIR=/path/to/build cargo test
//! ```
//!
//! A real conversion needs a real map, which is not in this repository --
//! Open Doctrines' are its own and GD5's are GPL. Point at one and the last
//! test runs; without it, it SKIPS and says so rather than passing quietly.

use dragoman::{abi_version, convert, convert_unciv, detect, roundtrip_check, Format, Options, RoundTrip,
               Severity, REQUIRED_ABI};

#[test]
fn the_library_loads_and_agrees_on_the_abi() {
    let v = dragoman::version();
    assert!(!v.is_empty(), "the library reports no version");
    assert_eq!(abi_version(), REQUIRED_ABI, "this crate was written against a different ABI");
}

#[test]
fn defaults_come_from_the_library() {
    let o = Options::default();
    // Not asserted as a fixed table: the point is that they were ASKED FOR.
    // A struct of `false` would mean Default::default() never reached the
    // library, which is the mistake this catches.
    assert!(o.carry_sidecar, "carry_sidecar should default on -- a round trip is lossless");
    assert!(o.derive_geometry);
    assert!(o.translate_scripts);
    assert!(!o.strict);
    assert!(o.synthesise_ocean);
}

#[test]
fn something_that_is_not_a_map_is_unknown() {
    let mut p = std::env::temp_dir();
    p.push("dragoman-rs-not-a-map.txt");
    std::fs::write(&p, b"hello").unwrap();
    assert_eq!(detect(&p), Format::Unknown);
    let _ = std::fs::remove_file(&p);
}

/// The polarity test. If zero were read as failure, this would return `Ok`.
#[test]
fn a_failed_conversion_is_an_error_that_says_why() {
    let mut out = std::env::temp_dir();
    out.push("dragoman-rs-should-not-appear");
    let _ = std::fs::remove_dir_all(&out);

    let r = convert("/does/not/exist/anywhere.odmap", &out, Format::Gd5, Options::default());
    let err = r.expect_err("converting a path that does not exist should fail");
    assert!(!err.message.is_empty(), "it failed without saying why");
    assert!(!out.exists(), "it wrote something anyway");
}

/// Unciv is reachable, and the grid size is honoured.
///
/// Worth its own test because Unciv arrived in the C ABI and in NO binding:
/// this one had no `Format::Unciv` at all, so the format the library had
/// supported for a release was one no Rust caller could name.
#[test]
fn unciv_is_reachable_and_the_grid_is_honoured() {
    let Some(map) = std::env::var_os("DRAGOMAN_TEST_MAP") else {
        eprintln!("skip: set DRAGOMAN_TEST_MAP=<a map> to run a real conversion");
        return;
    };
    let map = std::path::PathBuf::from(map);
    if detect(&map) != Format::Odmap {
        eprintln!("skip: DRAGOMAN_TEST_MAP is not an .odmap");
        return;
    }

    let mut out = std::env::temp_dir();
    out.push(format!("dragoman-rs-unciv-{}.json", std::process::id()));
    let _ = std::fs::remove_file(&out);

    convert_unciv(&map, &out, 24, 15, Options::default()).expect("conversion failed");
    let text = std::fs::read_to_string(&out).expect("nothing was written");
    // 24x15 is Unciv's "Tiny". Counting the positions rather than parsing the
    // JSON keeps this test free of a parser dependency.
    let tiles = text.matches("\"position\"").count();
    assert_eq!(tiles, 24 * 15, "the grid size was not honoured");

    let _ = std::fs::remove_file(&out);
}

#[test]
fn a_real_map_converts_and_returns() {
    let Some(map) = std::env::var_os("DRAGOMAN_TEST_MAP") else {
        eprintln!("skip: set DRAGOMAN_TEST_MAP=<a map> to run a real conversion");
        return;
    };
    let map = std::path::PathBuf::from(map);
    let from = detect(&map);
    assert_ne!(from, Format::Unknown, "{} is not a map", map.display());

    let to = if from == Format::Odmap { Format::Gd5 } else { Format::Odmap };
    let mut out = std::env::temp_dir();
    out.push(format!("dragoman-rs-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&out);

    let outcome = convert(&map, &out, to, Options::default()).expect("conversion failed");
    assert!(out.exists(), "it reported success and wrote nothing");
    // Plenty of notes is the NORMAL case: the two games do not hold the same
    // facts. What matters is that each arrives with its stable code attached.
    for n in &outcome.notes {
        assert!(!n.code.is_empty(), "a note with no code: {n}");
        assert!(!n.message.is_empty(), "a note with no message: {n}");
    }
    assert!(outcome.worst >= Severity::Info);

    // And the property the library exists for.
    //
    // NOT read the way convert() is read. dg_roundtrip_check returns 1 for
    // identical, 0 for a difference and -1 for a check that could not run, so
    // treating 0 as success -- as the rest of the ABI does -- calls a holding
    // round trip a failure. This test caught exactly that.
    let (held, _notes) =
        roundtrip_check(&map, to, Options::default()).expect("the round-trip check failed to run");
    assert_eq!(held, RoundTrip::Identical, "the map did not come back unchanged");

    let _ = std::fs::remove_dir_all(&out);
}
