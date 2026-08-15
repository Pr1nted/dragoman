/* The property the whole library is for: a map that crosses and comes back is
 * the map that set out.
 *
 * "The same" is defined precisely, because it cannot mean byte-identical at
 * the container level and should not pretend to. Two zip files holding
 * identical members are different files -- deflate is not required to be
 * reproducible across implementations, and Open Doctrines packs with Python's
 * zlib while this library packs with miniz. What is asserted instead is that
 * every province, nation, relation, claim, script and carried file returns
 * unchanged, and that the province raster hashes to the same value: the map
 * the game loads is the same map, byte for byte in every field it reads.
 */
#include <dragoman/dragoman.h>

#include <cstdlib>
#include <filesystem>

#include "Check.h"
#include "Fixture.h"
#include "ModelJson.h"

using namespace dragoman;
namespace fs = std::filesystem;

static void printReport(dg_report* r) {
    for (int i = 0; i < dg_report_count(r); ++i) {
        if (dg_report_severity(r, i) == DG_INFO) continue;
        std::fprintf(stderr, "       [%s] %s\n", dg_report_code(r, i), dg_report_message(r, i));
    }
}

/* Write the fixture out as a real .odmap, so everything below goes through the
 * same file the game would open rather than through an in-memory shortcut. */
static std::string writeFixtureOdmap() {
    const std::string path = fixture::scratch("fixture.odmap");
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    if (!writeOdMap(path, w, opt, report)) return std::string();
    return path;
}

static void testOdToGd5AndBack() {
    const std::string odmap = writeFixtureOdmap();
    REQUIRE(!odmap.empty());

    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* report = nullptr;
    const int rc = dg_roundtrip_check(odmap.c_str(), DG_FORMAT_GD5, &opts, &report);
    if (rc != 1) printReport(report);
    CHECK_EQ(rc, 1);
    dg_report_free(report);
}

static void testGd5ToOdAndBack() {
    const std::string odmap = writeFixtureOdmap();
    REQUIRE(!odmap.empty());

    /* Start from a GD5 map this time, by making one first. The map that comes
     * out of that conversion is a legitimate GD5 map in its own right, and the
     * question is whether it survives the return leg. */
    const std::string gd5 = fixture::scratch("fixture-gd5");
    fs::remove_all(gd5);
    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* conv = nullptr;
    REQUIRE(dg_convert(odmap.c_str(), gd5.c_str(), DG_FORMAT_GD5, &opts, &conv) == 0);
    dg_report_free(conv);

    dg_report* report = nullptr;
    const int rc = dg_roundtrip_check(gd5.c_str(), DG_FORMAT_ODMAP, &opts, &report);
    if (rc != 1) printReport(report);
    CHECK_EQ(rc, 1);
    dg_report_free(report);
}

/* The individual facts, checked by name rather than by digest, so a failure
 * says which one moved. */
