/* Unciv: the resampling, and the grid it lands on.
 *
 * The grid is the part worth a test. A hex coordinate function that is merely
 * PLAUSIBLE produces a file that parses, has the right number of tiles, has no
 * duplicate positions, and describes a world whose continents Unciv scatters --
 * and nothing about the output looks wrong. The first version of this binding
 * shipped exactly that, written from memory of how axial coordinates usually
 * work rather than from HexMath.kt.
 */
#include <dragoman/dragoman.h>

#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "Check.h"
#include "Fixture.h"
#include "ModelJson.h"
#include "Support.h"

using namespace dragoman;
namespace fs = std::filesystem;

/* Unciv's HexMath.getTileCoordsFromColumnRow, written out a SECOND time from
 * the Kotlin, so a mistake in the library's copy does not agree with itself
 * here. */
static void uncivHex(int column, int row, int& x, int& y) {
    int twoRows = row * 2;
    if (std::abs(column) % 2 == 1) twoRows += 1;
    x = (twoRows - column) / 2;
    y = (twoRows + column) / 2;
}

static Json readJsonFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) return Json();
    return Json::parse(bytes.begin(), bytes.end(), nullptr, false);
}

/* Every tile sits where Unciv's own arithmetic puts it. */
static void testTheGridIsUncivsGrid() {
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-grid.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    const Json map = readJsonFile(out);
    REQUIRE(!map.is_null() && !map.is_discarded());
    REQUIRE(map.contains("tileList"));
    const Json& tiles = map["tileList"];
    REQUIRE(tiles.is_array());

    /* The writer lays them out row by row, so the nth tile is (col, row). */
    const int columns = map["mapParameters"]["mapSize"].value("width", 0);
    const int rows = map["mapParameters"]["mapSize"].value("height", 0);
    CHECK(columns > 0);
    CHECK(rows > 0);
    CHECK_EQ(static_cast<int>(tiles.size()), columns * rows);

    /* Centred on the origin, exactly as TileMap(width, height, ruleset, wrap)
     * lays a rectangular map out: columns -w/2..(w-1)/2, rows likewise. */
    const int colMin = -(columns / 2), rowMin = -(rows / 2);

    long wrong = 0;
    std::set<std::pair<int, int>> seen;
    for (int ri = 0; ri < rows; ++ri) {
        for (int ci = 0; ci < columns; ++ci) {
            const size_t i = static_cast<size_t>(ri) * columns + ci;
            if (i >= tiles.size()) break;
            int wantX = 0, wantY = 0;
            uncivHex(ci + colMin, ri + rowMin, wantX, wantY);
            const int gotX = tiles[i]["position"].value("x", 0);
            const int gotY = tiles[i]["position"].value("y", 0);
            if (gotX != wantX || gotY != wantY) ++wrong;
            seen.insert({gotX, gotY});
        }
    }
    CHECK_EQ(wrong, 0L);
    /* Injective as well as correct: a function can agree with nothing and
     * still never collide, which is how the wrong one looked fine. */
    CHECK_EQ(seen.size(), tiles.size());
}

/* THE CHECK THAT ONLY THE SECOND CALL MAKES.
 *
 * TileMap.setTransients builds its tileMatrix on the first call and, on every
 * call after that, asserts the matrix it already has is the right size:
 *
 *     check(tileMatrix.size in (-2 * leftX)..(3 - 2 * leftX))
 *         { "TileMap.setTransients called on existing tileMatrix of different size" }
 *
 * which is only satisfiable when the columns are centred on the origin, so that
 * rightX is -leftX give or take one. A map editor loads a map with ONE call and
 * is perfectly happy with a grid numbered 0..width-1; GameStarter calls it a
 * second time through GameInfo.setTransients, and there the same map throws.
 *
 * Every map this library wrote before 2026-10-01 was numbered from zero. It
 * loaded in the editor, passed tools/validate_unciv.py, and passed
 * ValidateWithUnciv -- which called setTransients once -- and could not start a
 * game. The invariant is asserted here because it is cheap and runs everywhere;
 * the Java validator now starts an actual game, which is the real question. */
