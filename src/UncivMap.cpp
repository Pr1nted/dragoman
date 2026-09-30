/* Unciv: a hex grid, where both other games are pixel rasters.
 *
 * This is not a field mapping like GD5's. Open Doctrines and Greater Diplomacy
 * both paint provinces onto a raster -- a place is an arbitrary region of many
 * pixels -- and Unciv has a hexagon per place, with a terrain on it. Crossing
 * between them is a RESAMPLING, and a resampling loses information no matter
 * how carefully it is done.
 *
 * So the rule that makes a round trip lossless here is the one the sidecar
 * already exists for: the original raster and province table ride along
 * untouched, and coming home restores them rather than rebuilding them from
 * hexes. What a player edits in Unciv and brings back is a different question,
 * and a harder one -- see docs/unciv.md.
 *
 *   map.json          the TileMap: mapParameters and a flat tileList
 *
 * Unciv stores a tile's position in AXIAL HEX COORDINATES, not row and column,
 * and its own HexMath is the only thing that agrees with its renderer about
 * which hex is where. getTileCoordsFromColumnRow is reproduced here rather
 * than approximated: a map written on a grid Unciv does not share is one whose
 * continents are sheared apart.
 */
#include "Formats.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace dragoman {

namespace {

/* Unciv's own vocabulary, from android/assets/jsons/Civ V - Vanilla/Terrains.json.
 *
 * NOTE WHAT IS AND IS NOT A BASE TERRAIN. Unciv has six land bases --
 * Grassland, Plains, Tundra, Desert, Mountain, Snow -- and Hill, Forest, Jungle
 * and Marsh are FEATURES laid on top of one. Writing "Hill" as a base terrain
 * gives a map whose ruleset validation fails and which the game will not
 * start. */
struct UncivTerrain {
    const char* base;
    const char* feature;  /* nullptr for none */
};

/* The model's terrain tokens are GD5's thirteen. */
UncivTerrain uncivTerrainFor(const std::string& token) {
    if (token == "ocean") return {"Ocean", nullptr};
    if (token == "coastal_sea") return {"Coast", nullptr};
    if (token == "inland_sea" || token == "lakes") return {"Lakes", nullptr};
    if (token == "mountain") return {"Mountain", nullptr};
    if (token == "hills") return {"Plains", "Hill"};
    if (token == "desert") return {"Desert", nullptr};
    if (token == "plains") return {"Plains", nullptr};
    if (token == "forest") return {"Grassland", "Forest"};
    if (token == "jungle") return {"Plains", "Jungle"};
    if (token == "swamp") return {"Grassland", "Marsh"};
    if (token == "tundra") return {"Tundra", nullptr};
    if (token == "frozen") return {"Snow", nullptr};
    return {"Grassland", nullptr};
}

/* A LAND terrain for a province that has none.
 *
 * Open Doctrines stores no terrain at all -- its raster says land or sea and
 * nothing else -- so a map from there would arrive as an undifferentiated
 * grassland world, which is playable in the sense that it loads and dull in
 * every other sense.
 *
 * This invents a climate from latitude, which is the one thing an
 * equirectangular world map does tell us. It is INVENTED, and the conversion
 * says so: no source field is being translated here, and a map that crosses
 * back does not carry these terrains home -- the sidecar's original provinces
 * do.
 */
const char* climateFor(double latitude_deg) {
    const double a = std::fabs(latitude_deg);
    if (a >= 75.0) return "Snow";
    if (a >= 60.0) return "Tundra";
    if (a >= 35.0) return "Plains";
    if (a >= 30.0) return "Desert";   /* the horse latitudes, roughly */
    if (a >= 23.0) return "Plains";
    return "Grassland";               /* the tropics; Jungle is added as a feature */
}

/* Unciv's HexMath.getTileCoordsFromColumnRow, reproduced.
 *
 * Its renderer and its pathfinding both read these, so a map written on any
 * other grid is one whose land does not join up. Kept in its own function so
 * the correspondence with the original is easy to check.
 */
struct HexCoord { int x, y; };

HexCoord hexFromColumnRow(int column, int row) {
    /* Transcribed from HexMath.kt, and transcribed rather than derived on
     * purpose. The first version of this function was written from memory of
     * how axial coordinates usually work; it was injective, it produced a
     * plausible-looking map, and it agreed with Unciv nowhere. A grid the game
     * does not share is one whose continents are cut apart and scattered, and
     * nothing about the output file looks wrong.
     *
     *   var twoRows = row * 2
     *   if (abs(column) % 2 == 1) twoRows += 1
     *   return HexCoord.of((twoRows - column) / 2, (twoRows + column) / 2)
     */
    int twoRows = row * 2;
    if (std::abs(column) % 2 == 1) twoRows += 1;
    return {(twoRows - column) / 2, (twoRows + column) / 2};
}

}  // namespace

/* ------------------------------------------------------- model -> Unciv */

