//! Open Doctrines maps to Greater Diplomacy 5 maps, and back.
//!
//! ```no_run
//! use dragoman::{convert, Format};
//!
//! let result = convert("world.odmap", "out", Format::Gd5, Default::default())?;
//! for note in result.problems() {
//!     println!("{note}");
//! }
//! # Ok::<(), dragoman::Error>(())
//! ```
//!
//! The conversion is lossy in both directions and the losses are not the same
//! one way as the other. Whatever could not cross is in [`Outcome::notes`]; a
//! conversion that succeeds with warnings is the normal case, not a problem.
//!
//! # Linking
//!
//! This crate binds a C library it does not build. Point the linker at one:
//!
//! ```text
//! DRAGOMAN_LIB_DIR=/path/to/build cargo build
//! ```

mod ffi;

use std::ffi::{CStr, CString};
use std::fmt;
use std::path::Path;

/// Which game's layout a map is in.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Format {
    /// Neither, or nothing readable at that path.
    Unknown,
    /// Open Doctrines: one zip archive, `.odmap`.
    Odmap,
    /// Greater Diplomacy 5: a directory of files.
    Gd5,
    /// Unciv: one JSON file holding a hex grid.
    ///
    /// Not the same kind of thing as the other two. They paint provinces onto
    /// a raster; Unciv has a hexagon per place, so crossing is a RESAMPLING.
    /// Use [`convert_unciv`] to choose the grid.
    Unciv,
}

impl Format {
    fn code(self) -> i32 {
        match self {
            Format::Unknown => 0,
            Format::Odmap => 1,
            Format::Gd5 => 2,
            Format::Unciv => 3,
        }
    }

    fn from_code(code: i32) -> Self {
        match code {
            1 => Format::Odmap,
            2 => Format::Gd5,
            3 => Format::Unciv,
            _ => Format::Unknown,
        }
    }
}

/// How much a note matters.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Severity {
    /// Worth knowing; the translation is exact.
    Info,
    /// Translated, but not exactly. The message says what was lost.
    Warning,
    /// Not translated.
    Error,
}

impl Severity {
    fn from_code(code: i32) -> Self {
        match code {
            2 => Severity::Error,
            1 => Severity::Warning,
            _ => Severity::Info,
        }
    }
}

/// One thing the library has to say about a conversion.
///
/// `code` is the stable half -- `"script.unsupported"`, `"gd5.ocean"` -- and is
/// what to match on. `message` is a sentence for a person and is reworded
/// between releases, so matching on it is a test that breaks for no reason.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Note {
    pub severity: Severity,
    pub code: String,
    pub message: String,
}

impl fmt::Display for Note {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "[{}] {}", self.code, self.message)
    }
}

/// What a conversion did, and everything it had to say about it.
#[derive(Clone, Debug)]
pub struct Outcome {
    pub notes: Vec<Note>,
    pub worst: Severity,
}

impl Outcome {
    /// Just the warnings and errors -- what did not cross cleanly.
    pub fn problems(&self) -> impl Iterator<Item = &Note> {
        self.notes.iter().filter(|n| n.severity != Severity::Info)
    }
}

