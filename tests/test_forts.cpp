/* Forts, which cross for the first time.
 *
 * Greater Diplomacy 5 added forts as a building -- "Fort Lvl N", one per
 * province -- after this library was written. Open Doctrines has always had a
 * per-province `fortification`. So the two describe the same thing now, and
 * these check that it survives the trip in both directions and that the
 * ladders' different heights are handled by scaling rather than by clamping.
 *
 * The heights: GD5's FORT_MAX_LEVEL is 20, Open Doctrines clamps to 5.
 */
#include <dragoman/dragoman.h>

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

#include "Check.h"
#include "Fixture.h"
#include "ModelJson.h"
#include "Support.h"

using namespace dragoman;
namespace fs = std::filesystem;

/* Reads the fort level back out of a written GD5 map the way GD5 does: the
 * highest "Fort Lvl N" building on the province. Deliberately a separate
 * implementation from the library's, so a bug in that one does not agree with
 * itself here. */
static int fortInWrittenMap(const std::string& gd5Dir, int64_t provinceId) {
    Options opt;
    Report report;
    World read;
    if (!readGd5Map(gd5Dir, opt, read, report)) return -1;
    const Province* p = read.findProvince(provinceId);
    if (!p) return -1;

    int level = 0;
    const auto bld = p->extra.find("gd5_buildings");
    if (bld != p->extra.end() && bld->is_array()) {
        for (const auto& b : *bld) {
            if (!b.is_string()) continue;
            const std::string name = b.get<std::string>();
            const std::string prefix = "Fort Lvl ";
            if (name.rfind(prefix, 0) != 0) continue;
            level = std::max(level, std::atoi(name.c_str() + prefix.size()));
        }
    }
    return level;
}


/* Puts a fort on a written map the way a consistent GD5 map carries one.
 *
 * BOTH FILES. map_data.json is the base map and meta.json is a scenario layer
 * that repeats some of it, and GD5 reads the overlay last -- so editing only
 * the base leaves the overlay to win and the fort never appears. A real GD5
 * map is either consistent across the two or has no meta.json at all. */
static void placeFortOnDisk(const std::string& gd5Dir, int provinceId, int level,
                            bool withFactory) {
    Json extra = Json::array();
    if (withFactory) extra.push_back("Basic Factory");
    extra.push_back("Fort Lvl " + std::to_string(level));

    std::vector<uint8_t> bytes;
    REQUIRE(readFile(gd5Dir + "/map_data.json", bytes));
    Json data = Json::parse(bytes.begin(), bytes.end());

    std::string key;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (!it.value().is_object()) continue;
        if (it.value().value("id", 0) != provinceId) continue;
        key = it.key();
        it.value()["buildings"] = extra;
        break;
    }
    REQUIRE(!key.empty());
    REQUIRE(writeFile(gd5Dir + "/map_data.json", data.dump()));

    std::vector<uint8_t> metaBytes;
    if (!readFile(gd5Dir + "/meta.json", metaBytes)) return;
    Json meta = Json::parse(metaBytes.begin(), metaBytes.end());
    if (meta.contains("provinces") && meta["provinces"].contains(key)) {
        meta["provinces"][key]["buildings"] = extra;
        REQUIRE(writeFile(gd5Dir + "/meta.json", meta.dump()));
    }
}

/* Every Open Doctrines level, out to GD5 and home again, is the level it set
 * out as. This is the assertion the scaling exists to make true: with a
 * clamp, or with rounding the other way, the middle of the range collapses. */
