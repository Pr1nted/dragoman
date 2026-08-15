/* Converting a map from C, with every diagnostic printed and every handle
 * freed. Build it against the library:
 *
 *     cc example.c -I../../include -L../../build -ldragoman -lstdc++ \
 *        -Wl,-rpath,"$PWD/../../build" -o example
 *     ./example 1914.odmap base_maps/1914
 *
 * The rpath is what lets the built binary find the shared library in the build
 * tree; an installed library on the system path does not need it. `-lstdc++`
 * is needed because the core is C++ behind a C interface -- use `-lc++` with
 * clang's own standard library.
 */
#include <dragoman/dragoman.h>

#include <stdio.h>
#include <string.h>

static void print_report(dg_report* report) {
    for (int i = 0; i < dg_report_count(report); ++i) {
        const int severity = dg_report_severity(report, i);
        const char* label = severity == DG_ERROR   ? "error"
                            : severity == DG_WARNING ? "warning"
                                                     : "note";
        printf("  %-7s %s: %s\n", label, dg_report_code(report, i),
               dg_report_message(report, i));
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <input map> <output map>\n", argv[0]);
        return 2;
    }
    printf("dragoman %s (abi %d)\n", dg_version_string(), dg_abi_version());

    /* Decided by looking at the file, not at its name. */
    const dg_format from = dg_detect(argv[1]);
    if (from == DG_FORMAT_UNKNOWN) {
        fprintf(stderr, "%s is neither an .odmap nor a GD5 map directory\n", argv[1]);
        return 1;
    }
    const dg_format to = from == DG_FORMAT_ODMAP ? DG_FORMAT_GD5 : DG_FORMAT_ODMAP;
    printf("converting %s -> %s\n", dg_format_name(from), dg_format_name(to));

    dg_options opts;
    dg_options_defaults(&opts);

    /* Worth looking at first: how big the map is, and what it is called. */
    dg_report* report = NULL;
    dg_world*  world = dg_load(argv[1], from, &opts, &report);
    if (!world) {
        fprintf(stderr, "could not read %s: %s\n", argv[1], dg_last_error());
        print_report(report);   /* the report is yours even when the call failed */
        dg_report_free(report);
        return 1;
    }
    printf("%s: %d provinces, %d nations, %d scripts\n", dg_world_name(world),
           dg_world_province_count(world), dg_world_nation_count(world),
           dg_world_script_count(world));
    dg_report_free(report);

    report = NULL;
    if (dg_save(world, argv[2], to, &opts, &report) != 0) {
        fprintf(stderr, "could not write %s: %s\n", argv[2], dg_last_error());
        print_report(report);
        dg_report_free(report);
        dg_world_free(world);
        return 1;
    }
    print_report(report);
    dg_report_free(report);
    dg_world_free(world);

    printf("wrote %s\n", argv[2]);
    return 0;
}
