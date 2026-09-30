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

    long wrong = 0;
    std::set<std::pair<int, int>> seen;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            const size_t i = static_cast<size_t>(row) * columns + col;
            if (i >= tiles.size()) break;
            int wantX = 0, wantY = 0;
            uncivHex(col, row, wantX, wantY);
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

int main() {
    testTheGridIsUncivsGrid();
    testOnlyUncivsOwnTerrainsAreWritten();
    testInventedClimateIsDeclared();
    testTheOriginalComesBackThroughTheFile();
    testAnEditInUncivIsKept();
    testAStrippedMapIsRefusedAndSaysWhy();
    testAWrittenMapIsDetectedAsUnciv();
    return check::finish("test_unciv");
}
