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

/* data/constants.py: FLAG_SIZE. GD5 does not store a flag as an image file --
 * `flag_data` is base64 of *raw pixel bytes* at exactly this size, handed
 * straight to pygame.image.fromstring, RGBA when the payload is w*h*4 bytes
 * and RGB when it is w*h*3. Give it base64 of a PNG instead and fromstring
 * raises, decode_b64_to_surf swallows the exception, and the nation gets a
 * blank white rectangle -- which is exactly what every converted map showed
 * until this was worked out. */
constexpr int kFlagWidth = 60;
constexpr int kFlagHeight = 40;

/* An Open Doctrines flag PNG, in whatever size it happens to be, as the raw
 * RGBA bytes GD5 expects. */
std::string encodeFlagForGd5(const std::vector<uint8_t>& png) {
    Image img;
    if (!decodePng(png, img) || img.empty()) return std::string();
    const Image scaled = (img.width == kFlagWidth && img.height == kFlagHeight)
                             ? img
                             : resizeImage(img, kFlagWidth, kFlagHeight);
    return base64Encode(scaled.rgba);
}

/* And back: raw pixels at 60x40 into a PNG Open Doctrines can put in its
 * archive. */
std::vector<uint8_t> decodeFlagFromGd5(const std::string& b64) {
    const std::vector<uint8_t> raw = base64Decode(b64);
    const size_t rgba = static_cast<size_t>(kFlagWidth) * kFlagHeight * 4;
    const size_t rgb = static_cast<size_t>(kFlagWidth) * kFlagHeight * 3;

    Image img;
    img.width = kFlagWidth;
    img.height = kFlagHeight;
    img.channels = 4;
    if (raw.size() == rgba) {
        img.rgba = raw;
    } else if (raw.size() == rgb) {
        img.rgba.resize(rgba);
        for (size_t p = 0; p < static_cast<size_t>(kFlagWidth) * kFlagHeight; ++p) {
            img.rgba[p * 4 + 0] = raw[p * 3 + 0];
            img.rgba[p * 4 + 1] = raw[p * 3 + 1];
            img.rgba[p * 4 + 2] = raw[p * 3 + 2];
            img.rgba[p * 4 + 3] = 255;
        }
    } else {
        /* Some other size, or a payload this library does not recognise. It is
         * carried verbatim in `extra` either way, so nothing is lost by
         * declining to guess at it here. */
        return std::vector<uint8_t>();
    }
    return encodePng(img);
}

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

/* Research, from GD5's own tech tree and GD5's own rule.
 *
 * Open Doctrines does not put research in a map at all. Its tree is built in
 * C++ in Game_Research.cpp and each country's starting nodes are handed out by
 * a hardcoded list of ISO codes -- tier one gets fort1-3 and ind1-5, everyone
 * else gets ind1 and basic_training. So there is nothing in a .odmap to read,
 * and a converted map arrived in GD5 with every nation at level zero in
 * everything: no infantry, no factories, a stone age 1914.
 *
 * What both formats do carry is the date, and GD5 already knows what to do
 * with one. queries.get_time_appropriate_research(year) walks its tech tree
 * and gives each tech a level equal to the number of its introduction years
 * that have passed. That rule is applied here, against the tech tree read out
 * of the GD5 installation being written into -- not a copy kept in this
 * library, which would go stale the moment anyone modded a tech or shifted a
 * year, and is not ours to carry about anyway.
 */
