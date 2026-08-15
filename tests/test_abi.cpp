/* The C ABI as a caller in another language meets it: opaque handles, no C++
 * types, and every failure path returning rather than throwing.
 *
 * Written against the header alone -- no internal includes -- because that is
 * exactly what a binding has. Anything this file cannot do, Python cannot do.
 */
#include <dragoman/dragoman.h>

#include <cstring>
#include <string>

#include "Check.h"
#include "Fixture.h"

static std::string makeMap() {
    using namespace dragoman;
    const std::string path = fixture::scratch("abi.odmap");
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    if (!writeOdMap(path, w, opt, report)) return std::string();
    return path;
}

static void testDefaultsAreTheDocumentedOnes() {
    dg_options o;
    std::memset(&o, 0x5a, sizeof(o));  /* poison, so a field left unset shows */
    dg_options_defaults(&o);
    CHECK_EQ(o.carry_sidecar, 1);
    CHECK_EQ(o.derive_geometry, 1);
    CHECK_EQ(o.translate_scripts, 1);
    CHECK_EQ(o.strict, 0);
    CHECK_EQ(o.reencode_images, 0);
}

static void testDetectAndLoad() {
    const std::string path = makeMap();
    REQUIRE(!path.empty());

    CHECK_EQ(dg_detect(path.c_str()), DG_FORMAT_ODMAP);
    CHECK_EQ(std::string(dg_format_name(DG_FORMAT_ODMAP)), std::string("odmap"));
    CHECK_EQ(std::string(dg_format_name(DG_FORMAT_GD5)), std::string("gd5"));

    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* report = nullptr;
    dg_world*  world = dg_load(path.c_str(), DG_FORMAT_UNKNOWN, &opts, &report);
    REQUIRE(world != nullptr);

    CHECK_EQ(dg_world_province_count(world), 6);
    CHECK_EQ(dg_world_nation_count(world), 4);
    CHECK_EQ(dg_world_origin(world), DG_FORMAT_ODMAP);
    CHECK_EQ(std::string(dg_world_name(world)), std::string("Powder Keg Test"));

    /* The escape hatch: the whole model as text, which is how a binding reads
     * a field this ABI has no accessor for. */
    char* json = dg_world_to_json(world);
    REQUIRE(json != nullptr);
    const std::string doc(json);
    CHECK(doc.find("\"schema\"") != std::string::npos);
    CHECK(doc.find("Rhineland") != std::string::npos);
    CHECK(doc.find("\"digest\"") != std::string::npos);

    dg_report* back = nullptr;
    dg_world*  rebuilt = dg_world_from_json(json, &back);
    CHECK(rebuilt != nullptr);
    if (rebuilt) {
        CHECK_EQ(dg_world_province_count(rebuilt), 6);
        dg_world_free(rebuilt);
    }
    dg_report_free(back);
    dg_string_free(json);

    dg_report_free(report);
    dg_world_free(world);
}

/* Every one of these used to be a way to crash a binding. A null path, a file
 * that is not a map, a freed handle: each must come back as a value. */
static void testFailuresReturnRatherThanThrow() {
    dg_options opts;
    dg_options_defaults(&opts);

    CHECK_EQ(dg_detect(nullptr), DG_FORMAT_UNKNOWN);
    CHECK_EQ(dg_detect("/nonexistent/path/at/all"), DG_FORMAT_UNKNOWN);

    dg_report* report = nullptr;
    CHECK(dg_load("/nonexistent/path/at/all", DG_FORMAT_UNKNOWN, &opts, &report) == nullptr);
    CHECK(std::strlen(dg_last_error()) > 0);
    dg_report_free(report);

    report = nullptr;
    CHECK(dg_load(nullptr, DG_FORMAT_UNKNOWN, &opts, &report) == nullptr);
    dg_report_free(report);

    report = nullptr;
    CHECK(dg_world_from_json("{ this is not json", &report) == nullptr);
    dg_report_free(report);

    /* Null handles are legal arguments everywhere and answer with zero. */
    CHECK_EQ(dg_world_province_count(nullptr), 0);
    CHECK_EQ(dg_world_nation_count(nullptr), 0);
    CHECK_EQ(std::string(dg_world_name(nullptr)), std::string(""));
    CHECK_EQ(dg_report_count(nullptr), 0);
    CHECK_EQ(std::string(dg_report_code(nullptr, 0)), std::string(""));
    CHECK_EQ(dg_report_severity(nullptr, 99), -1);
    dg_world_free(nullptr);
    dg_report_free(nullptr);
    dg_string_free(nullptr);
}

/* A report is the caller's, freed by the caller, and readable after the call
 * that produced it has returned. */
static void testReportOutlivesTheCall() {
    const std::string path = makeMap();
    REQUIRE(!path.empty());
    const std::string out = fixture::scratch("abi-gd5");

    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* report = nullptr;
    CHECK_EQ(dg_convert(path.c_str(), out.c_str(), DG_FORMAT_GD5, &opts, &report), 0);
    REQUIRE(report != nullptr);
    CHECK(dg_report_count(report) > 0);

    for (int i = 0; i < dg_report_count(report); ++i) {
        CHECK(dg_report_severity(report, i) >= DG_INFO);
        CHECK(std::strlen(dg_report_code(report, i)) > 0);
        CHECK(std::strlen(dg_report_message(report, i)) > 0);
    }

    char* json = dg_report_to_json(report);
    REQUIRE(json != nullptr);
    CHECK(std::string(json).find("\"code\"") != std::string::npos);
    dg_string_free(json);
    dg_report_free(report);
}

/* The report pointer is optional; passing NULL must not leak it or crash. */
static void testReportIsOptional() {
    const std::string path = makeMap();
    REQUIRE(!path.empty());
    dg_options opts;
    dg_options_defaults(&opts);
    dg_world* world = dg_load(path.c_str(), DG_FORMAT_UNKNOWN, &opts, nullptr);
    CHECK(world != nullptr);
    dg_world_free(world);
}

int main() {
    testDefaultsAreTheDocumentedOnes();
    testDetectAndLoad();
    testFailuresReturnRatherThanThrow();
    testReportOutlivesTheCall();
    testReportIsOptional();
    return check::finish("test_abi");
}
