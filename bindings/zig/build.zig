// Building the binding, not the library.
//
// The C++ is CMake's job; duplicating it here would give two build systems that
// can disagree about what was built. This compiles the Zig module and tells the
// linker where the library and its header are:
//
//     zig build test -Ddragoman-lib=/path/to/build -Ddragoman-include=/path/to/include
//
// Written for Zig 0.15. Zig's build API is not stable between releases, and
// this file is exercised by CI on Linux rather than by hand -- see README.md
// for why it cannot run on a current macOS.
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib_dir = b.option([]const u8, "dragoman-lib",
        "Directory holding libdragoman") orelse "";
    const include_dir = b.option([]const u8, "dragoman-include",
        "Directory holding dragoman/dragoman.h") orelse "../../include";

    const module = b.addModule("dragoman", .{
        .root_source_file = b.path("src/dragoman.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    module.addIncludePath(.{ .cwd_relative = include_dir });
    if (lib_dir.len > 0) module.addLibraryPath(.{ .cwd_relative = lib_dir });
    module.linkSystemLibrary("dragoman", .{});

    const test_module = b.createModule(.{
        .root_source_file = b.path("src/test.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    test_module.addImport("dragoman", module);
    test_module.addIncludePath(.{ .cwd_relative = include_dir });
    if (lib_dir.len > 0) {
        test_module.addLibraryPath(.{ .cwd_relative = lib_dir });
        test_module.addRPath(.{ .cwd_relative = lib_dir });
    }
    test_module.linkSystemLibrary("dragoman", .{});

    const tests = b.addTest(.{ .root_module = test_module });
    const run = b.addRunArtifact(tests);
    b.step("test", "Run the binding's tests against the real library").dependOn(&run.step);
}
