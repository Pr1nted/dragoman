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

/* A nation as one of Unciv's civilizations, for starting locations when the
 * map is written WITHOUT a mod.
 *
 * Only exact matches live here: a modern state and the civilization named
 * after it. There used to be a second group standing a modern state in for a
 * historical civ Civ V happens to ship -- Italy for Rome, Iraq for Babylon,
 * Mexico for the Aztecs. Those were this library deciding something about the
 * world rather than translating it, and DG_FORMAT_UNCIV_MOD removed the only
 * argument for keeping them: a mod carries every nation under its own name,
 * so nothing has to be stood in for. A nation with no exact match now gets no
 * starting location and Unciv places that civ itself, which is what it did
 * for every map before any of this existed. */
struct UncivCiv {
    const char* iso;
    const char* civ;
};

const UncivCiv kCivs[] = {
    {"CHN", "China"},    {"EGY", "Egypt"},   {"GBR", "England"}, {"FRA", "France"},
    {"RUS", "Russia"},   {"USA", "America"}, {"JPN", "Japan"},   {"IND", "India"},
    {"DEU", "Germany"},  {"GER", "Germany"}, {"KOR", "Korea"},   {"ESP", "Spain"},
    {"GRC", "Greece"},   {"DNK", "Denmark"}, {"MNG", "Mongolia"},
};

const char* uncivCivFor(const std::string& iso) {
    for (const UncivCiv& c : kCivs) {
        if (iso == c.iso) return c.civ;
    }
    return nullptr;
}

/* The model's deposits as Unciv's resources.
 *
 * Only four of Open Doctrines' five have a vanilla counterpart. RUBBER DOES
 * NOT: Civ V put rubber in a later ruleset, and inventing a stand-in (Dyes is
 * the usual suggestion, being the other plantation luxury) would mean a map
 * that came back from Unciv had rubber where it had none and none where it
 * had rubber. It is left out and said out loud, like the invented climate.
 *
 * `terrains` is TileResources.json's terrainsCanBeFoundOn, and it is tight --
 * Gems only on jungle, forest or hill; Gold Ore not on grassland or plains at
 * all. A deposit whose province lands on the wrong hex does not cross. Matched
 * against the BASE and the FEATURE, since Unciv counts a jungle hex as jungle
 * even though its base is Plains. */
struct UncivResource {
    const char* deposit;      /* the model's key                     */
    const char* name;         /* Unciv's resource                    */
    bool        strategic;    /* strategic resources carry an amount */
    const char* terrains[8];  /* nullptr-terminated                  */
};

const UncivResource kResources[] = {
    {"gold",      "Gold Ore", false, {"Desert", "Hill", "Forest", "Jungle", nullptr}},
    {"gemstones", "Gems",     false, {"Jungle", "Forest", "Hill", nullptr}},
    {"metal",     "Iron",     true,  {"Grassland", "Plains", "Desert", "Tundra", "Snow",
                                      "Hill", "Marsh", nullptr}},
    {"oil",       "Oil",      true,  {"Desert", "Coast", "Tundra", "Snow", "Marsh",
                                      "Jungle", nullptr}},
};

const UncivResource* uncivResourceFor(const std::string& deposit) {
    for (const UncivResource& r : kResources) {
        if (deposit == r.deposit) return &r;
    }
    return nullptr;
}

bool resourceCanSitOn(const UncivResource& r, const std::string& base,
                      const std::string& feature) {
    for (const char* const* t = r.terrains; *t != nullptr; ++t) {
        if (base == *t || (!feature.empty() && feature == *t)) return true;
    }
    return false;
}

/* Unciv wants a small integer for a strategic deposit; Open Doctrines carries
 * a surface figure that runs from 0.1 to 128 with a long tail. A square root
 * puts that on a 1..9 scale without the top of the range swallowing the rest:
 * the median oil field lands on 5, the median iron on 3. The exact figure is
 * in the sidecar, so nothing is decided by this. */
int strategicAmountFor(double surface) {
    if (surface <= 0.0) return 1;
    const long n = dragoman::dgRoundToL(std::sqrt(surface));
    if (n < 1) return 1;
    if (n > 9) return 9;
    return static_cast<int>(n);
}

/* An Unciv resource back to the deposit it came from, for a map edited in the
 * game and brought home. Only the four that go out can come back; anything
 * else on a hex was put there by the player out of Unciv's own vocabulary and
 * has no deposit to become. */