bool writeUncivMap(const std::string& path, const World& world, const Options& opt,
                   Report& report) {
    if (world.width <= 0 || world.height <= 0 || world.raster.empty()) {
        setLastError("the world has no province raster to resample");
        return false;
    }

    /* THE GRID SIZE, and why it is not the raster's.
     *
     * A world map is a few thousand pixels across and Unciv's largest standard
     * map is 80 wide. Writing one hex per pixel would give a map no build of
     * the game can open, so the raster is sampled down -- and the sampling is
     * what decides whether the result is playable.
     *
     * Each hex reads the province under its CENTRE rather than a majority vote
     * over the pixels it covers. A vote sounds better and is worse here: it
     * dissolves every province narrower than the sample step, and on a world
     * map that is most islands and most of Europe. */
    /* Fixed, and not an option, because dg_options is a struct the CALLER
     * allocates: every binding declares its six ints, and a seventh field
     * would have them pass a struct smaller than the library reads. That is an
     * ABI break for a map size, so the size waits for a release that has other
     * reasons to bump the ABI.
     *
     * 80x50 is Unciv's "Huge" rectangular map, which is the largest a world
     * has any chance of surviving into. */
    const int columns = 80;
    const int rows = 50;

    std::map<int64_t, const Province*> byId;
    for (const auto& p : world.provinces) byId[p.id] = &p;

    Json tiles = Json::array();
    long water = 0, land = 0, invented = 0;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            /* The centre of this hex, in raster pixels. */
            const int px = static_cast<int>((col + 0.5) * world.width / columns);
            const int py = static_cast<int>((row + 0.5) * world.height / rows);
            const size_t idx = static_cast<size_t>(py) * world.width + px;
            const uint32_t id = idx < world.raster.size() ? world.raster[idx] : 0;

            const Province* prov = nullptr;
            const auto it = byId.find(static_cast<int64_t>(id));
            if (it != byId.end()) prov = it->second;

            std::string base, feature;
            bool isWater = false;

            if (prov == nullptr || id == 0) {
                /* Unpainted. Open Doctrines means open water by it; that is
                 * the same question the GD5 writer asks before inventing an
                 * ocean, and it is asked the same way. */
                base = "Ocean";
                isWater = true;
            } else if (!prov->terrain.empty()) {
                const UncivTerrain t = uncivTerrainFor(prov->terrain);
                base = t.base;
                if (t.feature) feature = t.feature;
                isWater = prov->is_sea;
            } else if (prov->is_sea) {
                base = "Ocean";
                isWater = true;
            } else {
                /* No terrain anywhere in the source: invent a climate. */
                const double lat = 90.0 - 180.0 * (py + 0.5) / world.height;
                base = climateFor(lat);
                if (std::fabs(lat) < 12.0) feature = "Jungle";
                ++invented;
            }

            if (isWater) ++water; else ++land;

            const HexCoord h = hexFromColumnRow(col, row);
            Json tile = Json::object();
            tile["position"] = Json{{"x", h.x}, {"y", h.y}};
            tile["baseTerrain"] = base;
            if (!feature.empty()) tile["terrainFeatures"] = Json::array({feature});
            tiles.push_back(tile);
        }
    }

    Json params = Json::object();
    params["shape"] = "Rectangular";
    params["mapSize"] = Json{{"radius", 0}, {"width", columns}, {"height", rows}};
    params["name"] = world.name.empty() ? std::string("Translated map") : world.name;
    params["type"] = "Custom";
    /* Unciv wraps a rectangular world east to west, and so does every map this
     * library reads. */
    params["worldWrap"] = true;

    Json map = Json::object();
    map["mapParameters"] = params;
    map["tileList"] = tiles;

    if (!writeFile(path, map.dump())) {
        setLastError("could not write " + path);
        return false;
    }

    report.info("unciv.write",
                "resampled " + std::to_string(world.provinces.size()) + " province(s) onto a "
                + std::to_string(columns) + "x" + std::to_string(rows)
                + " hex grid (" + std::to_string(land) + " land, " + std::to_string(water)
                + " water)");
    if (invented > 0) {
        report.warn("unciv.terrain",
                    std::to_string(invented) + " hex(es) were given a climate derived from "
                    "latitude, because the source map stores no terrain at all -- Open "
                    "Doctrines' raster says land or sea and nothing else. Nothing was "
                    "translated into them; they are invented so the map is playable, and the "
                    "map that comes home carries the sidecar's provinces rather than these");
    }
    if (opt.carry_sidecar) {
        report.info("unciv.sidecar",
                    "the original raster and province table ride in the sidecar, so a map that "
                    "crosses back is the map that set out rather than one rebuilt from hexes");
    }
    return true;
}

/* ------------------------------------------------------- Unciv -> model */

bool readUncivMap(const std::string& path, const Options& opt, World& world, Report& report) {
    (void)path; (void)opt; (void)world;
    /* NOT IMPLEMENTED, and saying so rather than doing it badly.
     *
     * Reading is not the mirror of writing here. Writing samples a raster onto
     * hexes and loses the province boundaries; reading would have to invent
     * them back, by merging contiguous same-owner hexes into regions that never
     * existed in that shape. The result would load, and would not be the map
     * anybody drew.
     *
     * What makes the export direction honest is that the ORIGINAL rides in the
     * sidecar, so a map that crosses and comes back is the map that set out.
     * Doing that properly needs somewhere to put a sidecar beside a single
     * JSON file, and a decision about what a player's edits in Unciv should
     * mean on the way home -- neither of which is answered by guessing here.
     *
     * See docs/unciv.md. */
    report.error("unciv.read",
                 "reading Unciv maps is not implemented. Converting TO Unciv works; coming back "
                 "would have to invent province boundaries the hex grid does not carry, and a "
                 "map rebuilt that way is not the map anybody drew");
    setLastError("reading Unciv maps is not implemented yet");
    return false;
}

}  // namespace dragoman
