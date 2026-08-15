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
#include "Raster.h"

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

/* GD5 does not draw water into its political layer -- it paints the chroma key
 * its renderer then sets as the surface's colorkey, so the terrain underneath
 * shows through. Painting an ordinary blue there instead hands the game an
 * opaque ocean pasted over its own map, which nothing in the model would ever
 * notice. */
static void testPoliticalLayerUsesTheChromaKey() {
    const std::string odmap = writeFixtureOdmap();
    REQUIRE(!odmap.empty());
    const std::string gd5 = fixture::scratch("chroma-gd5");
    fs::remove_all(gd5);

    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* report = nullptr;
    REQUIRE(dg_convert(odmap.c_str(), gd5.c_str(), DG_FORMAT_GD5, &opts, &report) == 0);
    dg_report_free(report);

    std::vector<uint8_t> bytes;
    REQUIRE(readFile(gd5 + "/political.png", bytes));
    Image political;
    REQUIRE(decodePng(bytes, political));

    Options opt;
    Report quiet;
    World written;
    REQUIRE(readGd5Map(gd5, opt, written, quiet));

    /* Province 1 of the fixture is open sea across the top of the map. */
    const size_t sea = 4 * (static_cast<size_t>(2) * written.width + 10);
    CHECK_EQ(int(political.rgba[sea + 0]), 255);
    CHECK_EQ(int(political.rgba[sea + 1]), 0);
    CHECK_EQ(int(political.rgba[sea + 2]), 255);

    /* Land keeps its owner's colour, and must never be the key itself. */
    const size_t land = 4 * (static_cast<size_t>(20) * written.width + 10);
    const bool isKey = political.rgba[land] == 255 && political.rgba[land + 1] == 0
                       && political.rgba[land + 2] == 255;
    CHECK(!isKey);
}

/* GD5 does not store a flag as an image file. `flag_data` is base64 of raw
 * pixel bytes at exactly 60x40, handed to pygame.image.fromstring; base64 of a
 * PNG makes fromstring raise, the game swallows it, and the nation shows a
 * blank white rectangle -- which every converted map did until this was
 * worked out. */
static void testFlagsAreRawPixelsNotPng() {
    const std::string odmap = fixture::scratch("flags.odmap");
    World w = fixture::makeWorld();

    /* A recognisable 8x4 flag, so the scaling to 60x40 has something to carry. */
    Image flag;
    flag.width = 8;
    flag.height = 4;
    flag.channels = 4;
    flag.rgba.assign(8 * 4 * 4, 0);
    for (size_t p = 0; p < 32; ++p) {
        flag.rgba[p * 4 + 0] = (p % 8) < 4 ? 220 : 20;
        flag.rgba[p * 4 + 1] = 30;
        flag.rgba[p * 4 + 2] = 40;
        flag.rgba[p * 4 + 3] = 255;
    }
    const std::vector<uint8_t> flagPng = encodePng(flag);
    w.findNation("GER")->flag_name = "flags/GER_EMPIRE.png";
    w.findNation("GER")->flag_bytes = flagPng;

    Options opt;
    Report report;
    REQUIRE(writeOdMap(odmap, w, opt, report));

    const std::string gd5 = fixture::scratch("flags-gd5");
    fs::remove_all(gd5);
    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* conv = nullptr;
    REQUIRE(dg_convert(odmap.c_str(), gd5.c_str(), DG_FORMAT_GD5, &opts, &conv) == 0);
    dg_report_free(conv);

    std::vector<uint8_t> metaBytes;
    REQUIRE(readFile(gd5 + "/meta.json", metaBytes));
    const Json meta = Json::parse(metaBytes.begin(), metaBytes.end());
    const Json& nations = meta["nation_data"];
    REQUIRE(nations.contains("German Empire"));

    const std::string encoded = nations["German Empire"].value("flag_data", std::string());
    REQUIRE(encoded != "DEFAULT" && !encoded.empty());
    const std::vector<uint8_t> raw = base64Decode(encoded);
    /* 60 * 40 * 4. Not a PNG: a PNG of this would start with the signature and
     * be nothing like this length. */
    CHECK_EQ(raw.size(), size_t(60 * 40 * 4));
    CHECK(!(raw.size() > 8 && raw[0] == 0x89 && raw[1] == 'P'));

    /* And the original full-size image, not the 60x40 GD5 copy, is what comes
     * home -- otherwise every crossing shrinks a nation's flag for good. */
    World back;
    const std::string home = fixture::scratch("flags-back.odmap");
    dg_report* conv2 = nullptr;
    REQUIRE(dg_convert(gd5.c_str(), home.c_str(), DG_FORMAT_ODMAP, &opts, &conv2) == 0);
    dg_report_free(conv2);
    REQUIRE(readOdMap(home, opt, back, report));

    const Nation* ger = back.findNation("GER");
    REQUIRE(ger != nullptr);
    CHECK_EQ(ger->flag_name, std::string("flags/GER_EMPIRE.png"));
    CHECK(ger->flag_bytes == flagPng);
}