const char* depositForUncivResource(const std::string& name) {
    for (const UncivResource& r : kResources) {
        if (name == r.name) return r.deposit;
    }
    return nullptr;
}

/* Where Unciv will accept a Fort, from TileImprovements.json's
 * terrainsCanBeBuiltOn. Mountain and Marsh are NOT on that list, and neither
 * is any water, so a fortified province that lands on one crosses without its
 * fort rather than with an improvement the ruleset refuses.
 *
 * Checked against the BASE and the FEATURE both. A hill is Plains+Hill and
 * both are allowed; a swamp is Grassland+Marsh and Marsh is not, so the swamp
 * is refused even though its base would pass on its own. Under-placing is the
 * safe direction: a missing fort is a smaller lie than a map that will not
 * load. */
bool fortCanStandOn(const std::string& base, const std::string& feature) {
    static const char* kAllowed[] = {
        "Plains", "Grassland", "Desert", "Hill", "Tundra", "Snow", "Forest", "Jungle",
    };
    const auto allowed = [](const std::string& t) {
        for (const char* a : kAllowed) {
            if (t == a) return true;
        }
        return false;
    };
    if (!allowed(base)) return false;
    return feature.empty() || allowed(feature);
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
                   Report& report, int columns_in, int rows_in,
                   const std::string& modName) {
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
    int columns = std::max(4, std::min(columns_in > 0 ? columns_in : 80, 200));
    const int rows = std::max(4, std::min(rows_in > 0 ? rows_in : 50, 200));

    /* World wrap needs an even width, and the map this writes always wraps --
     * it is a globe. Unciv's own rectangular constructor rounds an odd width
     * down for the same reason, so an odd --columns loses its last column here
     * rather than producing a map the game will not wrap. */
    if (columns % 2 != 0) --columns;

    /* The hex coordinates must be CENTRED ON THE ORIGIN, not run from zero.
     * TileMap.setTransients asserts it -- `tileMatrix.size in -2*leftX .. 3-2*leftX`
     * -- but only on the SECOND call, when the tileMatrix already exists. A map
     * editor loads a map with one call and is happy; GameStarter calls it again
     * through GameInfo.setTransients, and a map written 0..width-1 dies there
     * with "called on existing tileMatrix of different size". So the map looked
     * valid to every check this project had and could not start a game.
     * These are the exact ranges of TileMap(width, height, ruleset, worldWrap). */
    const int colMin = -(columns / 2), colMax = (columns - 1) / 2;
    const int rowMin = -(rows / 2),    rowMax = (rows - 1) / 2;

    std::map<int64_t, const Province*> byId;
    for (const auto& p : world.provinces) byId[p.id] = &p;

    Json tiles = Json::array();
    long water = 0, land = 0, invented = 0, forts = 0, fortsRefused = 0;
    long resourcesPlaced = 0, depositsRefusedByTerrain = 0, depositsWithNoCounterpart = 0;
    /* owner -> the most populous of its provinces that a hex actually landed on,
     * and where. A starting location has to be a hex, so a province no hex
     * sampled cannot host one however large it is. */
    std::map<std::string, std::pair<double, HexCoord>> seat;

    for (int row = rowMin; row <= rowMax; ++row) {
        for (int col = colMin; col <= colMax; ++col) {
            /* The centre of this hex, in raster pixels. The grid index is the
             * offset from the corner, which is no longer the coordinate. */
            const int ci = col - colMin, ri = row - rowMin;
            const int px = static_cast<int>((ci + 0.5) * world.width / columns);
            const int py = static_cast<int>((ri + 0.5) * world.height / rows);
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

            /* One deposit per hex, because Unciv has one `resource` field.
             * The largest surface figure that is legal on this terrain wins,
             * with the deposit name breaking a tie so two runs agree. */
            const UncivResource* res = nullptr;
            double resSurface = 0.0;
            if (prov != nullptr) {
                for (const auto& entry : prov->resources) {
                    const UncivResource* cand = uncivResourceFor(entry.first);
                    if (cand == nullptr) {
                        ++depositsWithNoCounterpart;
                        continue;
                    }
                    if (!resourceCanSitOn(*cand, base, feature)) {
                        ++depositsRefusedByTerrain;
                        continue;
                    }
                    if (res == nullptr || entry.second.a > resSurface) {
                        res = cand;
                        resSurface = entry.second.a;
                    }
                }
            }
            if (res != nullptr) ++resourcesPlaced;

            /* A fortified province crosses as Unciv's Fort. Unciv has one
             * fort where Open Doctrines has five levels and GD5 counts
             * buildings, so the level does not survive here -- it stays in
             * the sidecar, which is where the exact figures already live. */
            bool fort = false;
            if (prov != nullptr && prov->fortification > 0 && !isWater) {
                if (fortCanStandOn(base, feature)) {
                    fort = true;
                    ++forts;
                } else {
                    ++fortsRefused;
                }
            }

            const HexCoord h = hexFromColumnRow(col, row);
            Json tile = Json::object();
            tile["position"] = Json{{"x", h.x}, {"y", h.y}};
            tile["baseTerrain"] = base;
            if (!feature.empty()) tile["terrainFeatures"] = Json::array({feature});
            if (res != nullptr) {
                tile["resource"] = res->name;
                if (res->strategic) tile["resourceAmount"] = strategicAmountFor(resSurface);
            }
            if (fort) tile["improvement"] = "Fort";
            if (prov != nullptr && !prov->owner.empty() && !isWater) {
                auto seatIt = seat.find(prov->owner);
                if (seatIt == seat.end() || prov->population > seatIt->second.first) {
                    seat[prov->owner] = std::make_pair(
                        static_cast<double>(prov->population), h);
                }
            }
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
    if (!modName.empty()) {
        /* Without this the map loads against plain vanilla and every
         * startingLocation below names a civilization the ruleset has never
         * heard of. */
        params["mods"] = Json::array({modName});
    }

    /* Starting locations, for the nations Unciv has a civilization for. The
     * seat is the nation's most populous province that a hex landed on --
     * population being the only thing in the model that says where a country
     * actually lives, since neither game records a capital. */
    Json starts = Json::array();
    long seated = 0, unseated = 0;
    for (const auto& entry : seat) {
        std::string nation;
        if (!modName.empty()) {
            /* The mod carries every nation as a civilization, so each one can
             * be named outright and nothing has to be matched to Civ V. */
            const Nation* n = world.findNation(entry.first);
            if (n != nullptr && !n->name.empty()) nation = n->name;
        } else if (const char* civ = uncivCivFor(entry.first)) {
            nation = civ;
        }
        if (nation.empty()) {
            ++unseated;
            continue;
        }
        Json sl = Json::object();
        sl["position"] = Json{{"x", entry.second.second.x}, {"y", entry.second.second.y}};
        sl["nation"] = nation;
        starts.push_back(sl);
        ++seated;
    }

    Json map = Json::object();
    map["mapParameters"] = params;
    map["tileList"] = tiles;
    if (!starts.empty()) map["startingLocations"] = starts;

    /* ON THE MAP, NOT ON ITS PARAMETERS. `description` is a field of TileMap;
     * MapParameters has no such field, so putting it there means Unciv ignores
     * it and TileMap.description stays empty -- which is exactly what this did
     * until the game's own loader was asked and reported an empty string.
     *
     * It is here because it is the one thing that SURVIVES a save from Unciv's
     * editor. Verified by loading a map with the game's MapSaver and saving it
     * again: the dragoman key below does not come back, and this does. */
    map["description"] = std::string("Translated by open-dragoman ")
                         + DRAGOMAN_VERSION_STRING
                         + ". The record this map carried has been removed by "
                           "re-saving it in Unciv; converting it back will no longer "
                           "restore the original world.";

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
    if (seated > 0 || unseated > 0) {
        report.info("unciv.starts",
                    std::to_string(seated) + " nation(s) have a starting location, seated on "
                    "their most populous hex; " + std::to_string(unseated) +
                    " have no civilization in the ruleset and are left for Unciv to place, "
                    "as every map this library wrote before now was");
    }
    if (resourcesPlaced > 0 || depositsRefusedByTerrain > 0 || depositsWithNoCounterpart > 0) {
        report.info("unciv.resources",
                    std::to_string(resourcesPlaced) + " hex(es) carry a deposit as an Unciv "
                    "resource; " + std::to_string(depositsRefusedByTerrain) +
                    " deposit(s) fell on terrain the ruleset will not put them on, and " +
                    std::to_string(depositsWithNoCounterpart) +
                    " have no vanilla counterpart (rubber has none). Every figure is in the "
                    "sidecar regardless, so a return trip is unaffected");
    }
    if (forts > 0 || fortsRefused > 0) {
        std::string line = std::to_string(forts) + " fortified hex(es) carry Unciv's Fort";
        if (fortsRefused > 0) {
            line += "; " + std::to_string(fortsRefused) +
                    " could not, being mountain, marsh or water, which the ruleset "
                    "will not build a Fort on. The level itself is in the sidecar either way";
        }
        report.info("unciv.forts", line);
    }
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

/* The mod's identity is the NAME OF ITS FOLDER -- RulesetCache does
 * `modRuleset.name = modFolder.name()` and nothing else. So the name the map
 * declares has to be the directory the caller asked for, not the world's
 * title: a mod written into "ModReal" while the map asks for "Modern Day"
 * resolves to nothing, and every starting location in it names a civilization
 * the ruleset has never heard of. */
std::string modNameForDir(const std::string& dir) {
    std::string base = dir;
    while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    if (base.empty()) base = "Translated World";
    std::string out;
    for (char c : base) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == ' ' || c == '-') out += c;
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) out = "Translated World";
    return out;
}

bool writeUncivModImpl(const std::string& dir, const World& world, const Options& opt,
                       Report& report, int columns, int rows) {
    const std::string mod = modNameForDir(dir);

    if (!makeDirectories(dir + "/jsons") || !makeDirectories(dir + "/maps")) {
        setLastError("could not create the mod directories under " + dir);
        return false;
    }

    /* Nations.json. Unciv reads civilizations from the ruleset, so this is
     * what makes the map's own countries exist at all.
     *
     * UNC and BLC are left out on purpose: they are Open Doctrines' words for
     * unclaimed and blocked, not countries anybody plays. */
    std::map<std::string, std::vector<std::string>> cities;
    for (const Province& p : world.provinces) {
        if (p.owner.empty() || p.name.empty() || p.is_sea) continue;
        /* A province the generator numbered rather than named -- "Afghanistan
         * #1608" -- is not a city and reads badly as one. Open Doctrines' own
         * world map is all of these; its admin_1 maps have real names. Skip
         * them and fall back to the nation's name below if nothing is left. */
        if (p.name.find(" #") != std::string::npos) continue;
        std::vector<std::string>& list = cities[p.owner];
        if (list.size() < 30) list.push_back(p.name);
    }

    Json nations = Json::array();
    long written = 0, skipped = 0;
    for (const Nation& n : world.nations) {
        if (n.key == "UNC" || n.key == "BLC" || n.name.empty()) {
            ++skipped;
            continue;
        }
        const int r = static_cast<int>((n.color >> 16) & 0xFF);
        const int g = static_cast<int>((n.color >> 8) & 0xFF);
        const int b = static_cast<int>(n.color & 0xFF);

        Json nat = Json::object();
        nat["name"] = n.name;
        nat["leaderName"] = n.leader_name.empty() ? n.name : n.leader_name;
        nat["adjective"] = Json::array({n.adjective.empty() ? n.name : n.adjective});
        nat["outerColor"] = Json::array({r, g, b});
        /* Unciv draws the inner colour on top of the outer one, so it has to
         * contrast with it or the civ's name is unreadable on its own banner. */
        const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        nat["innerColor"] = luma > 140.0 ? Json::array({0, 0, 0}) : Json::array({255, 255, 255});

        Json cityList = Json::array();
        const auto it = cities.find(n.key);
        if (it != cities.end()) {
            for (const std::string& c : it->second) cityList.push_back(c);
        }
        if (cityList.empty()) cityList.push_back(n.name);
        nat["cities"] = cityList;

        nations.push_back(nat);
        ++written;
    }

    if (!writeFile(dir + "/jsons/Nations.json", nations.dump(1, '\t'))) {
        setLastError("could not write " + dir + "/jsons/Nations.json");
        return false;
    }

    std::string file = mod;
    for (char& c : file) {
        if (c == ' ') c = '-';
    }
    if (!writeUncivMap(dir + "/maps/" + file + ".json", world, opt, report, columns, rows, mod)) {
        return false;
    }

    report.info("unciv.mod",
                "wrote the mod \"" + mod + "\" with " + std::to_string(written) +
                " nation(s) as civilizations, each with its own colour and its provinces "
                "as city names; " + std::to_string(skipped) +
                " left out as unclaimed or unnamed. Drop the folder in Unciv's mods "
                "directory, and the map inside it is offered with its own countries");
    return true;
}

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
    long addedForts = 0, removedForts = 0, addedDeposits = 0, resourcesUnknown = 0;
    std::map<int64_t, int> fortWas;  /* fortification as the province arrived */

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
            /* The province had no terrain, so the hex carried an invented
             * climate. A difference there is not an edit -- it is the
             * invention -- and adopting it would quietly turn latitude
             * guesses into map data.
             *
             * This used to `continue`, which also skipped the fort and
             * deposit comparison below. Every province in an Open Doctrines
             * map has no terrain, so on a real map that was all of them: an
             * edit made in Unciv came home only if the source happened to be
             * GD5. The guard now covers the terrain comparison and nothing
             * else. */
            if (expected.base != nullptr && got != expected.base) {
                const std::string token = modelTerrainFor(got, tiles[i]);
                if (!token.empty() && token != prov->terrain) {
                    prov->terrain = token;
                    prov->is_sea = isSeaTerrain(token);
                    ++edits;
                } else if (token.empty()) {
                    ++unmatched;
                }
            }

            /* A fort or a deposit ADDED in Unciv's editor comes home. A
             * removal does not, and the asymmetry is deliberate: a province
             * covers several hexes and they all carry its fort, so clearing
             * one hex does not say the province is unfortified -- it says one
             * hex of five changed. An addition has no such ambiguity. What is
             * not adopted is counted and reported rather than passed over. */
            std::string feature;
            if (tiles[i].contains("terrainFeatures") && tiles[i]["terrainFeatures"].is_array()
                && !tiles[i]["terrainFeatures"].empty()
                && tiles[i]["terrainFeatures"][0].is_string()) {
                feature = tiles[i]["terrainFeatures"][0].get<std::string>();
            }

            /* Against the fortification the province ARRIVED with, not the
             * one it has part-way through this loop. Adopting a fort on one
             * hex raises the province's level, and every other hex of the
             * same province then reads as a removal -- six of them, on the
             * first real map this was run against. */
            const bool hasFort = tiles[i].value("improvement", std::string()) == "Fort";
            auto wasIt = fortWas.find(prov->id);
            const int fortBefore = wasIt == fortWas.end() ? prov->fortification
                                                          : wasIt->second;
            if (wasIt == fortWas.end()) fortWas[prov->id] = fortBefore;

            if (hasFort && fortBefore == 0) {
                if (prov->fortification == 0) ++addedForts;
                prov->fortification = 1;
            } else if (!hasFort && fortBefore > 0
                       && fortCanStandOn(got, feature) && !prov->is_sea) {
                ++removedForts;  /* counted, not adopted */
            }

            const std::string gotRes = tiles[i].value("resource", std::string());
            if (!gotRes.empty()) {
                const char* deposit = depositForUncivResource(gotRes);
                if (deposit == nullptr) {
                    ++resourcesUnknown;
                } else if (prov->resources.find(deposit) == prov->resources.end()) {
                    Resource r;
                    /* The amount is the square root of the surface on the way
                     * out, so square it coming back. A luxury carries no
                     * amount at all, so its figure is simply unknown and is
                     * recorded as present rather than invented large. */
                    const int amount = tiles[i].value("resourceAmount", 0);
                    r.a = amount > 0 ? static_cast<double>(amount) * amount : 1.0;
                    r.b = 0.0;
                    prov->resources[deposit] = r;
                    ++addedDeposits;
                }
            }
        }
    }

    if (addedForts > 0 || addedDeposits > 0) {
        report.info("unciv.adopted",
                    "took " + std::to_string(addedForts) + " fort(s) and " +
                    std::to_string(addedDeposits) + " deposit(s) added in Unciv");
    }
    if (removedForts > 0 || resourcesUnknown > 0) {
        report.info("unciv.notadopted",
                    std::to_string(removedForts) + " hex(es) had a fort cleared and " +
                    std::to_string(resourcesUnknown) + " carried a resource this library does "
                    "not translate; neither was taken, because a province spans several hexes "
                    "and one of them changing does not settle what the province is");
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

bool writeUncivMod(const std::string& dir, const World& world, const Options& opt,
                   Report& report, int columns, int rows) {
    return writeUncivModImpl(dir, world, opt, report, columns, rows);
}

}  // namespace dragoman
