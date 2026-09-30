/* Model lookups, nation keys, dates, format detection, and the sidecar. */
#include "Formats.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>

#include <dragoman/dragoman.h>

#include "ModelJson.h"

/* Bumped only when the sidecar's own layout changes, which is not the same
 * event as a library release: a 0.1.x and a 0.3.x Dragoman should still be
 * able to read each other's carried data. */
#define DRAGOMAN_SIDECAR_VERSION 1

namespace dragoman {

namespace fs = std::filesystem;

#include "IsoTable.inc"

/* ------------------------------------------------------------ model lookup */

const Province* World::findProvince(int64_t id) const {
    for (const auto& p : provinces) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

Province* World::findProvince(int64_t id) {
    return const_cast<Province*>(static_cast<const World*>(this)->findProvince(id));
}

const Nation* World::findNation(const std::string& key) const {
    if (key.empty()) return nullptr;
    for (const auto& n : nations) {
        if (n.key == key) return &n;
    }
    return nullptr;
}

Nation* World::findNation(const std::string& key) {
    return const_cast<Nation*>(static_cast<const World*>(this)->findNation(key));
}

/* ------------------------------------------------------------ nation keys */

std::string isoForName(const std::string& display_name) {
    for (const auto& row : kIsoTable) {
        if (display_name == row.name) return row.iso;
    }
    /* A second pass ignoring case and punctuation, so "United States of
     * America" still finds itself when a map maker typed it differently. */
    auto normalise = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            if (asciiAlnum(c)) o.push_back(asciiLower(c));
        }
        return o;
    };
    const std::string want = normalise(display_name);
    for (const auto& row : kIsoTable) {
        if (normalise(row.name) == want) return row.iso;
    }
    return std::string();
}

std::string synthesiseIso(const std::string& display_name, const std::vector<std::string>& taken) {
    /* Initials first -- "Vichy France" becomes VIF rather than VIC -- because
     * a code a human can read back to a nation is worth more here than one
     * that is merely unique. Falls back to the first three letters. */
    std::string initials, letters;
    bool atWordStart = true;
    for (unsigned char c : display_name) {
        if (asciiAlpha(c)) {
            const char up = asciiUpper(c);
            letters.push_back(up);
            if (atWordStart) initials.push_back(up);
            atWordStart = false;
        } else {
            atWordStart = true;
        }
    }

    std::string base;
    if (initials.size() >= 3) base = initials.substr(0, 3);
    else if (letters.size() >= 3) base = letters.substr(0, 3);
    else if (!letters.empty()) base = letters + std::string(3 - letters.size(), 'X');
    else base = "XXX";

    const auto isTaken = [&taken](const std::string& c) {
        return std::find(taken.begin(), taken.end(), c) != taken.end();
    };
    if (!isTaken(base)) return base;

    for (int i = 2; i < 1000; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2s%d", base.c_str(), i);
        if (!isTaken(buf)) return buf;
    }
    return base;
}

/* ------------------------------------------------------------------ dates */

namespace {
const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                         "July",    "August",   "September", "October", "November", "December"};
}

bool parseOdDate(const std::string& text, Date& out) {
    /* "July 1914 AD", "January 2000", "Modern Day". The last is a label the
     * game accepts and no calendar can hold, so it is reported as unparsed
     * and the caller keeps the string in the sidecar. */
    const std::vector<std::string> parts = splitOn(trim(text), ' ');
    if (parts.size() < 2) return false;

    int month = 0;
    for (int i = 0; i < 12; ++i) {
        if (parts[0] == kMonths[i]) { month = i + 1; break; }
    }
    if (month == 0) return false;

    const long year = std::strtol(parts[1].c_str(), nullptr, 10);
    if (year == 0) return false;

    out.month = month;
    out.year = static_cast<int>(year);
    out.day = 1;
    out.ad = !(parts.size() >= 3 && toUpper(parts[2]) == "BC");
    return true;
}

std::string formatOdDate(const Date& d) {
    const int m = (d.month >= 1 && d.month <= 12) ? d.month : 1;
    return std::string(kMonths[m - 1]) + " " + std::to_string(d.year) + (d.ad ? " AD" : " BC");
}

