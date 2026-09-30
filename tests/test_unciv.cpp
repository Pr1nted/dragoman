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

/* Reading is refused, and refused out loud. Returning a half-built world from
 * a hex grid would be worse than declining: the province boundaries it would
 * invent never existed. */
static void testReadingIsRefusedRatherThanGuessed() {
    World w = fixture::makeWorld();
    Options opt;
    Report report;
    const std::string out = fixture::scratch("unciv-read.json");
    REQUIRE(writeUncivMap(out, w, opt, report));

    World back;
    Report r2;
    CHECK(!readUncivMap(out, opt, back, r2));
    bool named = false;
    for (const auto& e : r2.entries()) {
        if (e.message.find("not implemented") != std::string::npos) named = true;
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
    testReadingIsRefusedRatherThanGuessed();
    testAWrittenMapIsDetectedAsUnciv();
    return check::finish("test_unciv");
}