/* Open Doctrines leaves its oceans unpainted, and GD5 can neither draw nor
 * sail across what is not a province. The water is cut into provinces on the
 * way out and the invented ones are deleted again on the way back. */
static void testOceanIsInventedForGd5AndRemovedComingBack() {
    World w = fixture::makeWorld();
    /* Make the fixture's sea unprovinced, the way an Open Doctrines map has
     * it: the pixels stay, the province does not. */
    for (auto& id : w.raster) {
        if (id == 1) id = 0;
    }
    w.provinces.erase(w.provinces.begin());

    const std::string odmap = fixture::scratch("ocean.odmap");
    Options opt;
    Report report;
    REQUIRE(writeOdMap(odmap, w, opt, report));

    const std::string gd5 = fixture::scratch("ocean-gd5");
    fs::remove_all(gd5);
    dg_options opts;
    dg_options_defaults(&opts);
    dg_report* conv = nullptr;
    REQUIRE(dg_convert(odmap.c_str(), gd5.c_str(), DG_FORMAT_GD5, &opts, &conv) == 0);
    dg_report_free(conv);

    /* Read as GD5 itself reads it -- straight out of map_data.json. Loading it
     * back through this library would show nothing, because the reader deletes
     * the invented provinces again, which is the other half of the feature. */
    std::vector<uint8_t> mapBytes;
    REQUIRE(readFile(gd5 + "/map_data.json", mapBytes));
    const Json mapData = Json::parse(mapBytes.begin(), mapBytes.end());

    long sea = 0;
    for (auto it = mapData.begin(); it != mapData.end(); ++it) {
        if (isSeaTerrain(it.value().value("terrain", std::string()))) ++sea;
    }
    CHECK(sea > 0);
    CHECK(mapData.size() > w.provinces.size());

    /* Owned by Ocean, which every GD5 map has on its roster and no Open
     * Doctrines map does, so it has to be added alongside them. */
    std::vector<uint8_t> metaBytes;
    REQUIRE(readFile(gd5 + "/meta.json", metaBytes));
    const Json gd5Meta = Json::parse(metaBytes.begin(), metaBytes.end());
    CHECK(gd5Meta["nation_data"].contains("Ocean"));

    /* ...and they are gone again by the time the map is back in Open
     * Doctrines, which does not put provinces in the water at all. */
    const std::string home = fixture::scratch("ocean-back.odmap");
    dg_report* conv2 = nullptr;
    REQUIRE(dg_convert(gd5.c_str(), home.c_str(), DG_FORMAT_ODMAP, &opts, &conv2) == 0);
    dg_report_free(conv2);

    World back;
    REQUIRE(readOdMap(home, opt, back, report));
    CHECK_EQ(back.provinces.size(), w.provinces.size());
    for (const auto& p : back.provinces) CHECK(!p.is_sea);

    /* Switching it off leaves the water as the source had it. */
    const std::string bare = fixture::scratch("ocean-off-gd5");
    fs::remove_all(bare);
    opts.synthesise_ocean = 0;
    dg_report* conv3 = nullptr;
    REQUIRE(dg_convert(odmap.c_str(), bare.c_str(), DG_FORMAT_GD5, &opts, &conv3) == 0);
    dg_report_free(conv3);

    World plain;
    REQUIRE(readGd5Map(bare, opt, plain, report));
    CHECK_EQ(plain.provinces.size(), w.provinces.size());
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
    testPoliticalLayerUsesTheChromaKey();
    testFlagsAreRawPixelsNotPng();
    testOceanIsInventedForGd5AndRemovedComingBack();
    testWithoutSidecarLosesData();
    testRealMapsIfAvailable();
    return check::finish("test_roundtrip");
}