std::string findTechTreePath(const std::string& map_dir) {
    /* A map is written into <gd5>/base_maps/<name> or
     * <gd5>/scenarios/<kind>/<name>, so the install is a few levels up. */
    fs::path here = fs::absolute(map_dir);
    for (int up = 0; up < 5 && !here.empty(); ++up) {
        const fs::path candidate = here / "data" / "json" / "research_template.json";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return std::string();
}

Json timeAppropriateResearch(const Json& tree, int year) {
    /* queries.get_time_appropriate_research, followed exactly. The 9999 /
     * 1800 pair is its sentinel for a tech with no ceiling; infantry_type and
     * cavalry are pinned back to zero before the timeline is counted. */
    Json out = Json::object();
    for (auto it = tree.begin(); it != tree.end(); ++it) {
        const Json& tech = it.value();
        if (!tech.is_object()) continue;
        out[it.key()] = tech.value("max_lvl", 0) == 9999 ? 1800 : 0;
    }
    if (out.contains("infantry_type")) out["infantry_type"] = 0;
    if (out.contains("cavalry")) out["cavalry"] = 0;

    for (auto it = tree.begin(); it != tree.end(); ++it) {
        const Json& tech = it.value();
        if (!tech.is_object() || !tech.contains("years") || !tech["years"].is_array()) continue;
        /* Infantry counts the year itself, everything else counts strictly
         * before it -- which is GD5's distinction, not one invented here. */
        const bool infantry = tech.value("category", std::string()) == "INFANTRY";
        int level = 0;
        for (const auto& y : tech["years"]) {
            if (!y.is_number()) continue;
            const int introduced = y.get<int>();
            if (infantry ? (introduced <= year) : (introduced < year)) ++level;
        }
        if (level > 0) out[it.key()] = level;
    }
    return out;
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

/* Give GD5 an ocean to draw and to sail on.
 *
 * Open Doctrines does not divide water into provinces -- every water pixel is
 * simply unpainted -- so a map crossing to GD5 arrives with nothing in the
 * sea: its renderer paints unpainted pixels black, and no fleet can move,
 * because movement is province to province.
 *
 * The water is grown into provinces rather than cut into them. Seeds are laid
 * on a lattice, nudged off it by a hash of their own coordinates, snapped to
 * the nearest water, and then all grown outwards at once through water only.
 * Each province is therefore the water nearest to one seed, which gives blobs
 * that follow the coast instead of squares laid over it -- GD5's own sea
 * provinces run about 0.66 on bounding-box fill, and a grid runs 1.00.
 *
 * Growing through water also settles a question a grid could only approximate:
 * a province cannot cross land, so the Mediterranean and the Atlantic are
 * necessarily separate provinces however close the seeds fall, and no fleet
 * can step over an isthmus. Water no seed reached -- a lake smaller than the
 * lattice -- becomes a province per connected piece afterwards.
 *
 * These provinces are an invention, not a translation, so their ids are
 * recorded in the sidecar and the crossing back deletes them again -- an Open
 * Doctrines map that has been to GD5 and returned has exactly the provinces it
 * started with.
 */
struct SynthesisedOcean {
    std::vector<int64_t> ids;
    long                 pixels = 0;
};

SynthesisedOcean synthesiseOcean(std::vector<uint32_t>& ids, int w, int h,
                                 std::vector<Province>& provinces, int spacing) {
    SynthesisedOcean made;
    if (w <= 0 || h <= 0 || spacing <= 0) return made;

    int64_t nextId = 1;
    for (const auto& p : provinces) nextId = std::max(nextId, p.id + 1);

    const size_t n = ids.size();
    auto water = [&ids](size_t i) { return ids[i] == 0; };
    auto index = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };

    /* Seeds on a lattice, displaced by a hash of the cell they belong to. An
     * exact lattice produces exactly the grid this is trying not to be, and
     * anything genuinely random would make the same map convert differently
     * twice. */
    std::vector<size_t> frontier;
    std::vector<int64_t> seedId;
    for (int cy = 0; cy < h; cy += spacing) {
        for (int cx = 0; cx < w; cx += spacing) {
            const uint32_t hash = static_cast<uint32_t>(cx) * 73856093u
                                  ^ static_cast<uint32_t>(cy) * 19349663u;
            int sx = cx + static_cast<int>(hash % static_cast<uint32_t>(spacing));
            int sy = cy + static_cast<int>((hash / 65521u) % static_cast<uint32_t>(spacing));
            if (sx >= w) sx = w - 1;
            if (sy >= h) sy = h - 1;

            /* Snapped to the nearest water in the cell, so a seed that lands
             * inland still plants the sea beside it rather than being lost. */
            size_t best = n;
            long bestDistance = 0;
            const int x1 = std::min(cx + spacing, w), y1 = std::min(cy + spacing, h);
            for (int y = cy; y < y1; ++y) {
                for (int x = cx; x < x1; ++x) {
                    const size_t i = index(x, y);
                    if (!water(i) || ids[i] != 0) continue;
                    const long dx = x - sx, dy = y - sy;
                    const long d = dx * dx + dy * dy;
                    if (best == n || d < bestDistance) { best = i; bestDistance = d; }
                }
            }
            if (best == n) continue;

            const int64_t id = nextId++;
            ids[best] = static_cast<uint32_t>(id);
            frontier.push_back(best);
            seedId.push_back(id);
        }
    }

    /* All seeds advance together, so every pixel joins the province whose seed
     * it is nearest to through water -- not the one that happened to reach it
     * first. */
    std::vector<size_t> next;
    while (!frontier.empty()) {
        next.clear();
        for (size_t i : frontier) {
            const int x = static_cast<int>(i % static_cast<size_t>(w));
            const int y = static_cast<int>(i / static_cast<size_t>(w));
            const uint32_t id = ids[i];
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d];
                const int ny = y + dy[d];
                if (ny < 0 || ny >= h) continue;
                if (nx < 0 || nx >= w) nx = (nx + w) % w;   /* the map is a cylinder */
                const size_t j = index(nx, ny);
                if (ids[j] != 0) continue;
                ids[j] = id;
                next.push_back(j);
            }
        }
        frontier.swap(next);
    }

    /* Whatever the seeds never reached: a pond smaller than the lattice, or a
     * sea enclosed away from every seed. One province per connected piece --
     * but only for pieces big enough to be worth one.
     *
     * The minimum matters more than it looks. A world raster at this size is
     * speckled with one- and two-pixel scraps of water in river mouths and
     * between islands, and giving each of them a province turned 750 sea
     * provinces into 1362, six hundred of which no fleet could ever enter.
     * Below the threshold the pixels are left unpainted, which is what they
     * already were, and is invisible at any zoom the game draws. */
    const long minimumArea = 64;
    std::vector<size_t> stack, piece;
    for (size_t start = 0; start < n; ++start) {
        if (ids[start] != 0) continue;
        const int64_t id = nextId++;
        piece.clear();
        stack.assign(1, start);
        ids[start] = static_cast<uint32_t>(id);
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            piece.push_back(i);
            const int x = static_cast<int>(i % static_cast<size_t>(w));
            const int y = static_cast<int>(i / static_cast<size_t>(w));
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d];
                const int ny = y + dy[d];
                if (ny < 0 || ny >= h) continue;
                if (nx < 0 || nx >= w) nx = (nx + w) % w;
                const size_t j = index(nx, ny);
                if (ids[j] != 0) continue;
                ids[j] = static_cast<uint32_t>(id);
                stack.push_back(j);
            }
        }
        if (static_cast<long>(piece.size()) >= minimumArea) {
            seedId.push_back(id);
        } else {
            for (size_t i : piece) ids[i] = 0;  /* left as it was found */
            --nextId;
        }
    }

    std::map<int64_t, long> area;
    for (uint32_t id : ids) {
        if (id >= static_cast<uint32_t>(seedId.front())) ++area[static_cast<int64_t>(id)];
    }
    for (int64_t id : seedId) {
        if (area[id] == 0) continue;
        Province prov;
        prov.id = id;
        prov.is_sea = true;
        prov.terrain = "ocean";  /* refined to coastal_sea once adjacency is known */
        provinces.push_back(prov);
        made.ids.push_back(id);
        made.pixels += area[id];
    }
    return made;
}

}  // namespace

