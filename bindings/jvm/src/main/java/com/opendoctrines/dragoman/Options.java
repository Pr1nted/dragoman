package com.opendoctrines.dragoman;

/**
 * What a conversion should and should not do.
 *
 * <p>The defaults come from the library rather than being repeated here, so an
 * option added upstream arrives with whatever value that release considers
 * sensible instead of a zero this class happened to leave in place. Start from
 * {@link #defaults()} and change only what you mean to.
 */
public final class Options {
    private boolean carrySidecar;
    private boolean deriveGeometry;
    private boolean translateScripts;
    private boolean strict;
    private boolean reencodeImages;
    private boolean synthesiseOcean;

    private Options() { }

    /** The library's own defaults, asked of the library. */
    public static Options defaults() {
        Native.DgOptions raw = new Native.DgOptions();
        Native.lib().dg_options_defaults(raw);
        Options o = new Options();
        o.carrySidecar = raw.carry_sidecar != 0;
        o.deriveGeometry = raw.derive_geometry != 0;
        o.translateScripts = raw.translate_scripts != 0;
        o.strict = raw.strict != 0;
        o.reencodeImages = raw.reencode_images != 0;
        o.synthesiseOcean = raw.synthesise_ocean != 0;
        return o;
    }

    /**
     * Carry what the destination game has no field for in a sidecar beside the
     * map, so converting back restores it. Off, a round trip stops being
     * lossless.
     */
    public Options carrySidecar(boolean v) { this.carrySidecar = v; return this; }

    /**
     * Compute what the destination needs and the source never stored --
     * province adjacency and centres for GD5, the land/sea mask for Open
     * Doctrines. Off, those fields are empty and the map may not load.
     */
    public Options deriveGeometry(boolean v) { this.deriveGeometry = v; return this; }

    /** Translate scripts as well as map data, as far as each side can express the other. */
    public Options translateScripts(boolean v) { this.translateScripts = v; return this; }

    /**
     * Treat any warning as a failure. For a pipeline, where a silent partial
     * translation is worse than a stopped one.
     */
    public Options strict(boolean v) { this.strict = v; return this; }

    /** Re-encode images rather than passing the original bytes through. Makes round trips non-identical byte for byte. */
    public Options reencodeImages(boolean v) { this.reencodeImages = v; return this; }

    /**
     * Cut the water into sea provinces when converting to GD5. Open Doctrines
     * leaves its oceans unpainted and GD5 can neither draw nor sail across what
     * is not a province, so without this the sea arrives black.
     */
    public Options synthesiseOcean(boolean v) { this.synthesiseOcean = v; return this; }

    public boolean carrySidecar() { return carrySidecar; }
    public boolean deriveGeometry() { return deriveGeometry; }
    public boolean translateScripts() { return translateScripts; }
    public boolean strict() { return strict; }
    public boolean reencodeImages() { return reencodeImages; }
    public boolean synthesiseOcean() { return synthesiseOcean; }

    Native.DgOptions toNative() {
        Native.DgOptions raw = new Native.DgOptions();
        raw.carry_sidecar = carrySidecar ? 1 : 0;
        raw.derive_geometry = deriveGeometry ? 1 : 0;
        raw.translate_scripts = translateScripts ? 1 : 0;
        raw.strict = strict ? 1 : 0;
        raw.reencode_images = reencodeImages ? 1 : 0;
        raw.synthesise_ocean = synthesiseOcean ? 1 : 0;
        return raw;
    }
}
