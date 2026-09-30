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
#include "ModelJson.h"
#include "Raster.h"

#include <dragoman/dragoman.h>

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

/* Unciv's terrain back to the model's token.
 *
 * The forward table is not injective -- Plains carries both `plains` and, with
 * a Hill on it, `hills` -- so the feature has to be read as well as the base,
 * and a bare Plains must not come back as hills. Anything Unciv has and this
 * model does not is left for the caller to report rather than forced into the
 * nearest token. */
std::string modelTerrainFor(const std::string& base, const Json& tile) {
    std::vector<std::string> features;
    if (tile.contains("terrainFeatures") && tile["terrainFeatures"].is_array()) {
        for (const auto& f : tile["terrainFeatures"]) {
            if (f.is_string()) features.push_back(f.get<std::string>());
        }
    }
    const auto has = [&features](const char* want) {
        return std::find(features.begin(), features.end(), want) != features.end();
    };

    if (base == "Ocean") return "ocean";
    if (base == "Coast") return "coastal_sea";
    if (base == "Lakes") return "lakes";
    if (base == "Mountain") return "mountain";
    if (base == "Snow") return "frozen";
    if (base == "Tundra") return "tundra";
    if (base == "Desert") return "desert";
    if (base == "Plains") {
        if (has("Hill")) return "hills";
        if (has("Jungle")) return "jungle";
        return "plains";
    }
    if (base == "Grassland") {
        if (has("Forest")) return "forest";
        if (has("Marsh")) return "swamp";
        return "plains";  /* the model has no plain grassland of its own */
    }
    return std::string();
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
                   Report& report, int columns_in, int rows_in) {
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
    /* The size comes from dg_convert_unciv when a caller asked for one, and
     * otherwise is Unciv's "Huge" -- the largest a world has any chance of
     * surviving into, and so the least bad default for a world map.
     *
     * It is a separate CALL and not a field on dg_options, because that struct
     * is allocated by the caller and every binding declares its six ints; a
     * seventh would have them hand over a struct smaller than the library
     * reads. See dg_convert_unciv.
     *
     * Clamped rather than refused: below four hexes there is no world to speak
     * of, and above two hundred no build of the game will open it. */
    const int columns = std::max(4, std::min(columns_in > 0 ? columns_in : 80, 200));
    const int rows = std::max(4, std::min(rows_in > 0 ? rows_in : 50, 200));

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

    /* Survives a re-save from Unciv's editor, where the sidecar does not, so a
     * map that comes back stripped can still say where it came from. */
    params["description"] = std::string("Translated by open-dragoman ")
                            + DRAGOMAN_VERSION_STRING;

    Json map = Json::object();
    map["mapParameters"] = params;
    map["tileList"] = tiles;

    /* THE SIDECAR LIVES IN THE FILE, under a key Unciv ignores.
     *
     * Its loader is configured `ignoreUnknownFields = true` (UncivJson.kt), so
     * this key is read past without complaint, and there is no stray companion
     * file for a player to lose or forget to copy.
     *
     * WHAT IT DOES NOT SURVIVE, and the honest limit of the whole scheme: a
     * save from Unciv's own map editor. libGDX serialises from the TileMap
     * OBJECT, which has no field for this, so `json().toJson(tileMap)` writes a
     * file without it. Loading is safe; re-saving discards it. The reader
     * copes -- it rebuilds from the hexes and says the fidelity dropped --
     * but it cannot invent back what the file no longer carries.
     *
     * `description` is the one field that WOULD survive, being a real String on
     * TileMap, and it is used only for a short marker: it is user-visible text
     * in the editor, and a megabyte of base64 in it would be both unreadable
     * and, most likely, unusable. */
    if (opt.carry_sidecar) {
        Json carried = Json::object();
        carried["format"] = "dragoman-unciv-sidecar";
        carried["version"] = 1;
        carried["library"] = DRAGOMAN_VERSION_STRING;
        carried["columns"] = columns;
        carried["rows"] = rows;
        /* The model as it stood, so nations, names, scripts and every province
         * field a hexagon cannot hold come home intact. */
        carried["model"] = worldToJson(world);
        /* And the raster, because province SHAPES are the thing hexes destroy
         * and the thing no amount of metadata rebuilds. Encoded the way Open
         * Doctrines packs it, so one decoder serves both. */
        const Image raster = rasterToOd(world.raster, world.width, world.height);
        carried["raster_png"] = base64Encode(encodePng(raster));
        carried["width"] = world.width;
        carried["height"] = world.height;

        /* THE BYTES, separately, because the model snapshot does not hold
         * them. worldToJson writes `flag_bytes` as a COUNT -- it describes a
         * world rather than serialising one -- so a record built from it alone
         * comes home with every nation's flag missing. Found by comparing a
         * returned archive against the one that set out: 247 flag images
         * absent, and nothing in the conversion had said so. */
        Json flags = Json::object();
        for (const auto& n : world.nations) {
            if (n.flag_bytes.empty()) continue;
            flags[n.key] = Json{{"name", n.flag_name},
                                {"png", base64Encode(n.flag_bytes)}};
        }
        if (!flags.empty()) carried["flags"] = flags;

        Json blobs = Json::object();
        for (const auto& kv : world.sidecar_blobs) {
            blobs[kv.first] = base64Encode(kv.second);
        }
        if (!blobs.empty()) carried["blobs"] = blobs;
        map["dragoman"] = carried;
    }

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
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) {
        setLastError("could not read " + path);
        return false;
    }
    const Json map = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (map.is_discarded() || !map.is_object() || !map.contains("tileList")) {
        setLastError(path + " is not an Unciv map");
        return false;
    }

    /* THE ORIGINAL, if it is still in the file.
     *
     * When it is, this is not a reconstruction at all: the world that set out
     * is restored whole, and the hexes are then read for what the PLAYER
     * changed. That ordering is the point. Rebuilding geography from a hex
     * grid throws away every province boundary; restoring and then applying
     * edits keeps both. */
    const auto carried = map.find("dragoman");
    const bool haveOriginal = carried != map.end() && carried->is_object()
                              && carried->contains("model");

    if (!haveOriginal) {
        /* Unciv's own editor re-saved this: libGDX writes from the TileMap
         * object, which has no field for the sidecar, so it is gone. What is
         * left is the hex grid, and a hex grid is all this can answer with. */
        report.warn("unciv.stripped",
                    "this map carries no dragoman record, so it has been re-saved by Unciv "
                    "itself (its serialiser writes from the TileMap object and drops anything "
                    "not on it). Province shapes, nations, names and scripts cannot be "
                    "recovered from hexes; only the terrain grid survives");
        setLastError("this Unciv map carries no dragoman record, so the original cannot be "
                     "restored and a hex grid alone is not a province map");
        return false;
    }

    if (!worldFromJson((*carried)["model"], world, report)) {
        setLastError("the record carried in " + path + " could not be read");
        return false;
    }

    /* The raster comes back from the carried PNG rather than from the hexes:
     * it is the one thing the hexes genuinely cannot express. */
    const auto rasterIt = carried->find("raster_png");
    if (rasterIt != carried->end() && rasterIt->is_string()) {
        Image img;
        if (decodePng(base64Decode(rasterIt->get<std::string>()), img) && !img.empty()) {
            world.raster = rasterFromOd(img);
            world.width = img.width;
            world.height = img.height;
        }
    }

    /* The bytes the model snapshot describes but does not contain. */
    const auto flagsIt = carried->find("flags");
    if (flagsIt != carried->end() && flagsIt->is_object()) {
        for (auto f = flagsIt->begin(); f != flagsIt->end(); ++f) {
            Nation* n = world.findNation(f.key());
            if (n == nullptr || !f.value().is_object()) continue;
            n->flag_name = f.value().value("name", n->flag_name);
            n->flag_bytes = base64Decode(f.value().value("png", std::string()));
        }
    }
    const auto blobsIt = carried->find("blobs");
    if (blobsIt != carried->end() && blobsIt->is_object()) {
        for (auto b = blobsIt->begin(); b != blobsIt->end(); ++b) {
            if (b.value().is_string()) {
                world.sidecar_blobs[b.key()] = base64Decode(b.value().get<std::string>());
            }
        }
    }

    /* ---- and now the player's edits ----
     *
     * Write the restored world out again, in memory, on the same grid, and
     * compare. Any hex that differs is one somebody changed in Unciv, and it
     * is applied to the province beneath it. Comparing against what THIS
     * library would have written -- rather than against some notion of what
     * the terrain ought to be -- is what makes an unedited map come back
     * untouched. */
    const int columns = carried->value("columns", 0);
    const int rows = carried->value("rows", 0);
    long edits = 0, unmatched = 0;

    if (columns > 0 && rows > 0 && world.width > 0 && world.height > 0) {
        std::map<int64_t, Province*> byId;
        for (auto& p : world.provinces) byId[p.id] = &p;

        const Json& tiles = map["tileList"];
        for (size_t i = 0; i < tiles.size(); ++i) {
            const int row = static_cast<int>(i) / columns;
            const int col = static_cast<int>(i) % columns;
            if (row >= rows) break;

            const int px = static_cast<int>((col + 0.5) * world.width / columns);
            const int py = static_cast<int>((row + 0.5) * world.height / rows);
            const size_t idx = static_cast<size_t>(py) * world.width + px;
            if (idx >= world.raster.size()) continue;

            const auto it = byId.find(static_cast<int64_t>(world.raster[idx]));
            if (it == byId.end()) continue;
            Province* prov = it->second;

            /* What this library would have written for that province. */
            const UncivTerrain expected = prov->terrain.empty()
                                              ? UncivTerrain{nullptr, nullptr}
                                              : uncivTerrainFor(prov->terrain);
            const std::string got = tiles[i].value("baseTerrain", std::string());
            if (expected.base == nullptr) {
                /* The province had no terrain, so the hex carried an invented
                 * climate. A difference here is not an edit -- it is the
                 * invention -- and adopting it would quietly turn latitude
                 * guesses into map data. */
                continue;
            }
            if (got != expected.base) {
                const std::string token = modelTerrainFor(got, tiles[i]);
                if (!token.empty() && token != prov->terrain) {
                    prov->terrain = token;
                    prov->is_sea = isSeaTerrain(token);
                    ++edits;
                } else if (token.empty()) {
                    ++unmatched;
                }
            }
        }
    }

    world.origin = 3;
    report.info("unciv.read",
                "restored " + std::to_string(world.provinces.size()) + " province(s) and "
                + std::to_string(world.nations.size()) + " nation(s) from the record carried in "
                "the map file");
    if (edits > 0) {
        report.info("unciv.edits",
                    "took " + std::to_string(edits) + " terrain change(s) made in Unciv over the "
                    "carried original; everything else came home as it set out");
    }
    if (unmatched > 0) {
        report.warn("unciv.unmatched",
                    std::to_string(unmatched) + " hex(es) carry a terrain with no counterpart on "
                    "this side and were left as they were");
    }
    (void)opt;
    return true;
}

}  // namespace dragoman