static void testFieldsSurviveTheCrossing() {
    const std::string odmap = writeFixtureOdmap();
    REQUIRE(!odmap.empty());
    const std::string gd5 = fixture::scratch("fields-gd5");
    fs::remove_all(gd5);

    Options opt;
    Report report;
    World source;
    REQUIRE(readOdMap(odmap, opt, source, report));

    World crossing = source;
    scriptsToEvents(crossing.scripts, "GER", crossing.events, report);
    REQUIRE(writeGd5Map(gd5, crossing, opt, report));

    World there;
    REQUIRE(readGd5Map(gd5, opt, there, report));

    CHECK_EQ(there.provinces.size(), source.provinces.size());
    CHECK_EQ(there.nations.size(), source.nations.size());
    CHECK_EQ(there.width, source.width);
    CHECK_EQ(there.height, source.height);
    CHECK_EQ(rasterDigest(there.raster), rasterDigest(source.raster));
    CHECK_EQ(there.date.year, 1914);
    CHECK_EQ(there.date.month, 7);

    /* A nation with no ISO code of its own was given one on the way out; the
     * sidecar has to hand the same one back, or the map acquires a second
     * country every time it crosses. */
    const Nation* invented = there.findNation("FIU");
    CHECK(invented != nullptr);
    if (invented) CHECK_EQ(invented->name, std::string("Free Imperial City of Ulm"));

    const Province* bohemia = there.findProvince(3);
    REQUIRE(bohemia != nullptr);
    CHECK_EQ(bohemia->owner, std::string("AUH"));
    CHECK_EQ(bohemia->name, std::string("Bohemia"));
    /* Open Doctrines keeps claims on the nation and GD5 on the province; the
     * same claim has to be findable through whichever shape the reader used. */
    CHECK(std::find(bohemia->cores.begin(), bohemia->cores.end(), "GER") != bohemia->cores.end());

    const Province* sea = there.findProvince(1);
    REQUIRE(sea != nullptr);
    CHECK(sea->is_sea);
    CHECK(sea->owner.empty());

    /* GD5 needs adjacency and centres; Open Doctrines never stored either, so
     * the crossing had to derive them. */
    const Province* rhineland = there.findProvince(2);
    REQUIRE(rhineland != nullptr);
    CHECK(rhineland->has_neighbors);
    CHECK(rhineland->has_center);
    CHECK(!rhineland->neighbors.empty());

    /* Population has no GD5 field at all, so it can only come back through
     * the sidecar -- which is the whole point of carrying one. */
    World home;
    const std::string back = fixture::scratch("fields-back.odmap");
    World returning = there;
    REQUIRE(writeOdMap(back, returning, opt, report));
    REQUIRE(readOdMap(back, opt, home, report));

    const Province* homeRhine = home.findProvince(2);
    REQUIRE(homeRhine != nullptr);
    CHECK_EQ(homeRhine->population, int64_t(200000));
    CHECK_EQ(homeRhine->port_level, 1);
    const Nation* ger = home.findNation("GER");
    REQUIRE(ger != nullptr);
    CHECK(ger->relations.count("RUS") && ger->relations.at("RUS").non_aggression);
}

/* Turning the sidecar off is supposed to be a real choice with a real cost:
 * a smaller file that no longer round trips. Asserting that keeps the two
 * modes from quietly becoming the same thing. */
static void testWithoutSidecarLosesData() {
    const std::string odmap = writeFixtureOdmap();
    REQUIRE(!odmap.empty());

    dg_options opts;
    dg_options_defaults(&opts);
    opts.carry_sidecar = 0;
    dg_report* report = nullptr;
    const int rc = dg_roundtrip_check(odmap.c_str(), DG_FORMAT_GD5, &opts, &report);
    CHECK_EQ(rc, 0);
    dg_report_free(report);
}

/* The shipped maps of both games, when whoever is running the suite has them.
 * These are the only tests that touch a map this project did not invent, and
 * they are the ones that catch a format assumption that only holds on a
 * fixture. Neither game's maps are redistributable here, so CI skips them. */
static void testRealMapsIfAvailable() {
    if (const char* od = std::getenv("DRAGOMAN_OD_MAP")) {
        std::printf("     real .odmap: %s\n", od);
        dg_options opts;
        dg_options_defaults(&opts);
        dg_report* report = nullptr;
        const int rc = dg_roundtrip_check(od, DG_FORMAT_GD5, &opts, &report);
        if (rc != 1) printReport(report);
        CHECK_EQ(rc, 1);
        dg_report_free(report);
    }
    if (const char* gd = std::getenv("DRAGOMAN_GD5_MAP")) {
        std::printf("     real GD5 map: %s\n", gd);
        dg_options opts;
        dg_options_defaults(&opts);
        dg_report* report = nullptr;
        const int rc = dg_roundtrip_check(gd, DG_FORMAT_ODMAP, &opts, &report);
        if (rc != 1) printReport(report);
        CHECK_EQ(rc, 1);
        dg_report_free(report);
    }
}

int main() {
    testOdToGd5AndBack();
    testGd5ToOdAndBack();
    testFieldsSurviveTheCrossing();
    testWithoutSidecarLosesData();
    testRealMapsIfAvailable();
    return check::finish("test_roundtrip");
}