/* GD5's tech tree, read from the installation a map sits in. Shared with the
 * Open Doctrines writer, which needs the same ceilings to turn a research node
 * back into a level. */
Json readTechTree(const std::string& gd5_map_dir) {
    const std::string path = findTechTreePath(gd5_map_dir);
    if (path.empty()) return Json::object();
    Report quiet;
    const Json tree = readJsonFile(path, quiet);
    return tree.is_object() ? tree : Json::object();
}

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
                n.flag_bytes = decodeFlagFromGd5(flag);
                if (!n.flag_bytes.empty()) n.flag_name = "flags/" + n.key + ".png";
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
        /* Carried so the Open Doctrines writer can turn levelled technologies
         * into research nodes: it needs each tech's ceiling, and by then the
         * GD5 installation is no longer in reach. */
        const Json tree = readTechTree(dir);
        if (!tree.empty()) world.sidecar["gd5"]["tech_tree"] = tree;

        /* The hand-painted terrain layer is the one thing here that cannot be
         * regenerated from the model without loss: two provinces of the same
         * terrain are indistinguishable in the palette, but the layer may hold
         * shading the palette does not name. It is carried whole. */
        std::vector<uint8_t> terrainBytes;
        if (readFile(joinPath(dir, "terrain.png"), terrainBytes)) {
            world.sidecar_blobs["gd5/terrain.png"] = terrainBytes;
        }
        readSidecarFrom(dir, world);

        /* The sea provinces this library invented on the way out are taken
         * out again here, along with the Ocean nation that owns them and the
         * pixels they were painted into, so that a map which went to GD5 and
         * came back has exactly the provinces it started with. Only the ids
         * recorded in the sidecar are touched: a sea province the map maker
         * has since drawn in GD5's own editor is a real one and stays. */
        const auto gd5Side = world.sidecar.find("gd5");
        if (gd5Side != world.sidecar.end() && gd5Side->contains(kSyntheticOceanKey)) {
            const Json& record = (*gd5Side)[kSyntheticOceanKey];
            std::set<int64_t> invented;
            if (record.contains("provinces") && record["provinces"].is_array()) {
                for (const auto& id : record["provinces"]) {
                    if (id.is_number_integer()) invented.insert(id.get<int64_t>());
                }
            }
            if (!invented.empty()) {
                const size_t before = world.provinces.size();
                world.provinces.erase(
                    std::remove_if(world.provinces.begin(), world.provinces.end(),
                                   [&invented](const Province& p) { return invented.count(p.id) != 0; }),
                    world.provinces.end());
                for (auto& id : world.raster) {
                    if (invented.count(static_cast<int64_t>(id))) id = 0;
                }
                const std::string oceanName = record.value("nation", std::string("Ocean"));
                world.nations.erase(
                    std::remove_if(world.nations.begin(), world.nations.end(),
                                   [&oceanName](const Nation& n) { return n.name == oceanName; }),
                    world.nations.end());
                report.info("gd5.ocean",
                            "removed the " + std::to_string(before - world.provinces.size())
                                + " sea provinces this library invented when the map crossed to "
                                  "GD5; Open Doctrines leaves its water unprovinced");
            }
        }

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

    /* ---- the sea, where the source game did not draw one ---- */
    std::vector<Province> localProvinces;
    std::vector<uint32_t> localRaster;
    SynthesisedOcean ocean;

    /* Only a map that does not already draw its water gets an ocean invented
     * for it -- see waterIsProvinced() for why this is a count rather than a
     * check that some province is marked sea. Checking merely that one exists
     * said "already provinced" for every Open Doctrines map, because a handful
     * of their coastal provinces sit mostly under the mask, and no ocean was
     * ever drawn. */
    if (opt.synthesise_ocean && !world.provinces.empty()
        && !waterIsProvinced(world.raster, world.provinces)) {
        localProvinces = world.provinces;
        localRaster = world.raster;
        /* Seed spacing, which sets how big a sea province comes out. GD5's own
         * world map gives each of its 501 sea provinces about a tenth of a
         * percent of the map; on an 8192x4096 raster that is a lattice step of
         * roughly this. */
        ocean = synthesiseOcean(localRaster, world.width, world.height, localProvinces,
                                /*spacing=*/192);
    }
    const bool synthesised = !ocean.ids.empty();
    const std::vector<Province>& provinces = synthesised ? localProvinces : world.provinces;
    const std::vector<uint32_t>& raster = synthesised ? localRaster : world.raster;

    /* ---- geometry GD5 needs and Open Doctrines never stored ---- */
    std::map<uint32_t, std::set<uint32_t>> adjacency;
    std::map<uint32_t, std::pair<int, int>> centers;
    bool derived = false;
    bool needsGeometry = false;
    for (const auto& p : provinces) {
        if (!p.has_neighbors || !p.has_center) { needsGeometry = true; break; }
    }
    if (needsGeometry && opt.derive_geometry && !raster.empty()) {
        adjacency = computeAdjacency(raster, world.width, world.height, /*wrap_x=*/true);
        centers = computeCenters(raster, world.width, world.height);
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
    for (const auto& p : provinces) {
        if (p.is_sea) seaIds.insert(static_cast<uint32_t>(p.id));
    }

    const std::map<uint32_t, std::set<uint32_t>> fullAdjacency =
        derived ? adjacency : computeAdjacency(raster, world.width, world.height, true);
    const std::set<uint32_t> coastal = computeCoastal(fullAdjacency, seaIds);

    /* The two games disagree about what the sea is. GD5 divides it into
     * provinces, gives them to a nation called Ocean and sails fleets between
     * them; Open Doctrines does not put provinces in the water at all and
     * moves ships by latitude and longitude over a land/sea mask instead. */
    if (synthesised) {
        /* A sea province that touches land is coastal water, which is both the
         * terrain GD5 shades differently and the one its landing rules read. */
        std::set<uint32_t> land;
        for (const auto& p : provinces) {
            if (!p.is_sea) land.insert(static_cast<uint32_t>(p.id));
        }
        for (auto& p : localProvinces) {
            if (!p.is_sea) continue;
            const auto neighbours = fullAdjacency.find(static_cast<uint32_t>(p.id));
            if (neighbours == fullAdjacency.end()) continue;
            for (uint32_t other : neighbours->second) {
                if (land.count(other)) { p.terrain = "coastal_sea"; break; }
            }
        }
        report.info("gd5.ocean",
                    "divided the water into " + std::to_string(ocean.ids.size())
                        + " sea provinces covering " + std::to_string(ocean.pixels)
                        + " pixels, because Open Doctrines leaves its oceans unpainted and GD5 "
                          "cannot draw or sail across what is not a province. They are recorded "
                          "in the sidecar and removed again on the way back.");
    } else if (seaIds.empty() && !provinces.empty()) {
        report.warn("gd5.nosea",
                    "this map has no sea provinces, because Open Doctrines does not divide water "
                    "into any, and ocean synthesis is switched off. GD5 will load it, but no fleet "
                    "can move and the sea will render black. Ship positions are preserved in the "
                    "sidecar and return intact.");
    }

    /* ---- map_data.json and the scenario overlay ---- */
    Json mapData = Json::object();
    Json overlay = Json::object();
    std::map<uint32_t, uint32_t> ownerColor;
    std::map<uint32_t, uint32_t> coreColor;
    std::vector<uint32_t> terrainOfPixel;

    for (const auto& p : provinces) {
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

        if (n.flag_bytes.empty()) {
            if (!nd.contains("flag_data")) nd["flag_data"] = "DEFAULT";
        } else {
            const std::string encoded = encodeFlagForGd5(n.flag_bytes);
            nd["flag_data"] = encoded.empty() ? Json("DEFAULT") : Json(encoded);
            if (encoded.empty()) {
                report.warn("gd5.flag",
                            "the flag for " + n.name
                                + " could not be decoded, so GD5 will draw its default one");
            }
        }

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

    /* ---- research ----
     *
     * Only for nations that arrive without any: one that came from GD5 keeps
     * exactly what it had, carried through the sidecar. A map from Open
     * Doctrines has none for anybody, and gets the levels its own date implies.
     */
    long unresearched = 0;
    for (auto it = nationData.begin(); it != nationData.end(); ++it) {
        if (!it.value().contains("research") || !it.value()["research"].is_object()
            || it.value()["research"].empty()) {
            ++unresearched;
        }
    }
    if (unresearched > 0) {
        const std::string treePath = findTechTreePath(dir);
        if (treePath.empty()) {
            report.warn("gd5.research",
                        std::to_string(unresearched)
                            + " nation(s) have no research, and GD5's tech tree was not found "
                              "beside the destination (data/json/research_template.json). They "
                              "will start at level zero in everything. Convert into a GD5 "
                              "installation, or set research in GD5's own research editor.");
        } else {
            Report quiet;
            const Json tree = readJsonFile(treePath, quiet);
            if (!tree.is_object() || tree.empty()) {
                report.warn("gd5.research", "GD5's tech tree at " + treePath + " could not be read");
            } else {
                const Json levels = timeAppropriateResearch(tree, world.date.year);
                long fromNodes = 0;
                for (auto it = nationData.begin(); it != nationData.end(); ++it) {
                    if (it.value().contains("research") && it.value()["research"].is_object()
                        && !it.value()["research"].empty()) {
                        continue;
                    }
                    /* A map that carries Open Doctrines research says more
                     * about this nation than its date does, so it wins. */
                    const Nation* n = nullptr;
                    for (const auto& candidate : world.nations) {
                        if (nameOf(candidate.key) == it.key()) { n = &candidate; break; }
                    }
                    std::vector<std::string> nodes;
                    if (n) {
                        const auto od = n->extra.find("od");
                        if (od != n->extra.end() && od->contains("research")
                            && (*od)["research"].is_array()) {
                            for (const auto& id : (*od)["research"]) {
                                if (id.is_string()) nodes.push_back(id.get<std::string>());
                            }
                        }
                    }
                    if (!nodes.empty()) {
                        const Json translated = researchGd5FromNodes(nodes, tree);
                        if (!translated.empty()) {
                            Json merged = levels;
                            for (auto t = translated.begin(); t != translated.end(); ++t) {
                                merged[t.key()] = t.value();
                            }
                            it.value()["research"] = merged;
                            ++fromNodes;
                            continue;
                        }
                    }
                    it.value()["research"] = levels;
                }
                if (fromNodes > 0) {
                    report.info("gd5.research",
                                "translated the research of " + std::to_string(fromNodes)
                                    + " nation(s) from the nodes carried in the map, rather than "
                                      "from its date");
                }
                if (!meta.contains("default_research") || meta["default_research"].is_null()) {
                    meta["default_research"] = levels;
                }
                report.info("gd5.research",
                            "gave " + std::to_string(unresearched) + " nation(s) the research "
                            "level GD5's own tech tree puts at " + std::to_string(world.date.year)
                            + ", across " + std::to_string(tree.size())
                            + " technologies. Open Doctrines does not store research in a map -- "
                              "its tree is compiled into the game and seeded by ISO code.");
            }
        }
    }

    /* The sea provinces invented above are owned by "Ocean", which every GD5
     * map has as a nation_data entry and an Open Doctrines map never does. It
     * is added here rather than to the model, because it is part of the same
     * invention: the sidecar records it and the crossing back removes it. */
    if (synthesised && !nationData.contains("Ocean")) {
        nationData["Ocean"] = Json{{"name", "Ocean"},
                                   {"adjective", ""},
                                   {"color", Json::array({10, 20, 40})},
                                   {"leader_name", ""},
                                   {"leader_title", ""},
                                   {"is_playable", false},
                                   {"manpower", 0},
                                   {"materials", 0},
                                   {"fuel", 0},
                                   {"at_war_with", Json::array()},
                                   {"allied_with", Json::array()},
                                   {"claims", Json::array()},
                                   {"relations", Json::object()},
                                   {"pending_diplomacy", Json::object()},
                                   {"flag_data", "DEFAULT"},
                                   {"portrait_data", "DEFAULT"},
                                   {"scripted_events", Json::array()}};
    }

    meta["nation_data"] = nationData;
    meta["provinces"] = overlay;

    /* ---- the layers ---- */
    const Image idImg = rasterToGd5(raster, world.width, world.height);
    writeFile(joinPath(dir, "id_map.png"), encodePng(idImg));
    writeFile(joinPath(dir, "political.png"),
              encodePng(politicalImage(raster, world.width, world.height, ownerColor,
                                       kChromaKey)));
    writeFile(joinPath(dir, "cores.png"),
              encodePng(politicalImage(raster, world.width, world.height, coreColor,
                                       kChromaKey)));

    const auto carriedTerrain = world.sidecar_blobs.find("gd5/terrain.png");
    if (carriedTerrain != world.sidecar_blobs.end() && !opt.reencode_images) {
        writeFile(joinPath(dir, "terrain.png"), carriedTerrain->second);
    } else {
        std::map<uint32_t, uint32_t> terrainColorOf;
        for (const auto& p : provinces) {
            std::string t = p.terrain;
            if (t.empty()) {
                t = defaultTerrainFor(p.is_sea, coastal.count(static_cast<uint32_t>(p.id)) != 0);
            }
            terrainColorOf[static_cast<uint32_t>(p.id)] = terrainColor(t);
        }
        writeFile(joinPath(dir, "terrain.png"),
                  encodePng(politicalImage(raster, world.width, world.height, terrainColorOf,
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

    if (opt.carry_sidecar) {
        Json extra = Json::object();
        if (synthesised) {
            extra["gd5"][kSyntheticOceanKey] = Json{{"provinces", ocean.ids},
                                                    {"nation", "Ocean"}};
        }
        writeSidecarInto(dir, world, extra);
    }

    report.info("gd5.write", "wrote " + std::to_string(provinces.size()) + " provinces to "
                                 + dir);
    if (opt.strict && report.hasWarnings()) {
        setLastError("strict mode: the translation produced warnings");
        return false;
    }
    return true;
}

}  // namespace dragoman
