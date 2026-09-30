package com.opendoctrines.dragoman;

import java.util.Objects;

/**
 * One thing the library has to say about a conversion.
 *
 * <p>The {@link #code()} is the stable half -- {@code "script.unsupported"},
 * {@code "gd5.ocean"} -- and is what to branch on. The {@link #message()} is a
 * sentence for a person and is reworded between releases, so matching on it is
 * a test that breaks for no reason.
 */
public final class Note {
    private final Severity severity;
    private final String code;
    private final String message;

    Note(Severity severity, String code, String message) {
        this.severity = severity;
        this.code = code == null ? "" : code;
        this.message = message == null ? "" : message;
    }

    public Severity severity() { return severity; }
    public String code() { return code; }
    public String message() { return message; }

    @Override public String toString() { return "[" + code + "] " + message; }

    @Override public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Note)) return false;
        Note n = (Note) o;
        return severity == n.severity && code.equals(n.code) && message.equals(n.message);
    }

    @Override public int hashCode() { return Objects.hash(severity, code, message); }
}
