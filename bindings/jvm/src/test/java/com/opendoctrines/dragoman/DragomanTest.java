package com.opendoctrines.dragoman;

import java.io.File;
import java.nio.file.Files;
import java.util.List;

/**
 * The binding, against the real library.
 *
 * <p>No test framework: the suite is small, the assertions are plain, and a
 * dependency here would be one more thing to install before it runs. Same
 * bargain as the C++ side.
 *
 * <p>Point it at a map:
 * <pre>java -Ddragoman.library.path=... -Ddragoman.test.map=world.odmap DragomanTest</pre>
 * With no map it checks everything that does not need one and says which parts
 * it skipped, rather than passing quietly and implying coverage it lacks.
 */
public final class DragomanTest {
    private static int failures = 0;

    private static void check(boolean cond, String what) {
        if (cond) {
            System.out.println("  ok    " + what);
        } else {
            System.out.println("  FAIL  " + what);
            failures++;
        }
    }

    private static void checkEq(Object got, Object want, String what) {
        if (got == null ? want == null : got.equals(want)) {
            System.out.println("  ok    " + what);
        } else {
            System.out.println("  FAIL  " + what + ": got " + got + ", wanted " + want);
            failures++;
        }
    }

    /** The library answers at all, and it is the one this binding expects. */
    private static void testTheLibraryLoadsAndAgreesOnTheAbi() {
        String v = Dragoman.version();
        check(v != null && !v.isEmpty(), "the library reports a version (" + v + ")");
        checkEq(Dragoman.abiVersion(), Loader.REQUIRED_ABI,
                "it speaks the ABI this binding was written against");
    }

    /** The defaults are the LIBRARY's, not this class's idea of them. */
    private static void testDefaultsComeFromTheLibrary() {
        Options o = Options.defaults();
        check(o.carrySidecar(), "carry_sidecar defaults on -- a round trip is lossless");
        check(o.deriveGeometry(), "derive_geometry defaults on");
        check(o.translateScripts(), "translate_scripts defaults on");
        check(!o.strict(), "strict defaults off");
        check(!o.reencodeImages(), "reencode_images defaults off");
        check(o.synthesiseOcean(), "synthesise_ocean defaults on");
    }

    /** A path that is neither game's is UNKNOWN, not an exception. */
    private static void testDetectOnSomethingThatIsNotAMap() throws Exception {
        File tmp = File.createTempFile("dragoman-not-a-map", ".txt");
        try {
            Files.write(tmp.toPath(), "hello".getBytes("UTF-8"));
            checkEq(Dragoman.detect(tmp), Format.UNKNOWN, "a text file is not a map");
        } finally {
            tmp.delete();
        }
    }

    /**
     * A conversion that cannot work reports WHY.
     *
     * <p>This is the assertion that catches the polarity bug: if zero were read
     * as failure and non-zero as success, this would come back ok() == true.
     */
    private static void testAFailedConversionSaysWhy() {
        File nowhere = new File("/does/not/exist/anywhere.odmap");
        File out = new File(System.getProperty("java.io.tmpdir"), "dragoman-should-not-appear");
        Result r = Dragoman.convert(nowhere, out, Format.GD5);
        check(!r.ok(), "converting a path that does not exist fails");
        check(!r.notes().isEmpty(), "and does not fail silently");
        checkEq(r.worst(), Severity.ERROR, "the worst severity is an error");
        check(!out.exists(), "nothing was written");
    }

    /** The real thing, when a map is available. */
    private static void testARealConversion(File map) {
        File out = new File(System.getProperty("java.io.tmpdir"),
                            "dragoman-jvm-" + System.nanoTime());
        Format from = Dragoman.detect(map);
        check(from != Format.UNKNOWN, "the map is recognised (" + from + ")");

        Format to = from == Format.ODMAP ? Format.GD5 : Format.ODMAP;
        Result r = Dragoman.convert(map, out, to);
        check(r.ok(), "it converts to " + to);
        if (!r.ok()) {
            for (Note n : r.notes()) System.out.println("        " + n);
            return;
        }
        check(out.exists(), "the output exists");

        // A conversion normally has plenty to say; the point is that the notes
        // arrive with their stable codes attached, which is what a caller
        // branches on.
        List<Note> notes = r.notes();
        check(!notes.isEmpty(), "it reported what it did (" + notes.size() + " note(s))");
        boolean coded = true;
        for (Note n : notes) coded = coded && !n.code().isEmpty() && !n.message().isEmpty();
        check(coded, "every note has both a code and a message");
        System.out.println("        worst severity: " + r.worst());

        // NOT read the way convert() is read. dg_roundtrip_check returns 1 for
        // identical, 0 for a difference and -1 for a check that could not run,
        // so treating 0 as success -- as the rest of the ABI does -- calls a
        // holding round trip a failure. The Rust binding's test caught exactly
        // that, in code that had already shipped here.
        RoundTrip rt = Dragoman.roundTripCheck(map, to, Options.defaults());
        checkEq(rt.outcome(), RoundTrip.Outcome.IDENTICAL,
                "the map comes back unchanged (and 1, not 0, means identical)");
    }

    /**
     * Unciv is reachable, and the grid size is honoured.
     *
     * Its own test because Unciv arrived in the C ABI and in NO binding: this
     * one had no Format.UNCIV at all, so a format the library had supported
     * for a release was one no JVM caller could name.
     */
    private static void testUncivIsReachable(File map) throws Exception {
        checkEq(Format.UNCIV.code(), 3, "the ABI's code for Unciv");
        if (Dragoman.detect(map) != Format.ODMAP) {
            System.out.println("  skip  the Unciv grid: the test map is not an .odmap");
            return;
        }
        File out = File.createTempFile("dragoman-jvm-unciv", ".json");
        out.deleteOnExit();
        Result r = Dragoman.convertUnciv(map, out, 24, 15, Options.defaults());
        check(r.ok(), "it converts to Unciv");
        if (!r.ok()) return;

        String json = new String(java.nio.file.Files.readAllBytes(out.toPath()), "UTF-8");
        // 24x15 is Unciv's "Tiny". Counting positions keeps this free of a
        // JSON parser the binding does not otherwise need.
        int tiles = json.split("\"position\"", -1).length - 1;
        checkEq(tiles, 24 * 15, "the grid size was honoured");
    }

    public static void main(String[] args) throws Exception {
        System.out.println("=== the JVM binding ===");
        testTheLibraryLoadsAndAgreesOnTheAbi();
        testDefaultsComeFromTheLibrary();
        testDetectOnSomethingThatIsNotAMap();
        testAFailedConversionSaysWhy();

        String mapPath = System.getProperty("dragoman.test.map");
        if (mapPath == null || mapPath.isEmpty()) {
            System.out.println("  skip  a real conversion: set -Ddragoman.test.map=<a map> to run it");
        } else {
            testARealConversion(new File(mapPath));
            testUncivIsReachable(new File(mapPath));
        }

        System.out.println(failures == 0 ? "all passed" : failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}
