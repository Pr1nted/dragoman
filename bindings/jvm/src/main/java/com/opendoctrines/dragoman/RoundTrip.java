package com.opendoctrines.dragoman;

import java.util.Collections;
import java.util.List;

/**
 * Whether a map came back the way it set out.
 *
 * <p><b>This does not share {@link Dragoman#convert} convention.</b>
 * {@code dg_convert} returns 0 for success; {@code dg_roundtrip_check} returns
 * 1 for identical, 0 for a difference and -1 for a check that could not run.
 * Reading it the same way reports a holding round trip as a failure and a
 * broken one as success, and both readings look like working code.
 */
public final class RoundTrip {

    public enum Outcome {
        /** Returned with every modelled field and the raster unchanged. */
        IDENTICAL,
        /** The check ran and found a difference. */
        DIFFERED,
        /** The check itself could not run. */
        FAILED
    }

    private final Outcome outcome;
    private final List<Note> notes;
    private final Severity worst;

    RoundTrip(Outcome outcome, List<Note> notes, Severity worst) {
        this.outcome = outcome;
        this.notes = Collections.unmodifiableList(notes);
        this.worst = worst;
    }

    public Outcome outcome() { return outcome; }

    /** The one question most callers are asking. */
    public boolean held() { return outcome == Outcome.IDENTICAL; }

    public List<Note> notes() { return notes; }
    public Severity worst() { return worst; }

    @Override public String toString() { return outcome + ", " + notes.size() + " note(s)"; }
}