static void testEveryOdLevelSurvivesTheRoundTrip() {
    for (int level = 0; level <= 5; ++level) {
        World w = fixture::makeWorld();
        for (auto& p : w.provinces) {
            if (!p.is_sea) p.fortification = level;
        }

        Options opt;
        Report report;
        const std::string odmap = fixture::scratch("forts-" + std::to_string(level) + ".odmap");
        const std::string gd5 = fixture::scratch("forts-" + std::to_string(level) + "-gd5");
        fs::remove_all(gd5);
        REQUIRE(writeOdMap(odmap, w, opt, report));

        World crossing;
        Report r2;
        REQUIRE(readOdMap(odmap, opt, crossing, r2));
        REQUIRE(writeGd5Map(gd5, crossing, opt, r2));

        /* What GD5 itself would see. Level 0 must write no fort at all rather
         * than a "Fort Lvl 0", which GD5's own regex would match and then
         * treat as a level-0 fort. */
        const int inMap = fortInWrittenMap(gd5, 2);
        CHECK_EQ(inMap, level == 0 ? 0 : level * 4);

        World home;
        Report r3;
        REQUIRE(readGd5Map(gd5, opt, home, r3));
        const Province* back = home.findProvince(2);
        REQUIRE(back != nullptr);
        CHECK_EQ(back->fortification, level);
    }
}

/* GD5's twenty levels land on Open Doctrines' five by rounding up, so no fort
 * ever arrives as no fort. Rounding down would drop levels 1-3 entirely. */
static void testGd5LevelsScaleDownRoundingUp() {
    const std::pair<int, int> expected[] = {
        {1, 1},  {2, 1},  {3, 1},  {4, 1},   /* the band that would vanish */
        {5, 2},  {8, 2},  {9, 3},  {12, 3},
        {13, 4}, {16, 4}, {17, 5}, {20, 5},
    };

    for (const auto& pair : expected) {
        World w = fixture::makeWorld();
        for (auto& p : w.provinces) p.fortification = 0;

        Options opt;
        Report report;
        const std::string gd5 = fixture::scratch("gd5fort-" + std::to_string(pair.first));
        fs::remove_all(gd5);
        REQUIRE(writeGd5Map(gd5, w, opt, report));

        /* Put a fort of GD5's own making on the map, the way GD5 would. */
        placeFortOnDisk(gd5, 2, pair.first, true);

        World home;
        Report r2;
        REQUIRE(readGd5Map(gd5, opt, home, r2));
        const Province* p = home.findProvince(2);
        REQUIRE(p != nullptr);
        CHECK_EQ(p->fortification, pair.second);
        /* The factory beside it is still counted, so the fort is not being
         * read by mistaking every building for one. */
        CHECK_EQ(p->industry, 1);
    }
}

/* A province's other buildings are not disturbed, and a fort already on the
 * map is replaced rather than joined by a second one -- GD5 keeps one per
 * province and reads the highest, so a stale entry would win. */
static void testWritingAFortLeavesOtherBuildingsAlone() {
    World w = fixture::makeWorld();
    Province* p = nullptr;
    for (auto& q : w.provinces) {
        if (!q.is_sea) { p = &q; break; }
    }
    REQUIRE(p != nullptr);

    Json carried = Json::array();
    carried.push_back("Basic Factory");
    carried.push_back("Fort Lvl 8");        /* what the map arrived with */
    carried.push_back("Recruitment Building Lvl 2");
    p->extra["gd5_buildings"] = carried;
    p->fortification = 3;                    /* what the player set afterwards */

    Options opt;
    Report report;
    const std::string gd5 = fixture::scratch("forts-preserve-gd5");
    fs::remove_all(gd5);
    REQUIRE(writeGd5Map(gd5, w, opt, report));

    World read;
    Report r2;
    REQUIRE(readGd5Map(gd5, opt, read, r2));
    const Province* got = read.findProvince(p->id);
    REQUIRE(got != nullptr);

    int forts = 0;
    bool factory = false, recruitment = false;
    const auto bld = got->extra.find("gd5_buildings");
    REQUIRE(bld != got->extra.end());
    for (const auto& b : *bld) {
        if (!b.is_string()) continue;
        const std::string name = b.get<std::string>();
        if (name.rfind("Fort Lvl ", 0) == 0) ++forts;
        if (name == "Basic Factory") factory = true;
        if (name == "Recruitment Building Lvl 2") recruitment = true;
    }
    CHECK_EQ(forts, 1);
    CHECK(factory);
    CHECK(recruitment);
    /* 3 out, 12 on the map, 3 home. The carried level 8 is gone. */
    CHECK_EQ(fortInWrittenMap(gd5, p->id), 12);
    CHECK_EQ(got->fortification, 3);
}

