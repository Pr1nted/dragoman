/* dragoman -- convert a map between Open Doctrines and Greater Diplomacy 5.
 *
 * Built on the C ABI rather than on the C++ internals on purpose: if the
 * command line tool can do it, so can a binding, and anything the CLI needs
 * that the ABI cannot express is a gap in the ABI.
 */
#include <dragoman/dragoman.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void usage() {
    std::printf(
        "dragoman %s -- Open Doctrines <-> Greater Diplomacy 5 map translation\n"
        "\n"
        "usage:\n"
        "  dragoman convert <in> <out> [--to odmap|gd5] [options]\n"
        "  dragoman roundtrip <map> [--to odmap|gd5] [options]\n"
        "  dragoman inspect <map> [--json]\n"
        "  dragoman detect <path>\n"
        "  dragoman version\n"
        "\n"
        "options:\n"
        "  --to <fmt>        target format; inferred from the source when omitted\n"
        "  --no-sidecar      do not carry data the target game has no field for\n"
        "                    (smaller output, and the round trip stops being lossless)\n"
        "  --no-geometry     do not derive province adjacency and centres\n"
        "  --no-scripts      do not translate scripts or scripted events\n"
        "  --no-ocean        do not invent sea provinces for a game whose maps\n"
        "                    leave their water unpainted (GD5 then renders it black\n"
        "                    and no fleet can move)\n"
        "  --reencode        re-encode images instead of passing them through\n"
        "  --strict          treat any warning as a failure\n"
        "  --quiet           print only warnings and errors\n",
        dg_version_string());
}

dg_format formatByName(const char* name) {
    if (!name) return DG_FORMAT_UNKNOWN;
    if (std::strcmp(name, "odmap") == 0 || std::strcmp(name, "od") == 0) return DG_FORMAT_ODMAP;
    if (std::strcmp(name, "gd5") == 0 || std::strcmp(name, "gd") == 0) return DG_FORMAT_GD5;
    return DG_FORMAT_UNKNOWN;
}

int printReport(dg_report* report, bool quiet) {
    const int n = dg_report_count(report);
    for (int i = 0; i < n; ++i) {
        const int sev = dg_report_severity(report, i);
        if (quiet && sev == DG_INFO) continue;
        const char* label = sev == DG_ERROR ? "error" : sev == DG_WARNING ? "warning" : "note";
        std::fprintf(sev == DG_INFO ? stdout : stderr, "  %-7s %s: %s\n", label,
                     dg_report_code(report, i), dg_report_message(report, i));
    }
    return dg_report_worst(report);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }

    const std::string command = argv[1];
    if (command == "version" || command == "--version") {
        std::printf("dragoman %s (abi %d)\n", dg_version_string(), dg_abi_version());
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        usage();
        return 0;
    }

    dg_options opts;
    dg_options_defaults(&opts);
    dg_format to = DG_FORMAT_UNKNOWN;
    bool quiet = false;
    bool asJson = false;

    std::string positional[2];
    int positionalCount = 0;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--to" && i + 1 < argc) { to = formatByName(argv[++i]); }
        else if (arg == "--no-sidecar") { opts.carry_sidecar = 0; }
        else if (arg == "--no-geometry") { opts.derive_geometry = 0; }
        else if (arg == "--no-scripts") { opts.translate_scripts = 0; }
        else if (arg == "--no-ocean") { opts.synthesise_ocean = 0; }
        else if (arg == "--reencode") { opts.reencode_images = 1; }
        else if (arg == "--strict") { opts.strict = 1; }
        else if (arg == "--quiet") { quiet = true; }
        else if (arg == "--json") { asJson = true; }
        else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "dragoman: unknown option %s\n", arg.c_str());
            return 2;
        } else if (positionalCount < 2) {
            positional[positionalCount++] = arg;
        }
    }

    if (command == "detect") {
        if (positionalCount < 1) { usage(); return 2; }
        const dg_format f = dg_detect(positional[0].c_str());
        std::printf("%s\n", dg_format_name(f));
        return f == DG_FORMAT_UNKNOWN ? 1 : 0;
    }

    if (command == "inspect") {
        if (positionalCount < 1) { usage(); return 2; }
        dg_report* report = nullptr;
        dg_world*  world = dg_load(positional[0].c_str(), DG_FORMAT_UNKNOWN, &opts, &report);
        if (!world) {
            std::fprintf(stderr, "dragoman: %s\n", dg_last_error());
            printReport(report, quiet);
            dg_report_free(report);
            return 1;
        }
        if (asJson) {
            char* json = dg_world_to_json(world);
            std::printf("%s\n", json ? json : "{}");
            dg_string_free(json);
        } else {
            std::printf("%s\n", dg_world_name(world));
            std::printf("  format     %s\n", dg_format_name(dg_world_origin(world)));
            std::printf("  provinces  %d\n", dg_world_province_count(world));
            std::printf("  nations    %d\n", dg_world_nation_count(world));
            std::printf("  scripts    %d\n", dg_world_script_count(world));
        }
        printReport(report, /*quiet=*/asJson ? true : quiet);
        dg_report_free(report);
        dg_world_free(world);
        return 0;
    }

    if (command == "convert") {
        if (positionalCount < 2) { usage(); return 2; }
        if (to == DG_FORMAT_UNKNOWN) {
            /* With no target named, the useful default is "the other one". */
            const dg_format from = dg_detect(positional[0].c_str());
            if (from == DG_FORMAT_UNKNOWN) {
                std::fprintf(stderr, "dragoman: cannot tell what %s is; pass --to\n",
                             positional[0].c_str());
                return 2;
            }
            to = from == DG_FORMAT_ODMAP ? DG_FORMAT_GD5 : DG_FORMAT_ODMAP;
        }

        dg_report* report = nullptr;
        const int rc = dg_convert(positional[0].c_str(), positional[1].c_str(), to, &opts, &report);
        if (rc != 0) {
            std::fprintf(stderr, "dragoman: %s\n", dg_last_error());
            printReport(report, quiet);
            dg_report_free(report);
            return 1;
        }
        if (!quiet) {
            std::printf("converted %s -> %s (%s)\n", positional[0].c_str(),
                        positional[1].c_str(), dg_format_name(to));
        }
        const int worst = printReport(report, quiet);
        dg_report_free(report);
        return worst >= DG_ERROR ? 1 : 0;
    }

    if (command == "roundtrip") {
        if (positionalCount < 1) { usage(); return 2; }
        if (to == DG_FORMAT_UNKNOWN) {
            const dg_format from = dg_detect(positional[0].c_str());
            to = from == DG_FORMAT_ODMAP ? DG_FORMAT_GD5 : DG_FORMAT_ODMAP;
        }
        dg_report* report = nullptr;
        const int rc = dg_roundtrip_check(positional[0].c_str(), to, &opts, &report);
        if (rc < 0) {
            std::fprintf(stderr, "dragoman: %s\n", dg_last_error());
            printReport(report, quiet);
            dg_report_free(report);
            return 2;
        }
        std::printf("%s\n", rc == 1
            ? "round trip preserved every modelled field and the province raster"
            : "round trip changed the map -- see below");
        printReport(report, quiet);
        dg_report_free(report);
        return rc == 1 ? 0 : 1;
    }

    usage();
    return 2;
}
