//! Open Doctrines maps to Greater Diplomacy 5 maps, and back.
//!
//! ```zig
//! const dragoman = @import("dragoman");
//!
//! var outcome = try dragoman.convert(allocator, "world.odmap", "out", .gd5, .{});
//! defer outcome.deinit();
//! for (outcome.notes) |note| std.debug.print("[{s}] {s}\n", .{ note.code, note.message });
//! ```
//!
//! The C declarations come from the header itself via `@cImport`, so there is
//! no second copy of the ABI here to drift from it.

const std = @import("std");

pub const c = @cImport({
    @cInclude("dragoman/dragoman.h");
});

/// The ABI this binding was written against.
pub const required_abi: c_int = 2;

/// Which game's layout a map is in.
pub const Format = enum(c_int) {
    unknown = 0,
    /// Open Doctrines: one zip archive, `.odmap`.
    odmap = 1,
    /// Greater Diplomacy 5: a directory of files.
    gd5 = 2,

    pub fn fromCode(code: c_int) Format {
        return switch (code) {
            1 => .odmap,
            2 => .gd5,
            else => .unknown,
        };
    }
};

/// How much a note matters.
pub const Severity = enum(c_int) {
    info = 0,
    /// Translated, but not exactly. The message says what was lost.
    warning = 1,
    /// Not translated.
    err = 2,

    pub fn fromCode(code: c_int) Severity {
        return switch (code) {
            2 => .err,
            1 => .warning,
            else => .info,
        };
    }
};

/// Whether a map came back the way it set out.
pub const RoundTrip = enum { identical, differed };

/// One thing the library has to say about a conversion.
///
/// `code` is the stable half -- `"script.unsupported"` -- and is what to switch
/// on. `message` is a sentence for a person and is reworded between releases.
///
/// Both point into memory this binding owns; they are freed by `Outcome.deinit`.
pub const Note = struct {
    severity: Severity,
    code: []const u8,
    message: []const u8,
};

/// What a conversion did, and everything it had to say about it.
pub const Outcome = struct {
    notes: []Note,
    worst: Severity,
    allocator: std.mem.Allocator,

    pub fn deinit(self: *Outcome) void {
        for (self.notes) |n| {
            self.allocator.free(n.code);
            self.allocator.free(n.message);
        }
        self.allocator.free(self.notes);
        self.notes = &.{};
    }

    /// Whether anything failed to cross cleanly.
    pub fn hasProblems(self: Outcome) bool {
        return self.worst != .info;
    }
};

pub const Error = error{
    /// The library refused the conversion. `lastError()` says why.
    ConversionFailed,
    /// The check itself could not run.
    CheckFailed,
    /// The loaded library speaks a different ABI.
    AbiMismatch,
    OutOfMemory,
};

/// What a conversion should and should not do.
///
/// `.{}` means the LIBRARY's defaults: leaving a field null asks for whatever
/// that release considers sensible, rather than a `false` this struct chose.
pub const Options = struct {
    carry_sidecar: ?bool = null,
    derive_geometry: ?bool = null,
    translate_scripts: ?bool = null,
    strict: ?bool = null,
    reencode_images: ?bool = null,
    synthesise_ocean: ?bool = null,

    /// The library's own defaults, asked of the library rather than repeated
    /// here -- so an option added upstream arrives with whatever value that
    /// release considers sensible.
    pub fn defaults() Options {
        var o: c.dg_options = undefined;
        c.dg_options_defaults(&o);
        return .{
            .carry_sidecar = o.carry_sidecar != 0,
            .derive_geometry = o.derive_geometry != 0,
            .translate_scripts = o.translate_scripts != 0,
            .strict = o.strict != 0,
            .reencode_images = o.reencode_images != 0,
            .synthesise_ocean = o.synthesise_ocean != 0,
        };
    }

    fn toC(self: Options) c.dg_options {
        var o: c.dg_options = undefined;
        c.dg_options_defaults(&o);
        if (self.carry_sidecar) |v| o.carry_sidecar = @intFromBool(v);
        if (self.derive_geometry) |v| o.derive_geometry = @intFromBool(v);
        if (self.translate_scripts) |v| o.translate_scripts = @intFromBool(v);
        if (self.strict) |v| o.strict = @intFromBool(v);
        if (self.reencode_images) |v| o.reencode_images = @intFromBool(v);
        if (self.synthesise_ocean) |v| o.synthesise_ocean = @intFromBool(v);
        return o;
    }
};

/// The native library's version, e.g. `"0.5.0"`.
pub fn version() []const u8 {
    return std.mem.span(c.dg_version_string());
}

