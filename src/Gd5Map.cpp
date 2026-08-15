/* Greater Diplomacy 5: a directory, not an archive.
 *
 *   id_map.png    province id per pixel, packed little-endian
 *   terrain.png   terrain per pixel, from a fixed thirteen-colour palette
 *   political.png owner colour per pixel
 *   cores.png     claimant colour per pixel
 *   map_data.json province objects, keyed by the id's colour triple as text
 *   meta.json     date, settings, nations, and a per-province scenario overlay
 *   history.json  the event log, empty on a fresh map
 *
 * Two things about this format shape the code. The province key is the string
 * form of a Python tuple -- "(11, 0, 0)" -- so it is written with the spaces
 * Python's repr puts there, not without them, or GD5 looks up a province it
 * cannot find. And `map_data.json` and `meta.json` overlap: the first is the
 * base map, the second a scenario layer that repeats owner, cores and units
 * on top of it. Both are written, and consistently, because the loader reads
 * the overlay last and a disagreement between them would silently win.
 */
#include "Formats.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace dragoman {

namespace fs = std::filesystem;

namespace {

constexpr double kMenPerHealth = 1000.0;

/* data/constants.py: COLOR_CHROMA_PINK. Water and unowned space are painted
 * this in the political and cores layers, and the renderer sets it as the
 * surface's colorkey so the terrain beneath shows through. Writing an ordinary
 * blue there instead gives GD5 an opaque ocean pasted over its own map. */
constexpr uint32_t kChromaKey = 0xFF00FF;
constexpr uint32_t kChromaNudged = 0xFE00FF;

/* The palette map_tools/automatic_map_painter.py reads a hand-painted terrain
 * layer through. Writing exactly these values means a layer we generate can be
 * re-imported by GD5's own painter and come back with the same terrains. */
struct TerrainColor { const char* name; uint32_t rgb; };
const TerrainColor kTerrainPalette[] = {
    {"mountain", 0x8B4513}, {"hills", 0xFFFF00},    {"desert", 0xFFA500},
    {"plains", 0x90EE90},   {"forest", 0x00BF00},   {"jungle", 0x008000},
    {"swamp", 0xFF1493},    {"tundra", 0xD3D3D3},   {"frozen", 0xFFFFFF},
    {"ocean", 0x0000FF},    {"coastal_sea", 0x0085FF},
    {"inland_sea", 0x00C2FF}, {"lakes", 0x80FFFF},
};

uint32_t terrainColor(const std::string& name) {
    for (const auto& t : kTerrainPalette) {
        if (name == t.name) return t.rgb;
    }
    return 0x90EE90;  /* plains, the least surprising thing to be */
}

/* GD5 keys a province by the string Python prints for the colour tuple. */
std::string provinceKey(int64_t id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "(%d, %d, %d)",
                  int(id & 0xff), int((id >> 8) & 0xff), int((id >> 16) & 0xff));
    return std::string(buf);
}

Json readJsonFile(const std::string& path, Report& report) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes) || bytes.empty()) return Json();
    try {
        return Json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception& ex) {
        report.warn("gd5.badjson", path + " is not valid JSON and was skipped: " + ex.what());
        return Json();
    }
}

}  // namespace

bool isSeaTerrain(const std::string& t) {
    return t == "ocean" || t == "coastal_sea" || t == "inland_sea" || t == "lakes";
}

std::string defaultTerrainFor(bool is_sea, bool is_coastal) {
    if (is_sea) return is_coastal ? "coastal_sea" : "ocean";
    return "plains";
}

/* ------------------------------------------------------------------- read */

