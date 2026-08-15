/* The raster layer: the two id encodings, and the geometry only it knows. */
#include "Check.h"
#include "Fixture.h"
#include "Raster.h"

using namespace dragoman;

/* The claim the whole translation rests on: Open Doctrines packs a province id
 * into a pixel big-endian and GD5 packs it little-endian, so the two rasters
 * are the same picture with red and blue exchanged, and no id is ever
 * renumbered. Checked across the full 24-bit range at the boundaries that
 * matter -- 255/256 is where the second byte starts, 65535/65536 the third. */
static void testIdEncodings() {
    const std::vector<uint32_t> ids = {1, 2, 254, 255, 256, 257, 511, 512,
                                       65535, 65536, 65537, 1247, 2523, 0xFFFFFF};
    const int w = static_cast<int>(ids.size());

    const Image od = rasterToOd(ids, w, 1);
    const Image gd = rasterToGd5(ids, w, 1);

    for (size_t i = 0; i < ids.size(); ++i) {
        const uint32_t id = ids[i];
        /* Open Doctrines writes the id as if the colour were hex: id 1 is
         * #000001, which is exactly what its provinces.json records. */
        CHECK_EQ(uint32_t(od.rgba[i * 4 + 0]), (id >> 16) & 0xff);
        CHECK_EQ(uint32_t(od.rgba[i * 4 + 1]), (id >> 8) & 0xff);
        CHECK_EQ(uint32_t(od.rgba[i * 4 + 2]), id & 0xff);
        /* GD5 writes the same id as the tuple it keys the province by: id 1
         * is (1, 0, 0). */
        CHECK_EQ(uint32_t(gd.rgba[i * 4 + 0]), id & 0xff);
        CHECK_EQ(uint32_t(gd.rgba[i * 4 + 1]), (id >> 8) & 0xff);
        CHECK_EQ(uint32_t(gd.rgba[i * 4 + 2]), (id >> 16) & 0xff);
    }

    CHECK(rasterFromOd(od) == ids);
    CHECK(rasterFromGd5(gd) == ids);
    /* And the crossing itself: read one, write the other, read it back. */
    CHECK(rasterFromGd5(rasterToGd5(rasterFromOd(od), w, 1)) == ids);
}

static void testPngRoundTrip() {
    World w = fixture::makeWorld();
    const Image img = rasterToOd(w.raster, w.width, w.height);
    const std::vector<uint8_t> png = encodePng(img);
    CHECK(!png.empty());

    Image back;
    REQUIRE(decodePng(png, back));
    CHECK_EQ(back.width, w.width);
    CHECK_EQ(back.height, w.height);
    /* A province id that survived a re-encode as a different number would
     * corrupt a map in a way that only shows up as a rendering fault later. */
    CHECK(rasterFromOd(back) == w.raster);
}

static void testAdjacency() {
    World w = fixture::makeWorld();
    const auto adj = computeAdjacency(w.raster, w.width, w.height, /*wrap_x=*/true);

    CHECK(adj.count(1) && adj.at(1).count(2));   // sea touches the coast
    CHECK(adj.count(3) && adj.at(3).count(4));   // the crescent wraps the block
    CHECK(!adj.at(3).count(2));                  // and keeps them apart
    /* Province 5 spans the seam. Without the wrap its two halves never meet
     * the same neighbour, and GD5 would draw a map you cannot sail around. */
    CHECK(adj.count(5) && adj.at(5).count(6));
    CHECK(adj.at(5).count(2));

    const auto noWrap = computeAdjacency(w.raster, w.width, w.height, /*wrap_x=*/false);
    CHECK(noWrap.at(5).size() <= adj.at(5).size());

    /* Adjacency is symmetric or the two games disagree about who borders whom. */
    for (const auto& kv : adj) {
        for (uint32_t other : kv.second) {
            CHECK(adj.count(other) && adj.at(other).count(kv.first));
        }
    }
}

static void testCenters() {
    World w = fixture::makeWorld();
    const auto centers = computeCenters(w.raster, w.width, w.height);

    /* Every centre must land on its own province. Province 4 is the reason
     * this test exists: it is a crescent, and its centroid falls inside
     * province 3, where GD5 would have drawn its unit counters. */
    for (const auto& kv : centers) {
        const uint32_t at = provinceAt(w.raster, w.width, w.height, kv.second.first,
                                       kv.second.second);
        CHECK_EQ(at, kv.first);
    }
    CHECK(centers.count(4));
}

static void testLandSea() {
    World w = fixture::makeWorld();
    std::set<uint32_t> sea = {1};
    const Image ls = landSeaImage(w.raster, w.width, w.height, sea);

    /* Open Doctrines asks one question of this layer -- is the red channel
     * over 128 -- so those are the only two values worth writing. */
    CHECK_EQ(int(ls.rgba[0]), 40);
    const size_t land = (static_cast<size_t>(20) * w.width + 10) * 4;
    CHECK_EQ(int(ls.rgba[land]), 200);

    CHECK(seaIdsFromLandSea(ls, w.raster) == sea);
}

static void testLonLat() {
    /* Ships are placed by latitude and longitude and units by province, so a
     * fleet crossing depends on this pair agreeing with the game's own. */
    int x = 0, y = 0;
    lonLatToPixel(-180.0, 90.0, 8192, 4096, x, y);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    lonLatToPixel(0.0, 0.0, 8192, 4096, x, y);
    CHECK_EQ(x, 4096);
    CHECK_EQ(y, 2048);

    double lon = 0, lat = 0;
    pixelToLonLat(4096, 2048, 8192, 4096, lon, lat);
    CHECK(lon == 0.0);
    CHECK(lat == 0.0);
}

int main() {
    testIdEncodings();
    testPngRoundTrip();
    testAdjacency();
    testCenters();
    testLandSea();
    testLonLat();
    return check::finish("test_raster");
}
