package com.opendoctrines.dragoman;

import java.util.Collections;
import java.util.List;

/**
 * What a conversion did, and everything it had to say about it.
 *
 * <p>A conversion can succeed with plenty of notes -- that is the normal case,
 * because the two games do not hold the same set of facts. {@link #ok()} and
 * {@code notes().isEmpty()} are different questions.
 */
public final class Result {
    private final boolean ok;
    private final List<Note> notes;
    private final Severity worst;

    Result(boolean ok, List<Note> notes, Severity worst) {
        this.ok = ok;
        this.notes = Collections.unmodifiableList(notes);
        this.worst = worst;
    }

    /** Whether the map was written. */
    public boolean ok() { return ok; }

    /** Everything the library said, in the order it said it. */
    public List<Note> notes() { return notes; }

    /** The worst severity in {@link #notes()}, or {@link Severity#INFO} if there were none. */
    public Severity worst() { return worst; }

    /** Just the warnings and errors -- what did not cross cleanly. */
    public List<Note> problems() {
        java.util.List<Note> out = new java.util.ArrayList<Note>();
        for (Note n : notes) {
            if (n.severity() != Severity.INFO) out.add(n);
        }
        return Collections.unmodifiableList(out);
    }

    @Override public String toString() {
        return (ok ? "ok" : "failed") + ", " + notes.size() + " note(s), worst " + worst;
    }
}