/* ------------------------------------------------------- format detection */

int detectFormat(const std::string& path) {
    if (isDirectory(path)) {
        /* A GD5 map is the directory its loader can open: an id raster and the
         * province table beside it. */
        if (fileExists(joinPath(path, "id_map.png")) && fileExists(joinPath(path, "map_data.json"))) {
            return 2;
        }
        return 0;
    }
    /* Unciv keeps a map as one JSON file, so this has to look inside rather
     * than trust an extension: a .json is not distinctive and neither is the
     * absence of one. A tileList of objects carrying a position is. */
    if (fileExists(path) && !looksLikeZip(path)) {
        std::vector<uint8_t> bytes;
        if (readFile(path, bytes) && bytes.size() > 2 && bytes.size() < 64u * 1024 * 1024) {
            const std::string head(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 4096));
            if (head.find("tileList") != std::string::npos
                && head.find("mapParameters") != std::string::npos) {
                return 3;
            }
        }
    }
    if (!fileExists(path) || !looksLikeZip(path)) return 0;

    Zip zip;
    std::string err;
    if (!readZip(path, zip, err)) return 0;
    if (zip.has("provinces.png") && (zip.has("provinces.json") || zip.has("countries.json"))) {
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- sidecar */

const char* kSidecarDir = "dragoman_sidecar";
const char* kSidecarMember = "dragoman/";

namespace {

/* The model as written, minus the sidecar itself -- otherwise each crossing
 * would carry the previous crossing's copy of the one before it. */
Json modelSnapshot(const World& world) {
    Json m = worldToJson(world);
    m.erase("sidecar");
    m.erase("sidecar_blobs");
    return m;
}

Json sidecarHeader(const World& world) {
    Json h = Json::object();
    h["format"] = "dragoman-sidecar";
    h["version"] = DRAGOMAN_SIDECAR_VERSION;
    h["library"] = DRAGOMAN_VERSION_STRING;
    h["origin"] = world.origin == 1 ? "odmap" : world.origin == 2 ? "gd5" : "unknown";
    return h;
}

/* Blob keys carry a directory ("od/land_sea.png"), and both containers are
 * happy to hold that as a path, so no escaping is needed either side. */
std::string blobPath(const std::string& key) { return "blobs/" + key; }

Json buildSidecarDoc(const World& world) {
    Json doc = Json::object();
    doc["dragoman"] = sidecarHeader(world);
    doc["data"] = world.sidecar;
    doc["model"] = modelSnapshot(world);

    /* The nation codes invented on the way in, so the way back reuses them
     * rather than inventing different ones. */
    Json keys = Json::object();
    for (const auto& n : world.nations) keys[n.name] = n.key;
    doc["nation_keys"] = keys;

    /* The scripts as their author wrote them. A script only partly expressible
     * as events still comes home in one piece. */
    Json scripts = Json::array();
    for (const auto& s : world.scripts) {
        scripts.push_back(Json{{"name", s.name}, {"text", s.text}, {"entrypoint", s.entrypoint}});
    }
    doc["scripts"] = scripts;
    return doc;
}

}  // namespace

void writeSidecarInto(Zip& zip, const World& world,
                     const std::map<std::string, std::vector<uint8_t>>& extra_blobs) {
    const Json doc = buildSidecarDoc(world);
    zip.putText(std::string(kSidecarMember) + "sidecar.json", doc.dump(1, ' '));
    for (const auto& kv : world.sidecar_blobs) {
        zip.put(std::string(kSidecarMember) + blobPath(kv.first), kv.second);
    }
    /* Written last so a freshly computed blob replaces the one carried in from
     * the previous crossing rather than the other way round. */
    for (const auto& kv : extra_blobs) {
        zip.put(std::string(kSidecarMember) + blobPath(kv.first), kv.second);
    }
}

const char* kSyntheticOceanKey = "synthetic_ocean";

bool waterIsProvinced(const std::vector<uint32_t>& raster,
                      const std::vector<Province>& provinces) {
    std::set<uint32_t> sea;
    for (const auto& p : provinces) {
        if (p.is_sea) sea.insert(static_cast<uint32_t>(p.id));
    }
    long seaPixels = 0, blankPixels = 0;
    for (uint32_t id : raster) {
        if (id == 0) ++blankPixels;
        else if (sea.count(id)) ++seaPixels;
    }
    if (blankPixels == 0) return true;   /* nothing unpainted: nothing to decide */
    return seaPixels * 10 >= blankPixels;
}

void writeSidecarInto(const std::string& dir, const World& world, const Json& extra_data) {
    Json doc = buildSidecarDoc(world);
    /* Merged into the carried data rather than kept beside it, so that reading
     * the sidecar back puts it exactly where the reader looks for it. */
    for (auto it = extra_data.begin(); it != extra_data.end(); ++it) {
        for (auto f = it.value().begin(); f != it.value().end(); ++f) {
            doc["data"][it.key()][f.key()] = f.value();
        }
    }
    const std::string root = joinPath(dir, kSidecarDir);
    makeDirectories(root);
    writeFile(joinPath(root, "sidecar.json"), doc.dump(1, ' '));
    for (const auto& kv : world.sidecar_blobs) {
        writeFile(joinPath(root, blobPath(kv.first)), kv.second);
    }
}

namespace {

void applySidecar(const Json& doc, World& world) {
    if (!doc.is_object()) return;
    if (doc.contains("model") && doc["model"].is_object()) world.prior_model = doc["model"];
    if (doc.contains("data") && doc["data"].is_object()) {
        /* Merged rather than replaced: the reader has already filled in what
         * this side of the trip knows, and the sidecar holds what the other
         * side knew. Neither should win outright. */
        for (auto it = doc["data"].begin(); it != doc["data"].end(); ++it) {
            if (world.sidecar.contains(it.key()) && world.sidecar[it.key()].is_object()
                && it.value().is_object()) {
                for (auto f = it.value().begin(); f != it.value().end(); ++f) {
                    if (!world.sidecar[it.key()].contains(f.key())) {
                        world.sidecar[it.key()][f.key()] = f.value();
                    }
                }
            } else if (!world.sidecar.contains(it.key())) {
                world.sidecar[it.key()] = it.value();
            }
        }
    }

    /* Nation codes chosen on an earlier crossing win over anything invented
     * now, which is what keeps a code stable across repeated conversions. */
    if (doc.contains("nation_keys") && doc["nation_keys"].is_object()) {
        for (auto& n : world.nations) {
            const auto it = doc["nation_keys"].find(n.name);
            if (it != doc["nation_keys"].end() && it->is_string()) n.key = it->get<std::string>();
        }
    }

    if (doc.contains("scripts") && doc["scripts"].is_array() && world.scripts.empty()) {
        for (const auto& s : doc["scripts"]) {
            ScriptSource src;
            src.name = s.value("name", std::string());
            src.text = s.value("text", std::string());
            src.entrypoint = s.value("entrypoint", false);
            if (!src.name.empty()) world.scripts.push_back(std::move(src));
        }
    }
}

}  // namespace

/* Which facts each format simply has no room for.
 *
 * This is the honest heart of the library. Open Doctrines models a province's
 * population, its port, its ethnic minorities and its position on a political
 * compass; GD5 has no field for any of them. GD5 models a province's terrain,
 * its neighbours, its garrison's research level and its faction; Open
 * Doctrines has no field for those. Neither game is missing anything it wants
 * -- they are different games -- but a map crossing between them would be
 * quietly poorer each way if nothing wrote the difference down.
 *
 * Only these fields are restored. Anything both games can express is left
 * exactly as the game that last held the map left it, so editing a map in GD5
 * and converting it back applies the edit rather than reverting it.
 */
void restoreUnrepresentable(World& world, Report& report) {
    if (!world.prior_model.is_object() || world.prior_model.empty()) return;

    World prior;
    Report quiet;
    if (!worldFromJson(world.prior_model, prior, quiet)) return;

    const bool readGd5 = world.origin == 2;
    int restored = 0;

    if (readGd5) {
        /* A GD5 map directory is named, not titled: there is nowhere in the
         * format to put a map's name, description, author or licence. */
        if (!prior.name.empty()) world.name = prior.name;
        if (!prior.description.empty()) world.description = prior.description;
        if (!prior.author.empty()) world.author = prior.author;
        if (!prior.license.empty()) world.license = prior.license;
        world.date.ad = prior.date.ad;
    } else {
        /* Open Doctrines dates a map to the month -- "July 1914 AD" -- and
         * steps one month a turn, so a GD5 map's day of the month and its
         * turn counter have nowhere to go and come back as the 1st of the
         * month, turn zero, unless they are put back from here. */
        world.date.day = prior.date.day;
        world.date.turn = prior.date.turn;
    }

    for (auto& p : world.provinces) {
        const Province* was = prior.findProvince(p.id);
        if (!was) continue;
        if (readGd5) {
            p.population = was->population;
            p.port_level = was->port_level;
            /* Fortification is the one field here the destination game can
             * now change for itself, so the map wins and the record only
             * fills a gap. GD5 gained forts as a building after this sidecar
             * was designed; before that, a fort could only ever come back
             * from the record, and taking the record unconditionally was
             * right. It is not any more -- a player who builds a fort in GD5
             * and translates home would have watched it disappear.
             *
             * Zero is treated as "the map said nothing" rather than "the
             * player demolished it": GD5 has no way to express a razed fort
             * distinctly from never having had one, and silently discarding a
             * level the map still remembers is the worse of the two mistakes.
             * Levels do not survive exactly in both directions -- see the
             * scaling note in Gd5Map.cpp -- so this is the field's own value,
             * not the one it started with. */
            if (p.fortification <= 0) p.fortification = was->fortification;
            p.resources = was->resources;
            if (p.name.empty()) p.name = was->name;
            for (auto it = was->extra.begin(); it != was->extra.end(); ++it) {
                if (startsWith(it.key(), "od")) p.extra[it.key()] = it.value();
            }
        } else {
            p.terrain = was->terrain;
            p.is_coastal = was->is_coastal;
            /* Open Doctrines has no per-province notion of water at all, so
             * this can only come back from the record. */
            p.is_sea = was->is_sea;
            /* Open Doctrines has claims but no cores, so a province's core
             * list has nowhere to live in a .odmap and comes back from here. */
            p.cores = was->cores;
            if (!p.has_center && was->has_center) {
                p.center_x = was->center_x;
                p.center_y = was->center_y;
                p.has_center = true;
            }
            if (!p.has_neighbors && was->has_neighbors) {
                p.neighbors = was->neighbors;
                p.has_neighbors = true;
            }
            for (auto it = was->extra.begin(); it != was->extra.end(); ++it) {
                if (startsWith(it.key(), "gd5")) p.extra[it.key()] = it.value();
            }

            /* A garrison survives the crossing as a headcount, because that is
             * all Open Doctrines stores: armies.json is {country_id, count}
             * and everything else about the division -- its type, its attack
             * and defence, its custom name, the order it was carrying -- has
             * no field. The detail is put back per unit, and only where the
             * headcount still matches, so an army edited in Open Doctrines
             * keeps the edit instead of being overwritten by its own past. */
            const auto now = p.extra.find("garrison");
            const auto then = was->extra.find("garrison");
            if (now != p.extra.end() && then != was->extra.end() && now->is_array()
                && then->is_array()) {
                for (size_t i = 0; i < now->size() && i < then->size(); ++i) {
                    const Json& before = (*then)[i];
                    Json& after = (*now)[i];
                    if (before.value("count", int64_t(-1)) != after.value("count", int64_t(-2))) {
                        continue;
                    }
                    /* The whole entry, not only its GD5 detail: the owner is
                     * lost too when the division belongs to a nation that is
                     * no longer on the roster, since armies.json can only name
                     * a country by an id the country table has. */
                    after = before;
                }
            }
        }
        ++restored;
    }

    for (auto& n : world.nations) {
        const Nation* was = prior.findNation(n.key);
        if (!was) continue;
        if (readGd5) {
            /* GD5 knows war and alliance and nothing in between, so a
             * non-aggression pact or a guarantee has no field to survive in. */
            for (const auto& kv : was->relations) {
                Relation& r = n.relations[kv.first];
                r.non_aggression = kv.second.non_aggression;
                r.guarantee = kv.second.guarantee;
                r.truce = kv.second.truce;
            }
            /* GD5 inlines a flag as base64 and never records what the file
             * was called, so the name has to come from here or every crossing
             * renames flags/GER_EMPIRE.png to flags/GER.png. */
            if (!was->flag_name.empty()) n.flag_name = was->flag_name;
            /* The flag GD5 handed back is 60x40 raw pixels, because that is
             * the only shape it stores one in. The original image is carried
             * beside the map, so where it is still there it wins -- otherwise
             * every crossing would permanently shrink a nation's flag to
             * GD5's icon size. */
            const auto original = world.sidecar_blobs.find("od/" + n.flag_name);
            if (original != world.sidecar_blobs.end() && !original->second.empty()) {
                n.flag_bytes = original->second;
            }
            const auto od = was->extra.find("od");
            if (od != was->extra.end()) n.extra["od"] = *od;
        } else {
            if (n.adjective.empty()) n.adjective = was->adjective;
            if (n.leader_name.empty()) n.leader_name = was->leader_name;
            if (n.leader_title.empty()) n.leader_title = was->leader_title;
            n.playable = was->playable;
            const auto gd5 = was->extra.find("gd5");
            if (gd5 != was->extra.end()) n.extra["gd5"] = *gd5;
        }
    }

    /* Scripts and events are the same intent in two shapes. Whichever shape
     * the format just read cannot hold is taken back from the record. */
    if (readGd5 && world.scripts.empty() && !prior.scripts.empty()) {
        world.scripts = prior.scripts;
    } else if (!readGd5 && world.events.empty() && !prior.events.empty()) {
        world.events = prior.events;
    }

    if (restored > 0) {
        report.info("sidecar.restored",
                    "restored fields for " + std::to_string(restored)
                        + " province(s) that the " + (readGd5 ? "GD5" : "Open Doctrines")
                        + " format has no place to store");
    }
}

std::map<std::string, std::string> readSidecarNationKeys(const std::string& dir) {
    std::map<std::string, std::string> keys;
    std::vector<uint8_t> bytes;
    if (!readFile(joinPath(joinPath(dir, kSidecarDir), "sidecar.json"), bytes)) return keys;
    try {
        const Json doc = Json::parse(bytes.begin(), bytes.end());
        if (doc.contains("nation_keys") && doc["nation_keys"].is_object()) {
            for (auto it = doc["nation_keys"].begin(); it != doc["nation_keys"].end(); ++it) {
                if (it.value().is_string()) keys[it.key()] = it.value().get<std::string>();
            }
        }
    } catch (const std::exception&) {
        keys.clear();
    }
    return keys;
}

void readSidecarFrom(const Zip& zip, World& world) {
    const ZipEntry* e = zip.find(std::string(kSidecarMember) + "sidecar.json");
    if (!e) return;
    try {
        applySidecar(Json::parse(e->data.begin(), e->data.end()), world);
    } catch (const std::exception&) {
        return;
    }
    const std::string prefix = std::string(kSidecarMember) + "blobs/";
    for (const auto& entry : zip.entries) {
        if (entry.is_dir || !startsWith(entry.name, prefix)) continue;
        world.sidecar_blobs[entry.name.substr(prefix.size())] = entry.data;
    }
}

void readSidecarFrom(const std::string& dir, World& world) {
    const std::string root = joinPath(dir, kSidecarDir);
    std::vector<uint8_t> bytes;
    if (!readFile(joinPath(root, "sidecar.json"), bytes)) return;
    try {
        applySidecar(Json::parse(bytes.begin(), bytes.end()), world);
    } catch (const std::exception&) {
        return;
    }

    const fs::path blobRoot = fs::path(root) / "blobs";
    std::error_code ec;
    if (!fs::is_directory(blobRoot, ec)) return;
    for (auto it = fs::recursive_directory_iterator(blobRoot, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        std::vector<uint8_t> data;
        if (!readFile(it->path().string(), data)) continue;
        world.sidecar_blobs[fs::relative(it->path(), blobRoot, ec).generic_string()] = data;
    }
}

}  // namespace dragoman