/// A conversion that did not happen.
#[derive(Clone, Debug)]
pub struct Error {
    /// Why, as the library put it.
    pub message: String,
    /// Everything it said before giving up.
    pub notes: Vec<Note>,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for Error {}

/// What a conversion should and should not do.
///
/// [`Options::default`] asks the LIBRARY for its defaults rather than repeating
/// them here, so an option added upstream arrives with whatever value that
/// release considers sensible instead of a `false` this struct left in place.
#[derive(Clone, Copy, Debug)]
pub struct Options {
    /// Carry what the destination has no field for in a sidecar, so converting
    /// back restores it. Off, a round trip stops being lossless.
    pub carry_sidecar: bool,
    /// Compute what the destination needs and the source never stored.
    pub derive_geometry: bool,
    /// Translate scripts as well as map data.
    pub translate_scripts: bool,
    /// Treat any warning as a failure.
    pub strict: bool,
    /// Re-encode images rather than passing the original bytes through.
    pub reencode_images: bool,
    /// Cut the water into sea provinces when converting to GD5.
    pub synthesise_ocean: bool,
}

impl Default for Options {
    fn default() -> Self {
        let mut raw = ffi::DgOptions {
            carry_sidecar: 0,
            derive_geometry: 0,
            translate_scripts: 0,
            strict: 0,
            reencode_images: 0,
            synthesise_ocean: 0,
        };
        // SAFETY: the library writes six ints into a struct we own.
        unsafe { ffi::dg_options_defaults(&mut raw) };
        Options {
            carry_sidecar: raw.carry_sidecar != 0,
            derive_geometry: raw.derive_geometry != 0,
            translate_scripts: raw.translate_scripts != 0,
            strict: raw.strict != 0,
            reencode_images: raw.reencode_images != 0,
            synthesise_ocean: raw.synthesise_ocean != 0,
        }
    }
}

impl Options {
    fn to_ffi(self) -> ffi::DgOptions {
        ffi::DgOptions {
            carry_sidecar: self.carry_sidecar as i32,
            derive_geometry: self.derive_geometry as i32,
            translate_scripts: self.translate_scripts as i32,
            strict: self.strict as i32,
            reencode_images: self.reencode_images as i32,
            synthesise_ocean: self.synthesise_ocean as i32,
        }
    }
}

/// The native library's version, e.g. `"0.5.0"`.
pub fn version() -> String {
    // SAFETY: a static string owned by the library.
    unsafe { cstr(ffi::dg_version_string()) }
}

/// The ABI the loaded library speaks.
pub fn abi_version() -> i32 {
    unsafe { ffi::dg_abi_version() }
}

/// The ABI this crate was written against.
pub const REQUIRED_ABI: i32 = 2;

/// Which game's layout is at this path, if either.
pub fn detect(path: impl AsRef<Path>) -> Format {
    let Ok(c) = path_to_c(path.as_ref()) else {
        return Format::Unknown;
    };
    Format::from_code(unsafe { ffi::dg_detect(c.as_ptr()) })
}

/// Convert a map to `to`.
pub fn convert(
    input: impl AsRef<Path>,
    output: impl AsRef<Path>,
    to: Format,
    options: Options,
) -> Result<Outcome, Error> {
    let input = path_to_c(input.as_ref()).map_err(|e| bare(e))?;
    let output = path_to_c(output.as_ref()).map_err(|e| bare(e))?;
    let opts = options.to_ffi();
    let mut report: *mut ffi::DgReport = std::ptr::null_mut();

    // ZERO IS SUCCESS. It is the C convention, and reading it the other way
    // makes a finished map look like a failure -- a bug this library's own
    // Python binding once shipped.
    let rc = unsafe {
        ffi::dg_convert(input.as_ptr(), output.as_ptr(), to.code(), &opts, &mut report)
    };
    finish(rc == 0, report)
}

/// Convert to Unciv's format, choosing the hex grid.
///
/// Unciv's own sizes run from 24x15 (Tiny) to 80x50 (Huge). Zero for either
/// takes the library's default of 80x50, so `convert_unciv(i, o, 0, 0, opts)`
/// is `convert(i, o, Format::Unciv, opts)`. Both are clamped to 4..200.
///
/// Its own entry point rather than a field on [`Options`]: `dg_options` is
/// allocated by the caller, so a field added to it would break this binding's
/// ABI along with every other one.
pub fn convert_unciv(
    input: impl AsRef<Path>,
    output: impl AsRef<Path>,
    columns: i32,
    rows: i32,
    options: Options,
) -> Result<Outcome, Error> {
    let input = path_to_c(input.as_ref()).map_err(|e| bare(e))?;
    let output = path_to_c(output.as_ref()).map_err(|e| bare(e))?;
    let opts = options.to_ffi();
    let mut report: *mut ffi::DgReport = std::ptr::null_mut();
    let rc = unsafe {
        ffi::dg_convert_unciv(
            input.as_ptr(), output.as_ptr(), columns, rows, &opts, &mut report,
        )
    };
    finish(rc == 0, report)   // zero is success, as everywhere in this ABI
}

/// Whether a map came back the way it set out.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RoundTrip {
    /// It returned with every modelled field and the raster unchanged.
    Identical,
    /// The check ran and found a difference.
    Differed,
}

/// Convert a map out and back and check it returned unchanged, writing nothing
/// permanent. The property the whole library is for.
///
/// # This does not share [`convert`]'s convention
///
/// `dg_convert` returns 0 for success. `dg_roundtrip_check` returns **1 for
/// identical**, 0 for a difference and -1 for a check that could not run -- so
/// reading it the same way reports a holding round trip as a failure and a
/// broken one as success. Both readings look like working code.
pub fn roundtrip_check(
    path: impl AsRef<Path>,
    to: Format,
    options: Options,
) -> Result<(RoundTrip, Outcome), Error> {
    let path = path_to_c(path.as_ref()).map_err(|e| bare(e))?;
    let opts = options.to_ffi();
    let mut report: *mut ffi::DgReport = std::ptr::null_mut();
    let rc = unsafe { ffi::dg_roundtrip_check(path.as_ptr(), to.code(), &opts, &mut report) };
    let outcome = finish(rc >= 0, report)?;
    Ok((
        if rc == 1 { RoundTrip::Identical } else { RoundTrip::Differed },
        outcome,
    ))
}

/// Reads the report, then frees it.
///
/// The free is why this is one function rather than being inlined at each call
/// site: the library hands back a report whether it succeeded or failed, and it
/// leaks unless every path releases it -- including the error path.
fn finish(ok: bool, report: *mut ffi::DgReport) -> Result<Outcome, Error> {
    let mut notes = Vec::new();
    let mut worst = Severity::Info;

    if !report.is_null() {
        // SAFETY: a report the library just handed us, released below on every
        // path out of this function.
        unsafe {
            let n = ffi::dg_report_count(report);
            for i in 0..n {
                notes.push(Note {
                    severity: Severity::from_code(ffi::dg_report_severity(report, i)),
                    code: cstr(ffi::dg_report_code(report, i)),
                    message: cstr(ffi::dg_report_message(report, i)),
                });
            }
            worst = Severity::from_code(ffi::dg_report_worst(report));
            ffi::dg_report_free(report);
        }
    }

    if ok {
        Ok(Outcome { notes, worst })
    } else {
        let message = unsafe { cstr(ffi::dg_last_error()) };
        Err(Error {
            message: if message.is_empty() {
                "the conversion failed without saying why".to_owned()
            } else {
                message
            },
            notes,
        })
    }
}

fn bare(message: String) -> Error {
    Error { message, notes: Vec::new() }
}

fn path_to_c(path: &Path) -> Result<CString, String> {
    let s = path
        .to_str()
        .ok_or_else(|| format!("{} is not valid UTF-8", path.display()))?;
    CString::new(s).map_err(|_| format!("{} contains a NUL byte", path.display()))
}

/// A `const char*` the library owns. Copied, never freed here.
unsafe fn cstr(p: *const std::os::raw::c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        CStr::from_ptr(p).to_string_lossy().into_owned()
    }
}
