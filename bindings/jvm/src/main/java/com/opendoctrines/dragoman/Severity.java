package com.opendoctrines.dragoman;

/** How much a report entry matters. */
public enum Severity {
    /** Something worth knowing; the translation is exact. */
    INFO(0),
    /** Translated, but not exactly. The message says what was lost. */
    WARNING(1),
    /** Not translated. */
    ERROR(2);

    private final int code;

    Severity(int code) { this.code = code; }

    public int code() { return code; }

    static Severity fromCode(int code) {
        for (Severity s : values()) {
            if (s.code == code) return s;
        }
        return INFO;
    }
}
