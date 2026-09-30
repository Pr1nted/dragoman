package com.opendoctrines.dragoman;

/**
 * Finding the native library, and saying something useful when it is not there.
 *
 * <p>JNA looks in {@code jna.library.path}, then the usual system directories,
 * then the CLASSPATH -- a jar may carry {@code /darwin-aarch64/libdragoman.dylib}
 * and friends, which is how a consumer gets a working library without
 * installing one. All three routes are left to JNA; this class exists for the
 * fourth case, where none of them found anything.
 *
 * <p>The failure that matters is not "no library" but "the WRONG library".
 * dg_abi_version is checked here, once, at load: a copy built against a
 * different ABI would otherwise answer calls with fields in the wrong places
 * and return plausible nonsense rather than failing.
 */
final class Loader {
    /** The ABI this binding was written against. */
    static final int REQUIRED_ABI = 2;

    /** Set this system property to load a specific file rather than searching. */
    static final String PATH_PROPERTY = "dragoman.library.path";

    private Loader() { }

    static Native load() {
        final String explicit = System.getProperty(PATH_PROPERTY);
        final String target = explicit != null && !explicit.isEmpty() ? explicit : "dragoman";

        final Native lib;
        try {
            lib = com.sun.jna.Native.load(target, Native.class);
        } catch (UnsatisfiedLinkError e) {
            throw new DragomanException(
                "could not load the open-dragoman native library (looked for '" + target + "'). "
                + "Put libdragoman.so / .dylib / .dll on the library path, add a jar that carries "
                + "it, or set -D" + PATH_PROPERTY + "=/path/to/libdragoman" + extension()
                + ". The original error was: " + e.getMessage(), e);
        }

        final int abi = lib.dg_abi_version();
        if (abi != REQUIRED_ABI) {
            throw new DragomanException(
                "open-dragoman " + lib.dg_version_string() + " speaks ABI " + abi
                + ", and this binding was written against ABI " + REQUIRED_ABI
                + ". They would appear to work and would disagree about what the bytes mean, "
                + "so the load is refused instead.");
        }
        return lib;
    }

    private static String extension() {
        final String os = System.getProperty("os.name", "").toLowerCase(java.util.Locale.ROOT);
        if (os.contains("mac") || os.contains("darwin")) return ".dylib";
        if (os.contains("win")) return ".dll";
        return ".so";
    }
}