static void testTheGridIsCentredOnTheOrigin() {
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-centred.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    const Json map = readJsonFile(out);
    REQUIRE(map.contains("tileList"));
    const Json& tiles = map["tileList"];
    REQUIRE(tiles.is_array() && !tiles.empty());

    int leftX = INT_MAX, rightX = INT_MIN;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const int x = tiles[i]["position"].value("x", 0);
        if (x < leftX) leftX = x;
        if (x > rightX) rightX = x;
    }
    const int width = rightX - leftX + 1;
    CHECK(width >= -2 * leftX);
    CHECK(width <= 3 - 2 * leftX);

    /* World wrap needs an even width and the writer always wraps. Note that
     * mapSize.width is the COLUMN COUNT and is not the x extent above -- axial
     * x runs wider than the grid is columns, and Unciv's own TileMap(w, h, ...)
     * has the same property. tools/unciv/CompareGrid.java checks this writer's
     * positions against that constructor's, tile for tile. */
    CHECK_EQ(map["mapParameters"]["mapSize"].value("width", 0) % 2, 0);
}

/* Only terrains Unciv has, and Hill is not one of them -- it is a FEATURE, and
 * writing it as a base terrain gives a map whose ruleset validation fails. */
static void testOnlyUncivsOwnTerrainsAreWritten() {
    World w = fixture::makeWorld();
    /* Every token the model can hold, so the mapping is exercised whole. */
    const char* tokens[] = {"ocean", "coastal_sea", "inland_sea", "lakes", "mountain",
                            "hills", "desert", "plains", "forest", "jungle",
                            "swamp", "tundra", "frozen"};
    size_t i = 0;
    for (auto& p : w.provinces) {
        p.terrain = tokens[i % (sizeof(tokens) / sizeof(tokens[0]))];
        ++i;
    }

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-terrain.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    const Json map = readJsonFile(out);
    REQUIRE(!map.is_null() && !map.is_discarded());

    const std::set<std::string> bases = {"Ocean", "Coast", "Lakes", "Grassland", "Plains",
                                         "Tundra", "Desert", "Mountain", "Snow"};
    const std::set<std::string> features = {"Hill", "Forest", "Jungle", "Marsh"};

    for (const auto& t : map["tileList"]) {
        const std::string base = t.value("baseTerrain", std::string());
        CHECK(bases.count(base) == 1);
        if (base == "Hill") {
            CHECK(false);  /* a feature, never a base */
        }
        if (t.contains("terrainFeatures")) {
            for (const auto& f : t["terrainFeatures"]) {
                CHECK(features.count(f.get<std::string>()) == 1);
            }
        }
    }
}

/* A map with no terrain at all gets one, and the conversion SAYS it invented
 * it rather than letting it pass for a translation. */
static void testInventedClimateIsDeclared() {
    World w = fixture::makeWorld();
    for (auto& p : w.provinces) p.terrain.clear();

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-invented.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    bool said = false;
    for (const auto& e : report.entries()) {
        if (e.message.find("invented") != std::string::npos
            || e.message.find("derived from latitude") != std::string::npos) {
            said = true;
        }
    }
    CHECK(said);
}

/* A map that goes out and comes back is the map that set out.
 *
 * Not because the hexes are read back into provinces -- they cannot be -- but
 * because the original rides inside the file, under a key Unciv ignores. The
 * raster in particular must return exactly: province SHAPES are the thing a
 * hex grid destroys and the thing no amount of metadata rebuilds. */
static void testTheOriginalComesBackThroughTheFile() {
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-carry.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    World back;
    Report r2;
    REQUIRE(readUncivMap(out, opt, back, r2));

    CHECK_EQ(back.provinces.size(), w.provinces.size());
    CHECK_EQ(back.nations.size(), w.nations.size());
    CHECK_EQ(back.width, w.width);
    CHECK_EQ(back.height, w.height);
    CHECK(back.raster == w.raster);

    /* Flags are bytes, and the model snapshot records only their SIZE -- it
     * describes a world rather than serialising one. A record built from it
     * alone comes home with every flag missing, which is how this was found:
     * by comparing a returned archive against the one that set out. */
    size_t withFlags = 0, returned = 0;
    for (const auto& n : w.nations) {
        if (!n.flag_bytes.empty()) ++withFlags;
    }
    for (const auto& n : back.nations) {
        if (!n.flag_bytes.empty()) ++returned;
    }
    CHECK_EQ(returned, withFlags);
}

