package com.opendoctrines.dragoman;

/**
 * A conversion that did not happen, or a library that could not be loaded.
 *
 * <p>Unchecked, because every call site that could sensibly recover is one that
 * chose to ask for a conversion in the first place -- and the ones that cannot
 * should not be made to write a catch block that rethrows.
 */
public class DragomanException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public DragomanException(String message) { super(message); }
    public DragomanException(String message, Throwable cause) { super(message, cause); }
}
