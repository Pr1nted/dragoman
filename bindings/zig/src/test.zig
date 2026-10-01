//! The binding, against the real library.
//!
//! A real conversion needs a real map, which is not in this repository -- Open
//! Doctrines' maps are its own and GD5's are GPL. Set DRAGOMAN_TEST_MAP and the
//! last test runs; without it, it SKIPS and says so rather than passing quietly.
const std = @import("std");
const dragoman = @import("dragoman");

test "the library loads and agrees on the ABI" {
    try std.testing.expect(dragoman.version().len > 0);
    try dragoman.checkAbi();
}

test "defaults come from the library" {
    // Asked for, not repeated: an all-false struct would mean the call never
    // reached the library, which is the mistake this catches.
    const o = dragoman.Options.defaults();
    try std.testing.expectEqual(@as(?bool, true), o.carry_sidecar);
    try std.testing.expectEqual(@as(?bool, true), o.derive_geometry);
    try std.testing.expectEqual(@as(?bool, true), o.translate_scripts);
    try std.testing.expectEqual(@as(?bool, false), o.strict);
    try std.testing.expectEqual(@as(?bool, true), o.synthesise_ocean);
}

test "something that is not a map is unknown" {
    const a = std.testing.allocator;
    const path = "/tmp/dragoman-zig-not-a-map.txt";
    {
        const f = try std.fs.cwd().createFile(path, .{});
        defer f.close();
        try f.writeAll("hello");
    }
    defer std.fs.cwd().deleteFile(path) catch {};
    try std.testing.expectEqual(dragoman.Format.unknown, try dragoman.detect(a, path));
}

// The polarity test: if zero were read as failure, this would return an Outcome.
test "a failed conversion is an error" {
    const a = std.testing.allocator;
    const result = dragoman.convert(a, "/does/not/exist/anywhere.odmap",
                                    "/tmp/dragoman-zig-should-not-appear", .gd5, .{});
    try std.testing.expectError(dragoman.Error.ConversionFailed, result);
    try std.testing.expect(dragoman.lastError().len > 0);
}

// Unciv is reachable, and the grid size is honoured.
//
// Its own test because Unciv arrived in the C ABI and in NO binding: this one
// had no `.unciv` at all, so a format the library had supported for a release
// was one no Zig caller could name.
test "unciv is reachable and the grid is honoured" {
    const a = std.testing.allocator;
    const map = std.process.getEnvVarOwned(a, "DRAGOMAN_TEST_MAP") catch {
        std.debug.print("skip: set DRAGOMAN_TEST_MAP=<a map> to run a real conversion\n", .{});
        return;
    };
    defer a.free(map);

    if (try dragoman.detect(a, map) != .odmap) {
        std.debug.print("skip: DRAGOMAN_TEST_MAP is not an .odmap\n", .{});
        return;
    }

    const out = "/tmp/dragoman-zig-unciv.json";
    std.fs.cwd().deleteFile(out) catch {};
    defer std.fs.cwd().deleteFile(out) catch {};

    var outcome = try dragoman.convertUnciv(a, map, out, 24, 15, .{});
    defer outcome.deinit();

    const text = try std.fs.cwd().readFileAlloc(a, out, 64 * 1024 * 1024);
    defer a.free(text);
    // 24x15 is Unciv's "Tiny". Counting positions keeps this free of a JSON
    // parser the binding does not otherwise need.
    var tiles: usize = 0;
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, text, i, "\"position\"")) |at| : (i = at + 1) tiles += 1;
    try std.testing.expectEqual(@as(usize, 24 * 15), tiles);
}

test "a real map converts and returns" {
    const a = std.testing.allocator;
    const map = std.process.getEnvVarOwned(a, "DRAGOMAN_TEST_MAP") catch {
        std.debug.print("skip: set DRAGOMAN_TEST_MAP=<a map> to run a real conversion\n", .{});
        return;
    };
    defer a.free(map);

    const from = try dragoman.detect(a, map);
    try std.testing.expect(from != .unknown);
    const to: dragoman.Format = if (from == .odmap) .gd5 else .odmap;

    const out = "/tmp/dragoman-zig-out";
    std.fs.cwd().deleteTree(out) catch {};
    defer std.fs.cwd().deleteTree(out) catch {};

    var outcome = try dragoman.convert(a, map, out, to, .{});
    defer outcome.deinit();
    for (outcome.notes) |n| {
        try std.testing.expect(n.code.len > 0);
        try std.testing.expect(n.message.len > 0);
    }

    // NOT read the way convert is: 1 is identical, 0 is differed.
    var rt = try dragoman.roundTripCheck(a, map, to, .{});
    defer rt.outcome.deinit();
    try std.testing.expectEqual(dragoman.RoundTrip.identical, rt.result);
}