/* A terrain changed in Unciv is a terrain that comes home changed. The carried
 * original is the starting point, not the last word. */
static void testAnEditInUncivIsKept() {
    World w = fixture::makeWorld();
    for (auto& p : w.provinces) {
        if (!p.is_sea) p.terrain = "plains";
    }

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-edit.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    /* The player turns some plains into desert. */
    std::vector<uint8_t> bytes;
    REQUIRE(readFile(out, bytes));
    Json map = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    REQUIRE(!map.is_discarded());
    long changed = 0;
    for (auto& t : map["tileList"]) {
        if (t.value("baseTerrain", std::string()) == "Plains") {
            t["baseTerrain"] = "Desert";
            ++changed;
        }
    }
    REQUIRE(changed > 0);
    REQUIRE(writeFile(out, map.dump()));

    World back;
    Report r2;
    REQUIRE(readUncivMap(out, opt, back, r2));

    bool anyDesert = false;
    for (const auto& p : back.provinces) {
        if (p.terrain == "desert") anyDesert = true;
    }
    CHECK(anyDesert);
}

/* A map Unciv itself re-saved has no carried record: its serialiser writes from
 * the TileMap object and drops anything not on it. That is refused, and named,
 * rather than answered with province boundaries invented from hexes. */
static void testAStrippedMapIsRefusedAndSaysWhy() {
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-stripped.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    std::vector<uint8_t> bytes;
    REQUIRE(readFile(out, bytes));
    Json map = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    REQUIRE(!map.is_discarded());
    map.erase("dragoman");                    /* what Unciv's own save does */
    REQUIRE(writeFile(out, map.dump()));

    World back;
    Report r2;
    CHECK(!readUncivMap(out, opt, back, r2));
    bool named = false;
    for (const auto& e : r2.entries()) {
        if (e.message.find("re-saved by Unciv") != std::string::npos) named = true;
    }
    CHECK(named);
}

/* A file this library wrote is one it recognises. */
static void testAWrittenMapIsDetectedAsUnciv() {
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-detect.json");
    REQUIRE(writeUncivMap(out, w, opt, report));
    CHECK_EQ(detectFormat(out), 3);
}

/* A fort crosses as Unciv's Fort improvement -- but only where Unciv will
 * build one. TileImprovements.json lists terrainsCanBeBuiltOn for Fort and
 * Mountain and Marsh are not on it, so a fortified province that lands on
 * either has to cross without its fort. Writing one there produces a map the
 * ruleset refuses, which is worse than a missing fort.
 *
 * The negative half is the point. A version of this that only checked "a fort
 * appears somewhere" passes just as well against a writer that puts Fort on
 * every fortified hex regardless of terrain. */
static void testAFortCrossesOnlyWhereUncivWillBuildOne() {
    World w = fixture::makeWorld();

    const auto terrainAndFort = [&w](int64_t id, const char* terrain, int fort) {
        Province* p = w.findProvince(id);
        REQUIRE(p != nullptr);
        p->terrain = terrain;
        p->fortification = fort;
    };
    terrainAndFort(2, "plains", 2);     /* allowed      */
    terrainAndFort(3, "mountain", 3);   /* refused      */
    terrainAndFort(4, "swamp", 3);      /* refused      */
    terrainAndFort(5, "plains", 0);     /* not fortified */
    terrainAndFort(6, "plains", 0);

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-forts.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    const Json map = readJsonFile(out);
    REQUIRE(map.contains("tileList"));

    int forts = 0, fortsOnMountain = 0, fortsOnMarsh = 0, improvementsThatAreNotForts = 0;
    for (const auto& t : map["tileList"]) {
        const std::string imp = t.value("improvement", std::string());
        if (imp.empty()) continue;
        if (imp != "Fort") { ++improvementsThatAreNotForts; continue; }
        ++forts;
        if (t.value("baseTerrain", std::string()) == "Mountain") ++fortsOnMountain;
        if (t.contains("terrainFeatures")) {
            for (const auto& f : t["terrainFeatures"]) {
                if (f.is_string() && f.get<std::string>() == "Marsh") ++fortsOnMarsh;
            }
        }
    }

    CHECK(forts > 0);                        /* the plain block carried its fort */
    CHECK_EQ(fortsOnMountain, 0);
    CHECK_EQ(fortsOnMarsh, 0);
    CHECK_EQ(improvementsThatAreNotForts, 0);

    /* And nothing invents one: with no fortification anywhere, no improvement
     * is written at all. */
    for (int64_t id = 1; id <= 6; ++id) {
        if (Province* p = w.findProvince(id)) p->fortification = 0;
    }
    const std::string bare = fixture::scratch("unciv-no-forts.json");
    REQUIRE(writeUncivMap(bare, w, opt, report));
    const Json bareMap = readJsonFile(bare);
    REQUIRE(bareMap.contains("tileList"));
    int anyImprovement = 0;
    for (const auto& t : bareMap["tileList"]) {
        if (!t.value("improvement", std::string()).empty()) ++anyImprovement;
    }
    CHECK_EQ(anyImprovement, 0);
}