bool readGd5Map(const std::string& dir, const Options& opt, World& world, Report& report) {
    if (!isDirectory(dir)) {
        setLastError(dir + " is not a directory, so it is not a GD5 map");
        return false;
    }
    world.origin = 2;  /* DG_FORMAT_GD5 */

    std::vector<uint8_t> idBytes;
    if (!readFile(joinPath(dir, "id_map.png"), idBytes)) {
        setLastError("no id_map.png in " + dir);
        return false;
    }
    Image idImg;
    if (!decodePng(idBytes, idImg)) {
        setLastError("id_map.png could not be decoded");
        return false;
    }
    world.width = idImg.width;
    world.height = idImg.height;
    world.raster = rasterFromGd5(idImg);

    const Json mapData = readJsonFile(joinPath(dir, "map_data.json"), report);
    const Json meta = readJsonFile(joinPath(dir, "meta.json"), report);

    /* ---- world header ---- */
    world.name = fs::path(dir).filename().string();
    if (meta.is_object() && meta.contains("date") && meta["date"].is_object()) {
        const Json& d = meta["date"];
        world.date.year = d.value("year", 1);
        world.date.month = d.value("month", 0) + 1;  /* GD5 counts months from zero */
        world.date.day = d.value("day", 1);
        world.date.turn = d.value("total_turns", 0);
    }

    /* ---- nations ---- */
    /* Settled first, because every province owner and every relation below is
     * resolved through these codes. */
    const std::map<std::string, std::string> carriedKeys =
        opt.carry_sidecar ? readSidecarNationKeys(dir) : std::map<std::string, std::string>();

    if (meta.is_object() && meta.contains("nation_data") && meta["nation_data"].is_object()) {
        std::vector<std::string> taken;
        for (auto it = meta["nation_data"].begin(); it != meta["nation_data"].end(); ++it) {
            const Json& nd = it.value();
            Nation n;
            /* The nation's identity is the key it sits under in nation_data,
             * not its "name" field. Every province owner, every core and every
             * entry in at_war_with is that key, and the two are allowed to
             * disagree: GD5's own 1914 scenario has two separate nations whose
             * name field reads "German Empire", and treating the name as the
             * identity merged them into one and handed 121 provinces to
             * nobody. The display name travels in extra, and is written back
             * to the field it came from. */
            n.name = it.key();
            /* The ISO code Open Doctrines needs does not exist here. A code
             * agreed on an earlier crossing wins, then the lookup table, and
             * only then is one invented -- so a nation keeps the code it was
             * given rather than collecting a new one per conversion. The
             * table is not enough on its own: it pairs "Czechoslovakia" with
             * CSK, while the map that arrived calls it CZE, and both are
             * codes Open Doctrines itself uses on different maps. */
            const auto carried = carriedKeys.find(n.name);
            if (carried != carriedKeys.end()) n.key = carried->second;
            if (n.key.empty()) n.key = isoForName(n.name);
            /* Uniqueness is checked against every code already handed out, not
             * only against the invented ones. GD5's world map names 556
             * nations and the lookup table cheerfully gives two of them the
             * same ISO code; the second then overwrote the first, and the map
             * came back with fewer countries than it left with. */
            const bool clash = n.key.empty()
                               || std::find(taken.begin(), taken.end(), n.key) != taken.end();
            if (clash) n.key = synthesiseIso(n.name, taken);
            taken.push_back(n.key);

            n.adjective = nd.value("adjective", "");
            n.leader_name = nd.value("leader_name", "");
            n.leader_title = nd.value("leader_title", "");
            n.playable = nd.value("is_playable", true);
            n.treasury = nd.value("materials", 0.0);

            if (nd.contains("color") && nd["color"].is_array() && nd["color"].size() >= 3) {
                n.color = (uint32_t(nd["color"][0].get<int>() & 0xff) << 16)
                          | (uint32_t(nd["color"][1].get<int>() & 0xff) << 8)
                          | uint32_t(nd["color"][2].get<int>() & 0xff);
            }

            const std::string flag = nd.value("flag_data", std::string("DEFAULT"));
            if (!flag.empty() && flag != "DEFAULT") {
                n.flag_bytes = base64Decode(flag);
                n.flag_name = "flags/" + n.key + ".png";
            }

            if (nd.contains("at_war_with") && nd["at_war_with"].is_array()) {
                for (const auto& t : nd["at_war_with"]) {
                    if (t.is_string()) n.relations[t.get<std::string>()].at_war = true;
                }
            }
            if (nd.contains("allied_with") && nd["allied_with"].is_array()) {
                for (const auto& t : nd["allied_with"]) {
                    if (t.is_string()) n.relations[t.get<std::string>()].ally = true;
                }
            }
            if (nd.contains("claims") && nd["claims"].is_array()) {
                for (const auto& c : nd["claims"]) {
                    if (c.is_number_integer()) n.claims.push_back(c.get<int64_t>());
                }
            }

            /* The research tree, faction membership, manpower, fuel, the
             * portrait and the diplomatic queue have no Open Doctrines
             * counterpart. They are kept whole rather than approximated. */
            Json extra = Json::object();
            for (auto f = nd.begin(); f != nd.end(); ++f) {
                static const char* handled[] = {"adjective", "color", "leader_name",
                                                "leader_title", "is_playable", "materials",
                                                "flag_data", "at_war_with", "allied_with",
                                                "claims", "scripted_events"};
                bool known = false;
                for (const char* k : handled) known = known || f.key() == k;
                if (!known) extra[f.key()] = f.value();
            }
            n.extra["gd5"] = extra;

            /* Scripted events belong to the nation that owns them. */
            if (opt.translate_scripts && nd.contains("scripted_events")
                && nd["scripted_events"].is_array()) {
                eventsFromGd5(nd["scripted_events"], n.key, world.events, report);
            }

            world.nations.push_back(std::move(n));
        }
    }

    /* Nation relations are keyed by display name here and by ISO code in the
     * model, so the names are translated once the whole roster is known. */
    std::map<std::string, std::string> nameToKey;
    for (const auto& n : world.nations) nameToKey[n.name] = n.key;
    for (auto& n : world.nations) {
        std::map<std::string, Relation> fixed;
        for (const auto& kv : n.relations) {
            const auto it = nameToKey.find(kv.first);
            fixed[it != nameToKey.end() ? it->second : kv.first] = kv.second;
        }
        n.relations = std::move(fixed);
    }

    /* ---- provinces ---- */
    const Json* overlay = nullptr;
    if (meta.is_object() && meta.contains("provinces") && meta["provinces"].is_object()) {
        overlay = &meta["provinces"];
    }

    if (mapData.is_object()) {
        for (auto it = mapData.begin(); it != mapData.end(); ++it) {
            const Json& p = it.value();
            Province prov;
            prov.id = p.value("id", int64_t(0));
            prov.name = p.value("name", std::string());
            prov.terrain = p.value("terrain", std::string());
            prov.is_sea = isSeaTerrain(prov.terrain);
            prov.is_coastal = p.value("is_coastal", false);

            const Json* src = &p;
            Json merged;
            if (overlay && overlay->contains(it.key())) {
                merged = p;
                for (auto o = (*overlay)[it.key()].begin(); o != (*overlay)[it.key()].end(); ++o) {
                    merged[o.key()] = o.value();
                }
                src = &merged;
            }

            /* Resolved through the roster and cleared only when the name is
             * not on it. It is tempting to treat "Ocean", "Lakes" and
             * "Unclaimed" as words meaning "nobody", but they are entries in
             * nation_data like any other, and Open Doctrines models the same
             * idea the same way -- its 1914 map gives 104 provinces to a
             * country whose ISO code is UNC and whose name is, exactly,
             * Unclaimed. Special-casing the names threw that country away and
             * handed its territory to no one. */
            const std::string ownerName = src->value("owner", std::string());
            const auto ownIt = nameToKey.find(ownerName);
            prov.owner = ownIt != nameToKey.end() ? ownIt->second : std::string();

            if (src->contains("center") && (*src)["center"].is_array()
                && (*src)["center"].size() >= 2) {
                prov.center_x = (*src)["center"][0].get<int>();
                prov.center_y = (*src)["center"][1].get<int>();
                prov.has_center = true;
            }
            if (src->contains("neighbors") && (*src)["neighbors"].is_array()) {
                for (const auto& n : (*src)["neighbors"]) {
                    if (n.is_number_integer()) prov.neighbors.push_back(n.get<int64_t>());
                }
                prov.has_neighbors = true;
            }
            if (src->contains("cores") && (*src)["cores"].is_array()) {
                for (const auto& c : (*src)["cores"]) {
                    if (!c.is_string()) continue;
                    const auto cit = nameToKey.find(c.get<std::string>());
                    prov.cores.push_back(cit != nameToKey.end() ? cit->second : c.get<std::string>());
                }
            }

            /* Buildings stand in for Open Doctrines' industry level: the
             * factories are counted, everything else is remembered. */
            if (src->contains("buildings") && (*src)["buildings"].is_array()) {
                int factories = 0;
                for (const auto& b : (*src)["buildings"]) {
                    if (b.is_string() && b.get<std::string>().find("Factory") != std::string::npos) {
                        ++factories;
                    }
                }
                prov.industry = factories;
                prov.extra["gd5_buildings"] = (*src)["buildings"];
            }

            if (src->contains("units") && (*src)["units"].is_array() && !(*src)["units"].empty()) {
                Json garrison = Json::array();
                for (const auto& u : (*src)["units"]) {
                    const std::string owner = u.value("owner", std::string());
                    const auto uit = nameToKey.find(owner);
                    const double health = u.value("health", 0.0);
                    garrison.push_back(Json{
                        {"owner", uit != nameToKey.end() ? uit->second : owner},
                        {"count", static_cast<int64_t>(std::llround(health * kMenPerHealth))},
                        {"health", health},
                        {"naval", u.value("naval_unit", false)},
                        {"gd5", u}});
                }
                prov.extra["garrison"] = garrison;
            }

            Json extra = Json::object();
            for (auto f = src->begin(); f != src->end(); ++f) {
                static const char* handled[] = {"id", "name", "terrain", "is_coastal", "center",
                                                "neighbors", "owner", "cores", "buildings",
                                                "units", "json_key", "map_color"};
                bool known = false;
                for (const char* k : handled) known = known || f.key() == k;
                if (!known) extra[f.key()] = f.value();
            }
            if (!extra.empty()) prov.extra["gd5"] = extra;

            world.provinces.push_back(std::move(prov));
        }
    }

    /* The same canonical order the Open Doctrines reader produces; see the
     * note there for why neither format's own order can be relied on. */
    std::sort(world.provinces.begin(), world.provinces.end(),
              [](const Province& a, const Province& b) { return a.id < b.id; });
    std::sort(world.nations.begin(), world.nations.end(),
              [](const Nation& a, const Nation& b) { return a.key < b.key; });
    for (auto& p : world.provinces) std::sort(p.cores.begin(), p.cores.end());
    /* Events are collected nation by nation, so their order is nation_data's
     * order, which is whatever the map was last saved with. Grouped by owner
     * they are stable across a crossing; stable_sort keeps each nation's own
     * events in the sequence its author wrote them, which matters because a
     * later event may undo an earlier one. */
    std::stable_sort(world.events.begin(), world.events.end(),
                     [](const Event& a, const Event& b) { return a.owner < b.owner; });

    /* A nation's claims and a province's cores are read as the two separate
     * facts GD5 stores; see the note in OdMap.cpp for why merging them loses
     * both. */

    if (opt.carry_sidecar) {
        Json settings = Json::object();
        if (meta.is_object()) {
            for (auto f = meta.begin(); f != meta.end(); ++f) {
                if (f.key() == "nation_data" || f.key() == "provinces" || f.key() == "date") continue;
                settings[f.key()] = f.value();
            }
        }
        world.sidecar["gd5"]["meta"] = settings;
        world.sidecar["gd5"]["history"] = readJsonFile(joinPath(dir, "history.json"), report);

        /* The hand-painted terrain layer is the one thing here that cannot be
         * regenerated from the model without loss: two provinces of the same
         * terrain are indistinguishable in the palette, but the layer may hold
         * shading the palette does not name. It is carried whole. */
        std::vector<uint8_t> terrainBytes;
        if (readFile(joinPath(dir, "terrain.png"), terrainBytes)) {
            world.sidecar_blobs["gd5/terrain.png"] = terrainBytes;
        }
        readSidecarFrom(dir, world);
        restoreUnrepresentable(world, report);
    }

    report.info("gd5.read", "read " + std::to_string(world.provinces.size()) + " provinces and "
                                + std::to_string(world.nations.size()) + " nations from " + dir);
    return true;
}