/* The precedence rule. A map that goes out, gains a fort in GD5 and comes home
 * keeps the fort: the sidecar remembers the level it left with, and the map is
 * what the player actually built. Before GD5 had forts the record was the only
 * possible source and won unconditionally. */
static void testAFortBuiltInGd5SurvivesComingHome() {
    World w = fixture::makeWorld();
    for (auto& p : w.provinces) p.fortification = 0;

    Options opt;
    Report report;
    const std::string odmap = fixture::scratch("forts-precedence.odmap");
    const std::string gd5 = fixture::scratch("forts-precedence-gd5");
    const std::string back = fixture::scratch("forts-precedence-back.odmap");
    fs::remove_all(gd5);
    REQUIRE(writeOdMap(odmap, w, opt, report));

    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* conv = nullptr;
    REQUIRE(dg_convert(odmap.c_str(), gd5.c_str(), DG_FORMAT_GD5, &opts, &conv) == 0);
    if (conv) dg_report_free(conv);

    /* GD5 builds one. */
    placeFortOnDisk(gd5, 2, 16, false);

    dg_report* home = nullptr;
    REQUIRE(dg_convert(gd5.c_str(), back.c_str(), DG_FORMAT_ODMAP, &opts, &home) == 0);
    if (home) dg_report_free(home);

    World returned;
    Report r2;
    REQUIRE(readOdMap(back, opt, returned, r2));
    const Province* p = returned.findProvince(2);
    REQUIRE(p != nullptr);
    /* 16 of GD5's 20 -> 4 of Open Doctrines' 5. Not the 0 it set out with. */
    CHECK_EQ(p->fortification, 4);
}


/* A GD5 fort comes home the level it set out as -- the direction the tests
 * above do NOT cover.
 *
 * Every other test here runs Open Doctrines -> GD5 -> Open Doctrines, which is
 * exact by construction: the level is multiplied by four and divided by four.
 * The other way round is not. GD5 has twenty levels and Open Doctrines five, so
 * Fort Lvl 5 arrives as level 2 and, scaled back, returns as Lvl 8 -- a fort
 * that grows a little every time the map crosses.
 *
 * Five of Greater Diplomacy's own shipped maps failed conformance on this, and
 * no unit test saw it, because every one of them tested the easy direction.
 */
static void testAGd5FortLevelSurvivesTheReturnTrip() {
    const int levels[] = {1, 2, 3, 5, 7, 10, 13, 17, 19, 20};

    for (int level : levels) {
        World w = fixture::makeWorld();
        for (auto& p : w.provinces) p.fortification = 0;

        Options opt;
        Report report;
        const std::string gd5 = fixture::scratch("gd5fort-return-" + std::to_string(level));
        fs::remove_all(gd5);
        REQUIRE(writeGd5Map(gd5, w, opt, report));
        placeFortOnDisk(gd5, 2, level, true);

        /* Out to Open Doctrines... */
        World crossing;
        Report r2;
        REQUIRE(readGd5Map(gd5, opt, crossing, r2));
        const std::string odmap = fixture::scratch("gd5fort-return-" + std::to_string(level)
                                                   + ".odmap");
        REQUIRE(writeOdMap(odmap, crossing, opt, r2));

        /* ...and home again. */
        World home;
        Report r3;
        REQUIRE(readOdMap(odmap, opt, home, r3));
        const std::string back = fixture::scratch("gd5fort-return-" + std::to_string(level)
                                                  + "-back");
        fs::remove_all(back);
        REQUIRE(writeGd5Map(back, home, opt, r3));

        CHECK_EQ(fortInWrittenMap(back, 2), level);
    }
}

int main() {
    testEveryOdLevelSurvivesTheRoundTrip();
    testGd5LevelsScaleDownRoundingUp();
    testWritingAFortLeavesOtherBuildingsAlone();
    testAGd5FortLevelSurvivesTheReturnTrip();
    testAFortBuiltInGd5SurvivesComingHome();
    return check::finish("test_forts");
}