/* Deposits cross as Unciv resources, but only the four with a vanilla
 * counterpart and only onto terrain TileResources.json allows them on.
 *
 * The negative halves carry the weight: rubber must never appear (Civ V
 * vanilla has no rubber, and a stand-in would put rubber where the map had
 * none), and a deposit on the wrong terrain must be dropped rather than
 * written -- Unciv loads such a map perfectly happily, so nothing downstream
 * would catch it. */
static void testADepositCrossesOnlyWhereItsResourceMaySit() {
    World w = fixture::makeWorld();

    const auto setup = [&w](int64_t id, const char* terrain,
                            const char* deposit, double surface) {
        Province* p = w.findProvince(id);
        REQUIRE(p != nullptr);
        p->terrain = terrain;
        p->resources.clear();
        if (deposit != nullptr) {
            Resource r;
            r.a = surface;
            r.b = 1.0;
            p->resources[deposit] = r;
        }
    };
    setup(2, "plains", "metal", 36.0);      /* Iron sits on Plains     */
    setup(3, "plains", "gemstones", 50.0);  /* Gems do NOT sit on Plains */
    setup(4, "jungle", "gemstones", 50.0);  /* Gems do sit on Jungle   */
    setup(5, "plains", "rubber", 90.0);     /* no counterpart at all   */
    setup(6, "plains", nullptr, 0.0);

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-resources.json");
    REQUIRE(writeUncivMap(out, w, opt, report));
    const Json map = readJsonFile(out);
    REQUIRE(map.contains("tileList"));

    int iron = 0, gems = 0, gemsOnPlains = 0, rubberish = 0, ironAmountSeen = 0;
    for (const auto& t : map["tileList"]) {
        const std::string r = t.value("resource", std::string());
        if (r.empty()) continue;
        const std::string base = t.value("baseTerrain", std::string());
        bool jungle = false;
        if (t.contains("terrainFeatures")) {
            for (const auto& f : t["terrainFeatures"]) {
                if (f.is_string() && f.get<std::string>() == "Jungle") jungle = true;
            }
        }
        if (r == "Iron") {
            ++iron;
            ironAmountSeen = t.value("resourceAmount", 0);
        } else if (r == "Gems") {
            ++gems;
            if (!jungle) ++gemsOnPlains;
        } else {
            ++rubberish;  /* anything else at all, including a rubber stand-in */
        }
    }

    CHECK(iron > 0);
    CHECK(gems > 0);
    CHECK_EQ(gemsOnPlains, 0);
    CHECK_EQ(rubberish, 0);
    /* sqrt(36) = 6, and a strategic deposit carries its amount. */
    CHECK_EQ(ironAmountSeen, 6);
}

/* Starting locations, for the nations Unciv has a civilization for.
 *
 * The negative half matters as much as the positive: a nation Civ V does not
 * ship must NOT appear, because StartingLocation.nation has to name a civ in
 * the ruleset and an invented one would be read back as nothing. */
