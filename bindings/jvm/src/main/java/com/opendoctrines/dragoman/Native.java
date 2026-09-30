package com.opendoctrines.dragoman;

import com.sun.jna.Library;
import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.ptr.PointerByReference;

import java.util.Arrays;
import java.util.List;

/**
 * The flat C ABI, declared once.
 *
 * <p>Package-private on purpose: everything here is a raw pointer with an
 * ownership rule attached, and the rules are easy to get wrong from outside.
 *
 * <p>TWO OWNERSHIP RULES, and both have a shape that hides the mistake.
 *
 * <p><b>A report must be freed.</b> {@code dg_convert} hands back a
 * {@code dg_report*} whether it succeeded or failed, and it leaks unless
 * {@code dg_report_free} runs. Every call site here reads what it needs and
 * frees in a {@code finally}.
 *
 * <p><b>Some strings are ours and some are not.</b> A {@code const char*} --
 * {@code dg_last_error}, {@code dg_report_message} -- belongs to the library
 * and JNA may map it straight to {@code String}, which copies. A plain
 * {@code char*} -- {@code dg_report_to_json}, {@code dg_world_to_json} -- is
 * the CALLER's to free with {@code dg_string_free}, and mapping one to
 * {@code String} leaks it silently, with nothing to see at the call site. Those
 * two are declared as {@link Pointer} for exactly that reason.
 */
interface Native extends Library {

    /** dg_options: six ints, in this order. */
    class DgOptions extends Structure {
        public int carry_sidecar;
        public int derive_geometry;
        public int translate_scripts;
        public int strict;
        public int reencode_images;
        public int synthesise_ocean;

        @Override protected List<String> getFieldOrder() {
            return Arrays.asList("carry_sidecar", "derive_geometry", "translate_scripts",
                                 "strict", "reencode_images", "synthesise_ocean");
        }
    }

    String dg_version_string();
    int dg_abi_version();

    int dg_detect(String path);
    String dg_format_name(int fmt);

    void dg_options_defaults(DgOptions out);

    int dg_convert(String inPath, String outPath, int to, DgOptions opts, PointerByReference report);
    int dg_roundtrip_check(String path, int to, DgOptions opts, PointerByReference report);

    int dg_report_count(Pointer report);
    int dg_report_severity(Pointer report, int index);
    String dg_report_code(Pointer report, int index);
    String dg_report_message(Pointer report, int index);
    int dg_report_worst(Pointer report);
    /** Caller frees: see the class comment. */
    Pointer dg_report_to_json(Pointer report);
    void dg_report_free(Pointer report);

    String dg_last_error();
    void dg_string_free(Pointer s);

    static Native lib() { return Holder.LIB; }

    /** Loaded once, on first use, and not before -- see {@link Loader}. */
    final class Holder {
        private Holder() { }
        static final Native LIB = Loader.load();
    }
}
