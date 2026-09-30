package com.opendoctrines.dragoman;

import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Open Doctrines maps to Greater Diplomacy 5 maps, and back.
 *
 * <pre>{@code
 * Result r = Dragoman.convert(new File("world.odmap"), new File("out"), Format.GD5);
 * for (Note n : r.problems()) System.out.println(n);
 * }</pre>
 *
 * <p>From Kotlin the same thing reads:
 *
 * <pre>{@code
 * val r = Dragoman.convert(File("world.odmap"), File("out"), Format.GD5)
 * r.problems().forEach(::println)
 * }</pre>
 *
 * <p>The conversion is lossy in both directions and the losses are not the same
 * one way as the other. Whatever could not cross is in {@link Result#notes()};
 * a conversion that succeeds with warnings is the normal case, not a problem.
 */
public final class Dragoman {

    private Dragoman() { }

    /** The native library's version, e.g. {@code "0.5.0"}. */
    public static String version() {
        return Native.lib().dg_version_string();
    }

    /** The ABI the loaded library speaks. Checked at load; here for reporting. */
    public static int abiVersion() {
        return Native.lib().dg_abi_version();
    }

    /** Which game's layout is at this path, if either. */
    public static Format detect(File path) {
        if (path == null) throw new IllegalArgumentException("path");
        return Format.fromCode(Native.lib().dg_detect(path.getPath()));
    }

    /** Convert with the library's default options. */
    public static Result convert(File in, File out, Format to) {
        return convert(in, out, to, Options.defaults());
    }

    /**
     * Convert a map from whichever format it is in to {@code to}.
     *
     * @return what happened, including everything that did not cross cleanly.
     *         Never null; check {@link Result#ok()}.
     */
    public static Result convert(File in, File out, Format to, Options options) {
        if (in == null) throw new IllegalArgumentException("in");
        if (out == null) throw new IllegalArgumentException("out");
        if (to == null || to == Format.UNKNOWN) {
            throw new IllegalArgumentException("a target format is required");
        }
        if (options == null) options = Options.defaults();

        final PointerByReference report = new PointerByReference();
        // ZERO IS SUCCESS. It is the C convention, and reading it the other way
        // makes a finished map look like a failure -- which is a bug this
        // library's own Python binding once shipped.
        final int rc = Native.lib().dg_convert(in.getPath(), out.getPath(), to.code(),
                                               options.toNative(), report);
        return collect(rc == 0, report.getValue(), rc);
    }

    /**
     * Convert a map out and back and check it returned unchanged, writing
     * nothing permanent. The property the whole library is for.
     *
     * <p>Note the return type. This call does NOT use the same convention as
     * {@link #convert}: see {@link RoundTrip}.
     */
    public static RoundTrip roundTripCheck(File path, Format to, Options options) {
        if (path == null) throw new IllegalArgumentException("path");
        if (to == null || to == Format.UNKNOWN) {
            throw new IllegalArgumentException("a target format is required");
        }
        if (options == null) options = Options.defaults();
        final PointerByReference report = new PointerByReference();
        // 1 identical, 0 differed, -1 could not run.
        final int rc = Native.lib().dg_roundtrip_check(path.getPath(), to.code(),
                                                       options.toNative(), report);
        final Result r = collect(rc >= 0, report.getValue(), rc);
        final RoundTrip.Outcome outcome =
            rc == 1 ? RoundTrip.Outcome.IDENTICAL
                    : rc == 0 ? RoundTrip.Outcome.DIFFERED
                              : RoundTrip.Outcome.FAILED;
        return new RoundTrip(outcome, r.notes(), r.worst());
    }

    /**
     * Reads the report, then frees it.
     *
     * <p>The free is the whole reason this is one function rather than being
     * inlined at each call site: the library hands back a report whether it
     * succeeded or failed, and it leaks unless every path releases it.
     */
    private static Result collect(boolean ok, Pointer raw, int rc) {
        final List<Note> notes = new ArrayList<Note>();
        Severity worst = Severity.INFO;
        try {
            if (raw != null) {
                final int n = Native.lib().dg_report_count(raw);
                for (int i = 0; i < n; i++) {
                    notes.add(new Note(Severity.fromCode(Native.lib().dg_report_severity(raw, i)),
                                       Native.lib().dg_report_code(raw, i),
                                       Native.lib().dg_report_message(raw, i)));
                }
                worst = Severity.fromCode(Native.lib().dg_report_worst(raw));
            }
        } finally {
            if (raw != null) Native.lib().dg_report_free(raw);
        }

        if (!ok && notes.isEmpty()) {
            // A failure the library did not narrate. dg_last_error is the only
            // thing left that knows why, and losing it here would leave the
            // caller with a bare false.
            final String err = Native.lib().dg_last_error();
            notes.add(new Note(Severity.ERROR, "dragoman.failed",
                               err == null || err.isEmpty()
                                   ? "the conversion failed without saying why (code " + rc + ")"
                                   : err));
            worst = Severity.ERROR;
        }
        return new Result(ok, notes, worst);
    }
}