static void testStartingLocationsNameOnlyCivsTheRulesetHas() {
    World w = fixture::makeWorld();
    /* 2 and 3 are ordinary land; give one a civ Unciv has and one it has not. */
    REQUIRE(w.findProvince(2) != nullptr);
    REQUIRE(w.findProvince(3) != nullptr);
    /* Every owner set here, so the count below is this test's and not the
     * fixture's -- makeWorld gives its provinces owners of their own. */
    for (int64_t id = 1; id <= 6; ++id) {
        if (Province* p = w.findProvince(id)) p->owner.clear();
    }
    w.findProvince(2)->owner = "FRA";   /* France, which Civ V ships   */
    w.findProvince(3)->owner = "LUX";   /* Luxembourg, which it does not */

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-starts.json");
    REQUIRE(writeUncivMap(out, w, opt, report));
    const Json map = readJsonFile(out);

    REQUIRE(map.contains("startingLocations"));
    bool sawFrance = false;
    int total = 0;
    for (const auto& sl : map["startingLocations"]) {
        ++total;
        const std::string nation = sl.value("nation", std::string());
        /* Never a raw ISO code, and never Luxembourg under any spelling. */
        CHECK(nation != "FRA");
        CHECK(nation != "LUX");
        CHECK(nation != "Luxembourg");
        if (nation == "France") sawFrance = true;
        CHECK(sl.contains("position"));
    }
    CHECK(sawFrance);
    CHECK_EQ(total, 1);
}

/* A fort or a deposit added in Unciv's editor comes home; one cleared there
 * does not, because a province spans several hexes and clearing one of them
 * does not settle what the province is.
 *
 * Both halves are checked. Without the second, a reader that simply adopted
 * whatever the last hex said would pass. */
static void testAnAddedFortOrDepositComesHomeButARemovalDoesNot() {
    World w = fixture::makeWorld();
    for (auto& p : w.provinces) {
        if (p.is_sea) continue;
        p.terrain = "plains";
        p.fortification = 0;
        p.resources.clear();
    }
    /* One province starts fortified, so there is something to try to remove. */
    REQUIRE(w.findProvince(2) != nullptr);
    w.findProvince(2)->fortification = 3;

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-edit-fort.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    std::vector<uint8_t> bytes;
    REQUIRE(readFile(out, bytes));
    Json map = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    REQUIRE(!map.is_discarded());

    /* The player clears every fort the writer placed, and puts Iron with an
     * amount of 3 on a hex that had no deposit at all. */
    long cleared = 0, placed = 0;
    for (auto& t : map["tileList"]) {
        if (t.value("improvement", std::string()) == "Fort") {
            t.erase("improvement");
            ++cleared;
        } else if (t.value("baseTerrain", std::string()) == "Plains"
                   && !t.contains("resource") && placed == 0) {
            t["resource"] = "Iron";
            t["resourceAmount"] = 3;
            ++placed;
        }
    }
    REQUIRE(cleared > 0);
    REQUIRE(placed == 1);
    REQUIRE(writeFile(out, map.dump()));

    World back;
    Report r2;
    REQUIRE(readUncivMap(out, opt, back, r2));

    /* The addition arrived: some province gained metal, 3*3 = 9 of it. */
    bool gainedMetal = false;
    for (const auto& p : back.provinces) {
        const auto it = p.resources.find("metal");
        if (it != p.resources.end()) {
            gainedMetal = true;
            CHECK_EQ(static_cast<int>(it->second.a + 0.5), 9);
        }
    }
    CHECK(gainedMetal);

    /* The removal did not: province 2 is still fortified. */
    const Province* two = back.findProvince(2);
    REQUIRE(two != nullptr);
    CHECK_EQ(two->fortification, 3);
}

/* The same, on a map whose provinces have NO terrain -- which is every Open
 * Doctrines map, since that format stores none and the hexes get a climate
 * invented from latitude.
 *
 * This is its own test because the one above cannot see the bug it guards:
 * the fixture sets terrain, so the invented-climate path never runs there. On
 * a real Open Doctrines world map it ran for all 1,642 provinces, and an early
 * version of the read-back skipped every one of them. */