/// The ABI the loaded library speaks.
pub fn abiVersion() c_int {
    return c.dg_abi_version();
}

/// Refuse a library that would answer with fields in the wrong places.
pub fn checkAbi() Error!void {
    if (c.dg_abi_version() != required_abi) return Error.AbiMismatch;
}

/// Why the last call failed, as the library put it.
pub fn lastError() []const u8 {
    const p = c.dg_last_error();
    return if (p == null) "" else std.mem.span(p);
}

/// Which game's layout is at this path, if either.
pub fn detect(allocator: std.mem.Allocator, path: []const u8) Error!Format {
    const z = try allocator.dupeZ(u8, path);
    defer allocator.free(z);
    return Format.fromCode(@intCast(c.dg_detect(z.ptr)));
}

/// Convert a map to `to`.
///
/// The caller owns the returned `Outcome` and must `deinit` it.
pub fn convert(
    allocator: std.mem.Allocator,
    input: []const u8,
    output: []const u8,
    to: Format,
    options: Options,
) Error!Outcome {
    const in_z = try allocator.dupeZ(u8, input);
    defer allocator.free(in_z);
    const out_z = try allocator.dupeZ(u8, output);
    defer allocator.free(out_z);

    var opts = options.toC();
    var report: ?*c.dg_report = null;

    // ZERO IS SUCCESS. It is the C convention, and reading it the other way
    // makes a finished map look like a failure -- a bug this library's own
    // Python binding once shipped.
    const rc = c.dg_convert(in_z.ptr, out_z.ptr,
        @intCast(@intFromEnum(to)), &opts, &report);
    var outcome = try collect(allocator, report);
    errdefer outcome.deinit();
    if (rc != 0) {
        outcome.deinit();
        return Error.ConversionFailed;
    }
    return outcome;
}

/// Convert a map out and back and check it returned unchanged, writing nothing
/// permanent.
///
/// NOT the same convention as `convert`: `dg_roundtrip_check` returns 1 for
/// identical, 0 for a difference and -1 for a check that could not run, so
/// reading it as zero-for-success calls a holding round trip a failure.
pub fn roundTripCheck(
    allocator: std.mem.Allocator,
    path: []const u8,
    to: Format,
    options: Options,
) Error!struct { result: RoundTrip, outcome: Outcome } {
    const z = try allocator.dupeZ(u8, path);
    defer allocator.free(z);

    var opts = options.toC();
    var report: ?*c.dg_report = null;
    const rc = c.dg_roundtrip_check(z.ptr, @intCast(@intFromEnum(to)), &opts, &report);
    var outcome = try collect(allocator, report);
    errdefer outcome.deinit();
    if (rc < 0) {
        outcome.deinit();
        return Error.CheckFailed;
    }
    return .{
        .result = if (rc == 1) .identical else .differed,
        .outcome = outcome,
    };
}

/// Reads the report, then frees it.
///
/// The free is why this is one function rather than being inlined at each call
/// site: the library hands back a report whether it succeeded or failed, and it
/// leaks unless every path releases it -- including the failure path.
fn collect(allocator: std.mem.Allocator, report: ?*c.dg_report) Error!Outcome {
    // The count is known before the first note, so this allocates exactly once
    // rather than growing a list -- which also keeps the binding clear of
    // std.ArrayList, whose shape has changed between Zig releases.
    if (report == null) {
        return .{ .notes = &.{}, .worst = .info, .allocator = allocator };
    }
    const r = report.?;
    defer c.dg_report_free(r);

    const n: usize = @intCast(c.dg_report_count(r));
    const notes = try allocator.alloc(Note, n);
    var filled: usize = 0;
    errdefer {
        var i: usize = 0;
        while (i < filled) : (i += 1) {
            allocator.free(notes[i].code);
            allocator.free(notes[i].message);
        }
        allocator.free(notes);
    }

    while (filled < n) : (filled += 1) {
        const idx: c_int = @intCast(filled);
        const code_p = c.dg_report_code(r, idx);
        const msg_p = c.dg_report_message(r, idx);
        notes[filled] = .{
            .severity = Severity.fromCode(@intCast(c.dg_report_severity(r, idx))),
            // Copied: these point into the report, which is freed on the way out.
            .code = try allocator.dupe(u8, if (code_p == null) "" else std.mem.span(code_p)),
            .message = try allocator.dupe(u8, if (msg_p == null) "" else std.mem.span(msg_p)),
        };
    }

    return .{
        .notes = notes,
        .worst = Severity.fromCode(@intCast(c.dg_report_worst(r))),
        .allocator = allocator,
    };
}