/* ------------------------------------------------------------------ write */

bool writeGd5Map(const std::string& dir, const World& world, const Options& opt, Report& report) {
    if (!makeDirectories(dir)) {
        setLastError("could not create " + dir);
        return false;
    }

    /* GD5 keys everything by display name, and two nations sharing one would
     * silently merge. Duplicates are separated rather than left to collide. */
    std::map<std::string, std::string> keyToName;
    std::set<std::string> usedNames;
    for (const auto& n : world.nations) {
        std::string name = n.name.empty() ? n.key : n.name;
        if (usedNames.count(name)) {
            int suffix = 2;
            std::string candidate;
            do {
                candidate = name + " (" + std::to_string(suffix++) + ")";
            } while (usedNames.count(candidate));
            report.warn("gd5.namecollision",
                        "two nations are both called \"" + name + "\"; the second is written as \""
                            + candidate + "\" because GD5 identifies a nation by its name");
            name = candidate;
        }
        usedNames.insert(name);
        keyToName[n.key] = name;
    }

    auto nameOf = [&](const std::string& key) -> std::string {
        const auto it = keyToName.find(key);
        return it != keyToName.end() ? it->second : std::string();
    };

    /* ---- geometry GD5 needs and Open Doctrines never stored ---- */
    std::map<uint32_t, std::set<uint32_t>> adjacency;
    std::map<uint32_t, std::pair<int, int>> centers;
    bool derived = false;
    bool needsGeometry = false;
    for (const auto& p : world.provinces) {
        if (!p.has_neighbors || !p.has_center) { needsGeometry = true; break; }
    }
    if (needsGeometry && opt.derive_geometry && !world.raster.empty()) {
        adjacency = computeAdjacency(world.raster, world.width, world.height, /*wrap_x=*/true);
        centers = computeCenters(world.raster, world.width, world.height);
        derived = true;
        report.info("gd5.derived",
                    "computed province adjacency and centres from the province raster, because "
                    "Open Doctrines derives both at load rather than storing them");
    } else if (needsGeometry && !opt.derive_geometry) {
        report.warn("gd5.nogeometry",
                    "province neighbours and centres are missing and derivation is switched off; "
                    "GD5 will not be able to move units on this map");
    }

    std::set<uint32_t> seaIds;
    for (const auto& p : world.provinces) {
        if (p.is_sea) seaIds.insert(static_cast<uint32_t>(p.id));
    }

    /* The two games disagree about what the sea is. GD5 divides it into
     * provinces, gives them to a nation called Ocean and sails fleets between
     * them. Open Doctrines does not put provinces in the water at all -- its
     * 1914 map has 1247 of them and every one is land -- and moves ships by
     * latitude and longitude over a land/sea mask instead. A map crossing this
     * way therefore arrives with no water to sail on, and no amount of
     * carrying can invent it, because the source never drew those borders. */
    if (seaIds.empty() && !world.provinces.empty()) {
        report.warn("gd5.nosea",
                    "this map has no sea provinces, because Open Doctrines does not divide water "
                    "into any. GD5 will load it, but no fleet can move: to sail it, paint sea "
                    "provinces in GD5's map editor. Ship positions are preserved in the sidecar "
                    "and return intact.");
    }
    const std::set<uint32_t> coastal = computeCoastal(
        derived ? adjacency : computeAdjacency(world.raster, world.width, world.height, true),
        seaIds);

    /* ---- map_data.json and the scenario overlay ---- */
    Json mapData = Json::object();
    Json overlay = Json::object();
    std::map<uint32_t, uint32_t> ownerColor;
    std::map<uint32_t, uint32_t> coreColor;
    std::vector<uint32_t> terrainOfPixel;

    for (const auto& p : world.provinces) {
        const std::string key = provinceKey(p.id);
        Json j = Json::object();
        j["id"] = p.id;
        if (!p.name.empty()) j["name"] = p.name;

        std::string terrain = p.terrain;
        if (terrain.empty()) {
            terrain = defaultTerrainFor(p.is_sea, coastal.count(static_cast<uint32_t>(p.id)) != 0);
        }
        j["terrain"] = terrain;

        const bool isCoastal = p.has_center || p.has_neighbors
                                   ? p.is_coastal
                                   : coastal.count(static_cast<uint32_t>(p.id)) != 0;
        j["is_coastal"] = isCoastal;

        if (p.has_center) {
            j["center"] = Json::array({p.center_x, p.center_y});
        } else {
            const auto c = centers.find(static_cast<uint32_t>(p.id));
            j["center"] = c != centers.end() ? Json::array({c->second.first, c->second.second})
                                             : Json::array({0, 0});
        }

        if (p.has_neighbors) {
            j["neighbors"] = p.neighbors;
        } else {
            Json ns = Json::array();
            const auto a = adjacency.find(static_cast<uint32_t>(p.id));
            if (a != adjacency.end()) {
                for (uint32_t n : a->second) ns.push_back(n);
            }
            j["neighbors"] = ns;
        }

        /* An unowned land province is "Unclaimed" and unowned water is
         * "Ocean"; GD5 treats an empty string as a nation that does not exist
         * and drops the province out of every query that walks owners. */
        /* An owner the roster does not know keeps its own name; only a
         * genuinely ownerless province becomes Ocean or Unclaimed. */
        const std::string owner = nameOf(p.owner).empty() ? p.owner : nameOf(p.owner);
        j["owner"] = !owner.empty() ? owner : (p.is_sea ? "Ocean" : "Unclaimed");

        Json cores = Json::array();
        for (const auto& c : p.cores) {
            /* A claimant with no roster entry keeps the name it had. GD5 reads
             * cores as bare strings and does not require the nation to exist,
             * which is how its own scenarios core provinces for countries that
             * have already been annexed. */
            const std::string cn = nameOf(c);
            cores.push_back(cn.empty() ? c : cn);
        }
        j["cores"] = cores;

        Json units = Json::array();
        const auto gar = p.extra.find("garrison");
        if (gar != p.extra.end() && gar->is_array()) {
            for (const auto& g : *gar) {
                /* As with cores, an owner that is not on the roster keeps the
                 * name it arrived with rather than being blanked. */
                const std::string held = g.value("owner", std::string());
                const std::string heldName = nameOf(held).empty() ? held : nameOf(held);
                if (g.contains("gd5") && g["gd5"].is_object()) {
                    Json u = g["gd5"];
                    u["owner"] = heldName;
                    units.push_back(u);
                    continue;
                }
                /* An Open Doctrines garrison is a headcount and nothing else,
                 * so the unit it becomes is named for the map's own year --
                 * "Infantry Type 1914" on a 1914 map, which is the naming GD5
                 * uses for every unit of its own. */
                const double health = g.value("health", 0.0);
                const std::string unitType = "Infantry Type " + std::to_string(world.date.year);
                units.push_back(Json{
                    {"type", unitType},
                    {"owner", heldName},
                    {"health", health},
                    {"max_health", static_cast<int64_t>(std::llround(health))},
                    {"speed", 1},
                    {"attack", 100},
                    {"defense", 0},
                    {"level", 0},
                    {"order", Json{{"type", "MOVE"}, {"path", Json::array()}}},
                    {"custom_name", ""},
                    {"naval_unit", g.value("naval", false)}});
            }
        }

        Json buildings = Json::array();
        const auto bld = p.extra.find("gd5_buildings");
        if (bld != p.extra.end() && bld->is_array()) {
            buildings = *bld;
        } else {
            for (int i = 0; i < p.industry; ++i) buildings.push_back("Basic Factory");
        }

        /* GD5 names resources its own way -- "Oil", "Tungsten" -- while Open
         * Doctrines has five fixed lowercase deposits with a surface and a
         * reserve figure each. Where the original GD5 table was carried it is
         * written back verbatim, because mapping it through Open Doctrines'
         * five and back would rename Oil to oil and lose everything with no
         * counterpart at all. */
        Json resources = Json::object();
        const auto carriedRes = p.extra.find("gd5");
        if (carriedRes != p.extra.end() && carriedRes->is_object()
            && carriedRes->contains("resources") && (*carriedRes)["resources"].is_object()) {
            resources = (*carriedRes)["resources"];
        } else {
            for (const auto& kv : p.resources) {
                if (kv.second.a != 0.0) resources[kv.first] = kv.second.a;
            }
        }

        j["units"] = units;
        j["building_queue"] = Json::array();
        j["unit_queue"] = Json::array();
        j["orders"] = Json::array();
        j["buildings"] = buildings;
        j["resources"] = resources;
        j["json_key"] = key;
        j["map_color"] = Json::array({int(p.id & 0xff), int((p.id >> 8) & 0xff),
                                      int((p.id >> 16) & 0xff)});

        const auto extra = p.extra.find("gd5");
        if (extra != p.extra.end() && extra->is_object()) {
            for (auto f = extra->begin(); f != extra->end(); ++f) {
                if (!j.contains(f.key())) j[f.key()] = f.value();
            }
        }

        mapData[key] = j;

        /* The overlay repeats exactly the fields GD5's loader reads from it. */
        overlay[key] = Json{{"owner", j["owner"]},
                            {"cores", cores},
                            {"is_coastal", isCoastal},
                            {"units", units},
                            {"building_queue", Json::array()},
                            {"unit_queue", Json::array()},
                            {"orders", Json::array()},
                            {"resources", resources},
                            {"buildings", buildings}};

        /* GD5 does not draw water into the political layer; it paints it the
         * chroma key its renderer then sets as the surface's colorkey, so the
         * terrain underneath shows through. A nation whose colour happens to
         * be exactly that key is nudged one step off it, which is the same
         * guard map_utils.avoid_chroma() applies on GD5's own side -- without
         * it, that one country would be punched out as ocean. */
        const Nation* n = p.is_sea ? nullptr : world.findNation(p.owner);
        if (n) {
            ownerColor[static_cast<uint32_t>(p.id)] =
                n->color == kChromaKey ? kChromaNudged : n->color;
        }
        if (!p.cores.empty()) {
            const Nation* cn = world.findNation(p.cores.front());
            if (cn) {
                coreColor[static_cast<uint32_t>(p.id)] =
                    cn->color == kChromaKey ? kChromaNudged : cn->color;
            }
        }
    }

    /* ---- meta.json ---- */
    Json meta = Json::object();
    const auto carriedMeta = world.sidecar.find("gd5");
    if (carriedMeta != world.sidecar.end() && carriedMeta->contains("meta")
        && (*carriedMeta)["meta"].is_object()) {
        meta = (*carriedMeta)["meta"];
    }
    meta["date"] = Json{{"day", world.date.day},
                        {"month", world.date.month - 1},  /* back to GD5's zero-based month */
                        {"year", world.date.year},
                        {"total_turns", world.date.turn}};
    if (!meta.contains("loop_map")) meta["loop_map"] = true;
    if (!meta.contains("player_country")) meta["player_country"] = "None";
    if (!meta.contains("active_players")) meta["active_players"] = Json::array();
    if (!meta.contains("current_player_index")) meta["current_player_index"] = 0;
    if (!meta.contains("scenario_settings")) {
        meta["scenario_settings"] = Json{{"fog_of_war", true},
                                         {"casus_belli_required", true},
                                         {"days_per_turn", "Default"},
                                         {"use_scripted_events", !world.events.empty()},
                                         {"ai_disabled", false}};
    } else if (!world.events.empty()) {
        meta["scenario_settings"]["use_scripted_events"] = true;
    }
    if (!meta.contains("default_research")) meta["default_research"] = nullptr;

    Json nationData = Json::object();
    for (const auto& n : world.nations) {
        const std::string name = nameOf(n.key);
        Json nd = Json::object();
        const auto extra = n.extra.find("gd5");
        if (extra != n.extra.end() && extra->is_object()) nd = *extra;
        /* The roster key is the identity; the "name" field is what the player
         * sees, and only defaults to the key when the map never had one. */
        if (!nd.contains("name")) nd["name"] = name;
        nd["adjective"] = n.adjective;
        nd["color"] = Json::array({int((n.color >> 16) & 0xff), int((n.color >> 8) & 0xff),
                                   int(n.color & 0xff)});
        nd["leader_name"] = n.leader_name;
        nd["leader_title"] = n.leader_title;
        nd["is_playable"] = n.playable;
        if (!nd.contains("manpower")) nd["manpower"] = 0;
        nd["materials"] = n.treasury;
        if (!nd.contains("fuel")) nd["fuel"] = 0;
        if (!nd.contains("pending_diplomacy")) nd["pending_diplomacy"] = Json::object();
        if (!nd.contains("relations")) nd["relations"] = Json::object();
        if (!nd.contains("portrait_data")) nd["portrait_data"] = "DEFAULT";

        nd["flag_data"] = n.flag_bytes.empty() ? Json("DEFAULT")
                                               : Json(base64Encode(n.flag_bytes));

        Json atWar = Json::array(), allied = Json::array();
        for (const auto& kv : n.relations) {
            const std::string other = nameOf(kv.first);
            if (other.empty()) continue;
            if (kv.second.at_war) atWar.push_back(other);
            if (kv.second.ally) allied.push_back(other);
        }
        nd["at_war_with"] = atWar;
        nd["allied_with"] = allied;
        nd["claims"] = n.claims;

        /* Open Doctrines' non-aggression pacts and guarantees have no GD5
         * equivalent; saying so once per map is more use than once per pair. */
        nd["scripted_events"] = Json::array();
        nationData[name] = nd;
    }

    if (opt.translate_scripts && !world.events.empty()) {
        eventsToGd5(world.events, keyToName, nationData, report);
    }

    bool droppedPacts = false;
    for (const auto& n : world.nations) {
        for (const auto& kv : n.relations) {
            if (kv.second.non_aggression || kv.second.guarantee) droppedPacts = true;
        }
    }
    if (droppedPacts) {
        report.warn("gd5.relations",
                    "non-aggression pacts and guarantees have no GD5 counterpart and were not "
                    "written into the map; they are preserved in the sidecar and return intact");
    }

    meta["nation_data"] = nationData;
    meta["provinces"] = overlay;

    /* ---- the layers ---- */
    const Image idImg = rasterToGd5(world.raster, world.width, world.height);
    writeFile(joinPath(dir, "id_map.png"), encodePng(idImg));
    writeFile(joinPath(dir, "political.png"),
              encodePng(politicalImage(world.raster, world.width, world.height, ownerColor,
                                       kChromaKey)));
    writeFile(joinPath(dir, "cores.png"),
              encodePng(politicalImage(world.raster, world.width, world.height, coreColor,
                                       kChromaKey)));

    const auto carriedTerrain = world.sidecar_blobs.find("gd5/terrain.png");
    if (carriedTerrain != world.sidecar_blobs.end() && !opt.reencode_images) {
        writeFile(joinPath(dir, "terrain.png"), carriedTerrain->second);
    } else {
        std::map<uint32_t, uint32_t> terrainColorOf;
        for (const auto& p : world.provinces) {
            std::string t = p.terrain;
            if (t.empty()) {
                t = defaultTerrainFor(p.is_sea, coastal.count(static_cast<uint32_t>(p.id)) != 0);
            }
            terrainColorOf[static_cast<uint32_t>(p.id)] = terrainColor(t);
        }
        writeFile(joinPath(dir, "terrain.png"),
                  encodePng(politicalImage(world.raster, world.width, world.height, terrainColorOf,
                                           terrainColor("ocean"))));
    }

    writeFile(joinPath(dir, "map_data.json"), mapData.dump());
    writeFile(joinPath(dir, "meta.json"), meta.dump());

    Json history = Json::object();
    if (carriedMeta != world.sidecar.end() && carriedMeta->contains("history")
        && !(*carriedMeta)["history"].is_null()) {
        history = (*carriedMeta)["history"];
    }
    writeFile(joinPath(dir, "history.json"), history.dump());

    if (opt.carry_sidecar) writeSidecarInto(dir, world);

    report.info("gd5.write", "wrote " + std::to_string(world.provinces.size()) + " provinces to "
                                 + dir);
    if (opt.strict && report.hasWarnings()) {
        setLastError("strict mode: the translation produced warnings");
        return false;
    }
    return true;
}

}  // namespace dragoman