static void testAnAdditionComesHomeFromATerrainlessMapToo() {
    World w = fixture::makeWorld();
    for (auto& p : w.provinces) {
        p.terrain.clear();          /* as an Open Doctrines map arrives */
        p.fortification = 0;
        p.resources.clear();
    }

    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-edit-noterrain.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    std::vector<uint8_t> bytes;
    REQUIRE(readFile(out, bytes));
    Json map = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    REQUIRE(!map.is_discarded());

    /* Put a fort on the first hex that is not water, whatever climate it was
     * given -- the point is that the province behind it has no terrain. */
    long placed = 0;
    for (auto& t : map["tileList"]) {
        const std::string base = t.value("baseTerrain", std::string());
        if (base == "Ocean" || base == "Coast" || base == "Lakes") continue;
        if (base == "Mountain") continue;   /* a Fort may not stand there */
        t["improvement"] = "Fort";
        ++placed;
        if (placed == 2) break;
    }
    REQUIRE(placed > 0);
    REQUIRE(writeFile(out, map.dump()));

    World back;
    Report r2;
    REQUIRE(readUncivMap(out, opt, back, r2));

    long fortified = 0;
    for (const auto& p : back.provinces) {
        if (p.fortification > 0) ++fortified;
    }
    CHECK(fortified > 0);
}

/* The mod carries the map's own nations, and the map declares the mod.
 *
 * The declaration is the half that is easy to get wrong and impossible to
 * notice: Unciv names a mod after ITS FOLDER (RulesetCache does
 * `modRuleset.name = modFolder.name()`), so a map that declares anything else
 * -- the world's title, say -- loads against plain vanilla, and every
 * starting location in it names a civilization that does not exist. */
static void testTheModCarriesTheNationsAndTheMapDeclaresIt() {
    World w = fixture::makeWorld();
    const std::string dir = fixture::scratch("TestMod");
    /* The scratch directory outlives a run. Without this, a map left by an
     * EARLIER, correct run is still sitting at maps/TestMod.json, and the
     * check below reads that instead of what this run just wrote -- which is
     * how a deliberately broken build passed this test once already. */
    std::filesystem::remove_all(dir);

    Options opt;
    Report report;
    REQUIRE(writeUncivMod(dir, w, opt, report));

    const Json nations = readJsonFile(dir + "/jsons/Nations.json");
    REQUIRE(nations.is_array());
    CHECK(!nations.empty());
    for (const auto& n : nations) {
        CHECK(n.contains("name"));
        CHECK(n.contains("outerColor"));
        CHECK(n.contains("innerColor"));
        CHECK(n.contains("cities"));
        CHECK(!n["cities"].empty());           /* never an empty city list */
        CHECK(n["outerColor"].size() == 3);
    }

    /* The map is inside the mod, and declares the mod BY FOLDER NAME. */
    const Json map = readJsonFile(dir + "/maps/TestMod.json");
    REQUIRE(map.contains("mapParameters"));
    REQUIRE(map["mapParameters"].contains("mods"));
    REQUIRE(map["mapParameters"]["mods"].size() == 1);
    CHECK_EQ(map["mapParameters"]["mods"][0].get<std::string>(), std::string("TestMod"));

    /* Every starting location names a nation the mod actually defines. */
    REQUIRE(map.contains("startingLocations"));
    CHECK(!map["startingLocations"].empty());
    for (const auto& sl : map["startingLocations"]) {
        const std::string want = sl.value("nation", std::string());
        bool defined = false;
        for (const auto& n : nations) {
            if (n.value("name", std::string()) == want) { defined = true; break; }
        }
        CHECK(defined);
    }
}

int main() {
    testTheGridIsUncivsGrid();
    testTheGridIsCentredOnTheOrigin();
    testOnlyUncivsOwnTerrainsAreWritten();
    testInventedClimateIsDeclared();
    testTheOriginalComesBackThroughTheFile();
    testAnEditInUncivIsKept();
    testAStrippedMapIsRefusedAndSaysWhy();
    testAWrittenMapIsDetectedAsUnciv();
    testAFortCrossesOnlyWhereUncivWillBuildOne();
    testADepositCrossesOnlyWhereItsResourceMaySit();
    testStartingLocationsNameOnlyCivsTheRulesetHas();
    testAnAddedFortOrDepositComesHomeButARemovalDoesNot();
    testAnAdditionComesHomeFromATerrainlessMapToo();
    testTheModCarriesTheNationsAndTheMapDeclaresIt();
    return check::finish("test_unciv");
}
