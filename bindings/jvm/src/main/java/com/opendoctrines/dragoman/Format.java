package com.opendoctrines.dragoman;

/** Which game's layout a map is in. */
public enum Format {
    /** Neither, or nothing readable at that path. */
    UNKNOWN(0),
    /** Open Doctrines: one zip archive, {@code .odmap}. */
    ODMAP(1),
    /** Greater Diplomacy 5: a directory of files. */
    GD5(2);

    private final int code;

    Format(int code) { this.code = code; }

    /** The value the C ABI uses. */
    public int code() { return code; }

    static Format fromCode(int code) {
        for (Format f : values()) {
            if (f.code == code) return f;
        }
        return UNKNOWN;
    }
}
