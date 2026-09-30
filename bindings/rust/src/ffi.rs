//! The flat C ABI, declared once.
//!
//! Private on purpose: everything here is a raw pointer with an ownership rule
//! attached, and the rules are easy to get wrong from outside. The safe API in
//! [`crate`] is the only thing that should touch it.

use std::os::raw::{c_char, c_int};

/// `dg_options`: six ints, in this order.
#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct DgOptions {
    pub carry_sidecar: c_int,
    pub derive_geometry: c_int,
    pub translate_scripts: c_int,
    pub strict: c_int,
    pub reencode_images: c_int,
    pub synthesise_ocean: c_int,
}

/// Opaque: only ever held behind a pointer.
#[repr(C)]
pub(crate) struct DgReport {
    _private: [u8; 0],
}

extern "C" {
    pub(crate) fn dg_version_string() -> *const c_char;
    pub(crate) fn dg_abi_version() -> c_int;

    pub(crate) fn dg_detect(path: *const c_char) -> c_int;

    pub(crate) fn dg_options_defaults(out: *mut DgOptions);

    pub(crate) fn dg_convert(
        in_path: *const c_char,
        out_path: *const c_char,
        to: c_int,
        opts: *const DgOptions,
        out_report: *mut *mut DgReport,
    ) -> c_int;

    pub(crate) fn dg_roundtrip_check(
        path: *const c_char,
        to: c_int,
        opts: *const DgOptions,
        out_report: *mut *mut DgReport,
    ) -> c_int;

    pub(crate) fn dg_report_count(r: *const DgReport) -> c_int;
    pub(crate) fn dg_report_severity(r: *const DgReport, index: c_int) -> c_int;
    pub(crate) fn dg_report_code(r: *const DgReport, index: c_int) -> *const c_char;
    pub(crate) fn dg_report_message(r: *const DgReport, index: c_int) -> *const c_char;
    pub(crate) fn dg_report_worst(r: *const DgReport) -> c_int;
    pub(crate) fn dg_report_free(r: *mut DgReport);

    pub(crate) fn dg_last_error() -> *const c_char;
}
