/* Open Doctrines `.odmap`: one zip holding a province raster and a dozen JSON
 * objects, every one of them keyed by province id or by ISO code.
 *
 * The shape is wide rather than deep -- population.json is id -> number,
 * resources.json is id -> object, ports.json is id -> object -- so reading is
 * mostly a matter of walking the province list once and pulling the matching
 * entry out of each. Writing puts them back the same way, and the files the
 * game derives at load rather than reads (political.png above all) are not
 * written at all, which is the same decision Open Doctrines' own packer made.
 */
#include "Formats.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dragoman {

namespace {

/* Men per point of GD5 unit health. Open Doctrines counts an army in people
 * -- 1174826 of them in one province of the 1914 map -- and GD5 counts a
 * division in hit points, 1200 for the infantry of the same year. The ratio
 * is what makes an army that crosses look like an army rather than an
 * absurdity; the exact original figure rides in the sidecar, so nothing is
 * actually decided by this constant on a round trip. */
constexpr double kMenPerHealth = 1000.0;

Json parseMember(const Zip& zip, const std::string& name, Report& report) {
    const ZipEntry* e = zip.find(name);
    if (!e || e->data.empty()) return Json();
    try {
        return Json::parse(e->data.begin(), e->data.end());
    } catch (const std::exception& ex) {
        report.warn("od.badjson", name + " is not valid JSON and was skipped: " + ex.what());
        return Json();
    }
}

double numberOr(const Json& j, const char* key, double fallback) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return fallback;
    return it->get<double>();
}

std::string stringOr(const Json& j, const char* key, const std::string& fallback) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

std::string idKey(int64_t id) { return std::to_string(id); }

}  // namespace

/* ------------------------------------------------------------------- read */

bool readOdMap(const std::string& path, const Options& opt, World& world, Report& report) {
    Zip zip;
    std::string err;
    if (!readZip(path, zip, err)) {
        setLastError(err);
        return false;
    }

    world.origin = 1;  /* DG_FORMAT_ODMAP */

    /* ---- metadata ---- */
    const Json meta = parseMember(zip, "metadata.json", report);
    world.name = stringOr(meta, "name", "Untitled");
    world.description = stringOr(meta, "description", "");
    world.author = stringOr(meta, "author", "");
    world.license = stringOr(meta, "license", "");
    if (!parseOdDate(stringOr(meta, "map_date", ""), world.date)) {
        report.warn("od.date", "metadata.json has no readable map_date; defaulting to January 1 AD");
    }

    /* ---- the province raster ---- */
    const ZipEntry* provPng = zip.find("provinces.png");
    if (!provPng) {
        setLastError("this .odmap has no provinces.png, so it holds no map");
        return false;
    }
    Image provImg;
    if (!decodePng(provPng->data, provImg)) {
        setLastError("provinces.png could not be decoded");
        return false;
    }
    world.width = provImg.width;
    world.height = provImg.height;
    world.raster = rasterFromOd(provImg);

    /* ---- nations ---- */
    const Json countries = parseMember(zip, "countries.json", report);
    const Json relations = parseMember(zip, "relations.json", report);
    const Json claims = parseMember(zip, "claims.json", report);
    const Json countryCompass = parseMember(zip, "country_compass.json", report);
    const Json startingPolicies = parseMember(zip, "starting_policies.json", report);

    std::map<int64_t, std::string> countryIdToIso;
    if (countries.is_object()) {
        for (auto it = countries.begin(); it != countries.end(); ++it) {
            const Json& c = it.value();
            Nation n;
            n.key = stringOr(c, "iso_a3", it.key());
            n.name = stringOr(c, "name", n.key);
            n.treasury = numberOr(c, "treasury", 0.0);
            parseHexColor(stringOr(c, "color", "#808080"), n.color);

            const auto idIt = c.find("id");
            if (idIt != c.end() && idIt->is_number_integer()) {
                countryIdToIso[idIt->get<int64_t>()] = n.key;
            }

            /* The flag is a path into the archive; the bytes come with it so
             * that a destination which inlines images (GD5 base64s them) does
             * not have to go back to the file. */
            const auto flagIt = c.find("flag_actual");
            if (flagIt != c.end() && flagIt->is_object()) {
                n.flag_name = stringOr(*flagIt, "image", "");
                if (const ZipEntry* fe = zip.find(n.flag_name)) n.flag_bytes = fe->data;
            }

            /* The numeric id is kept rather than reassigned. Open Doctrines
             * hands out ids that are not a dense sequence -- Unclaimed is
             * 65534 on every shipped map -- and renumbering them rewrites
             * every province's country_id and every ship's owner into a map
             * that is subtly not the one that was read. */
            Json extra = Json::object();
            for (auto f = c.begin(); f != c.end(); ++f) {
                static const char* handled[] = {"iso_a3", "name", "color",
                                                "treasury", "flag_actual"};
                bool known = false;
                for (const char* k : handled) known = known || f.key() == k;
                if (!known) extra[f.key()] = f.value();
            }
            if (countryCompass.is_object() && countryCompass.contains(n.key)) {
                extra["country_compass"] = countryCompass[n.key];
            }
            if (startingPolicies.is_object()) {
                const auto sp = startingPolicies.find("starting_policies");
                if (sp != startingPolicies.end() && sp->is_object() && sp->contains(n.key)) {
                    extra["starting_policies"] = (*sp)[n.key];
                }
            }
            if (!extra.empty()) n.extra["od"] = extra;

            world.nations.push_back(std::move(n));
        }
    }

    if (relations.is_object()) {
        for (auto a = relations.begin(); a != relations.end(); ++a) {
            Nation* n = world.findNation(a.key());
            if (!n) continue;
            if (!a.value().is_object()) continue;
            for (auto b = a.value().begin(); b != a.value().end(); ++b) {
                Relation r;
                r.ally = b.value().value("ally", false);
                r.non_aggression = b.value().value("nonAggression", false);
                r.guarantee = b.value().value("guarantee", false);
                r.truce = b.value().value("truce", false);
                r.at_war = b.value().value("atWar", false);
                n->relations[b.key()] = r;
            }
        }
    }

    /* Claims whose claimant is not a country on this map are kept anyway.
     * GD5 scenarios routinely claim territory for a nation that has already
     * been annexed out of existence -- Kaiserreich 1936 does it a dozen times
     * -- and dropping those because no roster entry matched quietly erased a
     * war aim. They are set aside and written back out unchanged. */
    Json orphanClaims = Json::object();
    if (claims.is_object()) {
        for (auto it = claims.begin(); it != claims.end(); ++it) {
            if (!it.value().is_array()) continue;
            Nation* n = world.findNation(it.key());
            for (const auto& v : it.value()) {
                if (!v.is_number_integer()) continue;
                if (n) n->claims.push_back(v.get<int64_t>());
                else orphanClaims[it.key()].push_back(v.get<int64_t>());
            }
        }
    }
    if (!orphanClaims.empty()) world.sidecar["od"]["orphan_claims"] = orphanClaims;

    /* ---- provinces ---- */
    const Json provinces = parseMember(zip, "provinces.json", report);
    const Json population = parseMember(zip, "population.json", report);
    const Json resources = parseMember(zip, "resources.json", report);
    const Json ports = parseMember(zip, "ports.json", report);
    const Json armies = parseMember(zip, "armies.json", report);
    const Json compass = parseMember(zip, "political_compass.json", report);
    const Json minorities = parseMember(zip, "minorities.json", report);

    /* No province read from a .odmap is sea.
     *
     * Open Doctrines has no such thing: water is the absence of a province,
     * not a kind of one, and land_sea.png answers per pixel rather than per
     * province. Deciding it from that layer by majority looked reasonable and
     * was wrong -- it caught fifteen provinces on the world map whose pixels
     * happen to fall under the mask, among them the Faeroes and the Isle of
     * Man, which are islands the layer does not bother to draw as land. They
     * crossed to GD5 as ocean tiles owned by Norway and by the Isle of Man.
     *
     * A .odmap that came from GD5 does have sea provinces, and they come back
     * from the terrain the sidecar carried, below -- which is the field that
     * actually means it. */

    if (provinces.is_object()) {
        for (auto it = provinces.begin(); it != provinces.end(); ++it) {
            const Json& p = it.value();
            Province prov;
            prov.id = static_cast<int64_t>(numberOr(p, "id", std::atof(it.key().c_str())));
            prov.name = stringOr(p, "name", "");
            prov.owner = stringOr(p, "iso_a3", "");

            const std::string key = idKey(prov.id);

            if (population.is_object() && population.contains(key)
                && population[key].is_number()) {
                prov.population = population[key].get<int64_t>();
            }

            if (resources.is_object() && resources.contains(key)) {
                const Json& r = resources[key];
                for (const char* name : {"oil", "gold", "rubber", "gemstones", "metal"}) {
                    if (r.contains(name) && r[name].is_object()) {
                        Resource res;
                        res.a = numberOr(r[name], "a", 0.0);
                        res.b = numberOr(r[name], "b", 0.0);
                        if (res.a != 0.0 || res.b != 0.0) prov.resources[name] = res;
                    }
                }
                prov.fortification = static_cast<int>(numberOr(r, "fortification", 0.0));
                if (r.contains("industry") && r["industry"].is_object()) {
                    prov.industry = static_cast<int>(numberOr(r["industry"], "level", 0.0));
                    prov.extra["od_industry"] = r["industry"];
                }
            }

            if (ports.is_object() && ports.contains(key)) {
                prov.port_level = static_cast<int>(numberOr(ports[key], "level", 0.0));
            }

            /* Open Doctrines' claims are held on the nation; GD5 holds the
             * same fact on the province, as `cores`. Both shapes are filled
             * so neither writer has to go looking. */
            if (compass.is_object() && compass.contains(key)) {
                prov.extra["od_compass"] = compass[key];
            }
            if (minorities.is_object() && minorities.contains(key)) {
                prov.extra["od_minorities"] = minorities[key];
            }
            if (armies.is_object() && armies.contains(key) && armies[key].is_array()) {
                Json garrison = Json::array();
                for (const auto& a : armies[key]) {
                    Json entry = Json::object();
                    const int64_t cid = a.value("country_id", int64_t(0));
                    const auto isoIt = countryIdToIso.find(cid);
                    entry["owner"] = isoIt != countryIdToIso.end() ? isoIt->second : std::string();
                    entry["count"] = a.value("count", int64_t(0));
                    entry["health"] = a.value("count", int64_t(0)) / kMenPerHealth;
                    garrison.push_back(entry);
                }
                prov.extra["garrison"] = garrison;
            }

            world.provinces.push_back(std::move(prov));
        }
    }

    /* Both collections are put in a canonical order here, and by the GD5
     * reader too. Neither format promises one -- Open Doctrines keys these by
     * id in a JSON object and GD5 by a colour tuple -- so without this the
     * same map read through the two readers produces two orderings, and
     * anything comparing a map with itself after a crossing compares province
     * 113 against province 640. */
    std::sort(world.provinces.begin(), world.provinces.end(),
              [](const Province& a, const Province& b) { return a.id < b.id; });
    std::sort(world.nations.begin(), world.nations.end(),
              [](const Nation& a, const Nation& b) { return a.key < b.key; });

    /* A province's cores are deliberately not filled in from these claims.
     * GD5 keeps both -- a nation's `claims` and a province's `cores` -- and
     * they mean different things: what a country wants, and what it considers
     * already its own. Open Doctrines has only the first. Treating them as one
     * fact invented cores no one had declared and, worse, threw away claims
     * that happened not to be cores. Cores come back from the sidecar. */

    /* Ships are positioned by latitude and longitude rather than by province;
     * resolving them here means the GD5 writer, which knows only provinces,
     * does not have to know the projection. */
    const Json ships = parseMember(zip, "ships.json", report);
    if (ships.is_array()) {
        Json fleets = Json::array();
        for (const auto& s : ships) {
            int x = 0, y = 0;
            lonLatToPixel(s.value("lon", 0.0), s.value("lat", 0.0), world.width, world.height, x, y);
            Json entry = s;
            const int64_t cid = s.value("country_id", int64_t(0));
            const auto isoIt = countryIdToIso.find(cid);
            entry["owner"] = isoIt != countryIdToIso.end() ? isoIt->second : std::string();
            entry["province"] = provinceAt(world.raster, world.width, world.height, x, y);
            fleets.push_back(entry);
        }
        world.sidecar["od"]["ships"] = fleets;
    }

    /* ---- scripts ---- */
    for (const auto& e : zip.entries) {
        if (e.is_dir || !startsWith(e.name, "scripts/")) continue;
        ScriptSource s;
        s.name = e.name.substr(8);
        s.text = std::string(e.data.begin(), e.data.end());
        s.entrypoint = s.text.find("#OD/MapEngine/") != std::string::npos;
        world.scripts.push_back(std::move(s));
    }

    /* ---- everything the model has no field for ---- */
    if (opt.carry_sidecar) {
        static const char* modelled[] = {
            "metadata.json", "provinces.json", "countries.json", "population.json",
            "resources.json", "ports.json", "armies.json", "ships.json",
            "relations.json", "claims.json", "provinces.png", "land_sea.png",
            "political.png", "political_compass.json", "country_compass.json",
            "minorities.json"};
        Json carried = Json::object();
        for (const auto& e : zip.entries) {
            if (e.is_dir || startsWith(e.name, "scripts/")) continue;
            /* Our own sidecar from an earlier crossing is read separately,
             * below; carrying it as an opaque blob as well would nest one
             * inside the next on every round trip. */
            if (startsWith(e.name, kSidecarMember)) continue;
            bool known = false;
            for (const char* m : modelled) known = known || e.name == m;
            if (known) continue;
            /* JSON is carried as JSON so it stays diffable; anything else is a
             * blob. Flags already travel on their nation, but they are carried
             * here too -- a flag no country references still belongs to the map. */
            if (endsWith(e.name, ".json")) {
                try {
                    carried[e.name] = Json::parse(e.data.begin(), e.data.end());
                    continue;
                } catch (const std::exception&) { /* fall through to blob */ }
            }
            world.sidecar_blobs["od/" + e.name] = e.data;
        }
        world.sidecar["od"]["files"] = carried;
        world.sidecar["od"]["zip_order"] = Json::array();
        for (const auto& e : zip.entries) {
            world.sidecar["od"]["zip_order"].push_back(
                Json{{"name", e.name}, {"dir", e.is_dir}, {"mtime", e.mtime}});
        }
        readSidecarFrom(zip, world);
        restoreUnrepresentable(world, report);

        /* Terrain also says whether a province is water, and a map that
         * arrived from GD5 has one; a world that only ever set the flag has
         * had it restored just above. Either is enough. */
        for (auto& p : world.provinces) {
            if (!p.is_sea) p.is_sea = isSeaTerrain(p.terrain);
        }

        /* The borders that were filled in so Open Doctrines would not read
         * them as water are unpainted again here, so that the model holds the
         * raster the source game actually had and a crossing back to GD5
         * reproduces it pixel for pixel. */
        const auto gaps = world.sidecar_blobs.find("gaps/id_gaps.png");
        if (gaps != world.sidecar_blobs.end()) {
            Image mask;
            if (decodePng(gaps->second, mask) && mask.width == world.width
                && mask.height == world.height) {
                long restored = 0;
                for (size_t i = 0; i < world.raster.size(); ++i) {
                    if (mask.rgba[i * 4] > 128) {
                        world.raster[i] = 0;
                        ++restored;
                    }
                }
                report.info("od.unfilled",
                            "unpainted " + std::to_string(restored)
                                + " pixels that were filled in to make this map readable as an "
                                  "Open Doctrines landmass");
            }
        }
    }

    report.info("od.read", "read " + std::to_string(world.provinces.size()) + " provinces and "
                               + std::to_string(world.nations.size()) + " nations from " + path);
    return true;
}

/* ------------------------------------------------------------------ write */

bool writeOdMap(const std::string& path, const World& world, const Options& opt, Report& report) {
    Zip zip;

    /* A nation needs a numeric id in this format and a name in the other, and
     * only one of the two games stores both. Ids are assigned in nation order
     * so that a map written twice is written the same way. */
    std::map<std::string, int64_t> isoToId;
    std::set<int64_t> usedIds;
    for (const auto& n : world.nations) {
        const auto od = n.extra.find("od");
        if (od == n.extra.end() || !od->is_object()) continue;
        const auto id = od->find("id");
        if (id == od->end() || !id->is_number_integer()) continue;
        const int64_t v = id->get<int64_t>();
        if (usedIds.insert(v).second) isoToId[n.key] = v;
    }
    int64_t nextId = 1;
    for (const auto& n : world.nations) {
        if (isoToId.count(n.key)) continue;
        while (usedIds.count(nextId)) ++nextId;
        usedIds.insert(nextId);
        isoToId[n.key] = nextId;
    }

    Json countries = Json::object();
    Json relations = Json::object();
    Json claims = Json::object();
    Json countryCompass = Json::object();
    Json startingPolicies = Json::object();

    for (const auto& n : world.nations) {
        Json c = Json::object();
        const int64_t id = isoToId[n.key];
        c["id"] = id;
        c["iso_a3"] = n.key;
        c["name"] = n.name;
        c["color"] = formatHexColor(n.color);

        std::string flagName = n.flag_name;
        if (flagName.empty() && !n.flag_bytes.empty()) flagName = "flags/" + n.key + ".png";
        if (!flagName.empty()) {
            c["flag_actual"] = Json{{"image", flagName}};
            c["flag_censored"] = Json{{"image", flagName}};
            /* Only when the archive is not already carrying the original. A
             * flag that has been to GD5 and back is 60x40, and writing that
             * over the image it was made from would lose the full-size one
             * for good. */
            if (!n.flag_bytes.empty() && !world.sidecar_blobs.count("od/" + flagName)) {
                zip.put(flagName, n.flag_bytes);
            }
        }
        c["treasury"] = n.treasury;

        const auto extraIt = n.extra.find("od");
        if (extraIt != n.extra.end() && extraIt->is_object()) {
            for (auto f = extraIt->begin(); f != extraIt->end(); ++f) {
                if (f.key() == "country_compass") { countryCompass[n.key] = f.value(); continue; }
                if (f.key() == "starting_policies") { startingPolicies[n.key] = f.value(); continue; }
                c[f.key()] = f.value();
            }
        }
        countries[std::to_string(id)] = c;

        Json rel = Json::object();
        for (const auto& kv : n.relations) {
            Json r = Json::object();
            if (kv.second.ally) r["ally"] = true;
            if (kv.second.non_aggression) r["nonAggression"] = true;
            if (kv.second.guarantee) r["guarantee"] = true;
            if (kv.second.truce) r["truce"] = true;
            if (kv.second.at_war) r["atWar"] = true;
            if (!r.empty()) rel[kv.first] = r;
        }
        if (!rel.empty()) relations[n.key] = rel;
    }

    for (const auto& n : world.nations) {
        if (!n.claims.empty()) claims[n.key] = n.claims;
    }
    /* Claims held by a country that is no longer on the map, kept from the
     * read and written back where they were. */
    const auto sidecarOdIn = world.sidecar.find("od");
    if (sidecarOdIn != world.sidecar.end() && sidecarOdIn->contains("orphan_claims")) {
        const Json& orphans = (*sidecarOdIn)["orphan_claims"];
        for (auto it = orphans.begin(); it != orphans.end(); ++it) {
            if (!claims.contains(it.key())) claims[it.key()] = it.value();
        }
    }

    Json provinces = Json::object();
    Json population = Json::object();
    Json resources = Json::object();
    Json ports = Json::object();
    Json armies = Json::object();
    Json compass = Json::object();
    Json minorities = Json::object();

    std::set<uint32_t> seaIds;
    std::map<uint32_t, uint32_t> ownerColor;

    for (const auto& p : world.provinces) {
        const std::string key = idKey(p.id);
        Json j = Json::object();
        j["id"] = p.id;
        j["name"] = p.name;
        const auto idIt = isoToId.find(p.owner);
        j["country_id"] = idIt != isoToId.end() ? idIt->second : 0;
        j["iso_a3"] = p.owner;
        j["color"] = formatHexColor(static_cast<uint32_t>(p.id));
        provinces[key] = j;

        population[key] = p.population;

        Json r = Json::object();
        for (const char* name : {"oil", "gold", "rubber", "gemstones", "metal"}) {
            const auto it = p.resources.find(name);
            r[name] = Json{{"a", it != p.resources.end() ? it->second.a : 0.0},
                           {"b", it != p.resources.end() ? it->second.b : 0.0}};
        }
        const auto ind = p.extra.find("od_industry");
        if (ind != p.extra.end()) {
            r["industry"] = *ind;
        } else {
            r["industry"] = Json{{"level", p.industry}, {"income", 0.0},
                                 {"specialization", ""}, {"resourceIncome", 0.0},
                                 {"popIncome", 0.0}, {"popModifier", 1.0},
                                 {"fortification", 0}};
        }
        r["fortification"] = p.fortification;
        resources[key] = r;

        if (p.port_level > 0) ports[key] = Json{{"level", p.port_level}};

        const auto comp = p.extra.find("od_compass");
        if (comp != p.extra.end()) compass[key] = *comp;
        const auto min = p.extra.find("od_minorities");
        if (min != p.extra.end()) minorities[key] = *min;

        const auto gar = p.extra.find("garrison");
        if (gar != p.extra.end() && gar->is_array() && !gar->empty()) {
            Json list = Json::array();
            for (const auto& g : *gar) {
                const std::string owner = g.value("owner", std::string());
                const auto oit = isoToId.find(owner);
                int64_t count = g.value("count", int64_t(0));
                if (count == 0 && g.contains("health")) {
                    count = static_cast<int64_t>(std::llround(
                        g["health"].get<double>() * kMenPerHealth));
                }
                list.push_back(Json{{"country_id", oit != isoToId.end() ? oit->second : 0},
                                    {"count", count}});
            }
            if (!list.empty()) armies[key] = list;
        }

        if (p.is_sea) seaIds.insert(static_cast<uint32_t>(p.id));
        const Nation* owner = world.findNation(p.owner);
        if (owner) ownerColor[static_cast<uint32_t>(p.id)] = owner->color;
    }

    /* Ships: back from provinces to latitude and longitude if the original
     * pair was not carried. */
    Json ships = Json::array();
    const auto sidecarOd = world.sidecar.find("od");
    if (sidecarOd != world.sidecar.end() && sidecarOd->contains("ships")
        && (*sidecarOd)["ships"].is_array()) {
        for (const auto& s : (*sidecarOd)["ships"]) {
            Json out = s;
            out.erase("owner");
            out.erase("province");
            const std::string owner = s.value("owner", std::string());
            const auto oit = isoToId.find(owner);
            if (oit != isoToId.end()) out["country_id"] = oit->second;
            ships.push_back(out);
        }
    }

    /* ---- the layers ----
     *
     * Open Doctrines reads an unpainted pixel as water: 67% of its 1914
     * province raster is unassigned and its land mask agrees to within a
     * rounding error, so for this format province coverage and landmass are
     * the same thing. GD5 reads the same pixel as "not painted yet" -- its map
     * painter samples every third pixel and leaves the border between any two
     * provinces blank, which is 37% of its 1914 scenario.
     *
     * Written across unchanged, every one of those borders would become a sea
     * channel three pixels wide, cutting a shipping lane along each provincial
     * boundary in Europe. So a map whose water is made of provinces -- which
     * is how we know it came from a game that paints borders -- has its gaps
     * filled from the nearest province first. The mask of what was filled goes
     * in the sidecar, so the crossing back restores the borders exactly and
     * the round trip still holds.
     */
    std::vector<uint32_t> raster = world.raster;
    std::map<std::string, std::vector<uint8_t>> extraBlobs;

    /* Which of the two meanings this raster's blank pixels carry -- border, or
     * open water -- is the same question the GD5 writer asks before inventing
     * an ocean, and it is asked through the same function so the two can never
     * disagree. Filling a map whose blanks really are the sea would turn the
     * Atlantic into land, which is exactly what an earlier version of this did
     * to every Open Doctrines map that crossed. */
    if (waterIsProvinced(world.raster, world.provinces)) {
        std::vector<uint8_t> mask;
        std::vector<uint32_t> filled = fillGaps(world.raster, world.width, world.height,
                                                /*wrap_x=*/true, &mask);
        long gaps = 0;
        for (uint8_t m : mask) {
            if (m) ++gaps;
        }
        if (gaps > 0) {
            raster = std::move(filled);
            extraBlobs["gaps/id_gaps.png"] = encodePngGray(mask, world.width, world.height);
            report.info("od.filled",
                        "filled " + std::to_string(gaps)
                            + " unpainted pixels from their nearest province, because Open "
                              "Doctrines reads an unpainted pixel as open water");
        }
    }

    const Image provImg = rasterToOd(raster, world.width, world.height);
    zip.put("provinces.png", encodePng(provImg));
    zip.put("land_sea.png", encodePng(landSeaImage(raster, world.width, world.height, seaIds)));

    Json meta = Json::object();
    meta["name"] = world.name;
    meta["description"] = world.description;
    meta["author"] = world.author;
    meta["map_date"] = formatOdDate(world.date);
    meta["license"] = world.license;
    meta["has_scripts"] = !world.scripts.empty();

    zip.putText("metadata.json", meta.dump());
    zip.putText("provinces.json", provinces.dump());
    zip.putText("countries.json", countries.dump());
    zip.putText("population.json", population.dump());
    zip.putText("resources.json", resources.dump());
    zip.putText("ports.json", ports.dump());
    zip.putText("armies.json", armies.dump());
    zip.putText("ships.json", ships.dump());
    zip.putText("relations.json", relations.dump());
    zip.putText("claims.json", claims.dump());
    zip.putText("political_compass.json", compass.dump());
    zip.putText("country_compass.json", countryCompass.dump());
    zip.putText("minorities.json", minorities.dump());
    if (!startingPolicies.empty()) {
        zip.putText("starting_policies.json", Json{{"starting_policies", startingPolicies}}.dump());
    }

    for (const auto& s : world.scripts) {
        zip.putText("scripts/" + s.name, s.text);
    }

    /* Files the model never modelled, put back exactly as they were read --
     * policies.json, the flag licences, the symbol set, the thumbnail. */
    if (sidecarOd != world.sidecar.end()) {
        const auto files = sidecarOd->find("files");
        if (files != sidecarOd->end() && files->is_object()) {
            for (auto it = files->begin(); it != files->end(); ++it) {
                if (!zip.has(it.key())) zip.putText(it.key(), it.value().dump());
            }
        }
    }
    for (const auto& kv : world.sidecar_blobs) {
        if (!startsWith(kv.first, "od/")) continue;
        const std::string name = kv.first.substr(3);
        if (!zip.has(name)) zip.put(name, kv.second);
    }

    if (opt.carry_sidecar) writeSidecarInto(zip, world, extraBlobs);

    /* Restore the member order the archive had, so that two versions of a map
     * diff on content rather than on layout. */
    if (sidecarOd != world.sidecar.end() && sidecarOd->contains("zip_order")) {
        std::vector<ZipEntry> ordered;
        std::vector<bool> used(zip.entries.size(), false);
        for (const auto& want : (*sidecarOd)["zip_order"]) {
            const std::string name = want.value("name", std::string());
            for (size_t i = 0; i < zip.entries.size(); ++i) {
                if (used[i] || zip.entries[i].name != name) continue;
                zip.entries[i].mtime = want.value("mtime", int64_t(0));
                ordered.push_back(zip.entries[i]);
                used[i] = true;
                break;
            }
            if (want.value("dir", false)) {
                bool present = false;
                for (const auto& o : ordered) present = present || (o.name == name && o.is_dir);
                if (!present) {
                    ZipEntry d;
                    d.name = name;
                    d.is_dir = true;
                    d.mtime = want.value("mtime", int64_t(0));
                    ordered.push_back(d);
                }
            }
        }
        for (size_t i = 0; i < zip.entries.size(); ++i) {
            if (!used[i]) ordered.push_back(zip.entries[i]);
        }
        zip.entries = std::move(ordered);
    }

    std::string err;
    if (!writeZip(path, zip, err)) {
        setLastError(err);
        return false;
    }

    /* GD5's `materials` and Open Doctrines' `treasury` are both "what this
     * country has to spend", and they are not the same number. GD5 scenarios
     * start their nations at zero and let them accumulate -- its 1914 scenario
     * gives 765 of 766 nations nothing at all -- while Open Doctrines expects
     * a starting endowment and its shipped 1914 map hands out a median of 10.
     * Translated faithfully, that zero is still zero, and the first simulated
     * turn bankrupts the entire world. Saying so is the honest thing; picking
     * a number out of the air and calling it a translation is not. */
    long funded = 0;
    for (const auto& n : world.nations) {
        if (n.treasury > 0.0) ++funded;
    }
    if (!world.nations.empty() && funded * 20 < static_cast<long>(world.nations.size())) {
        report.warn("od.treasury",
                    "almost no nation on this map has a treasury (" + std::to_string(funded)
                        + " of " + std::to_string(world.nations.size())
                        + "). GD5 starts its nations with an empty stockpile and Open Doctrines "
                          "expects a starting endowment, so the map will load and play but every "
                          "country goes bankrupt on the first turn. Set starting treasuries in "
                          "Open Doctrines' map editor, or in countries.json.");
    }

    report.info("od.write", "wrote " + std::to_string(world.provinces.size()) + " provinces to "
                                + path);
    if (opt.strict && report.hasWarnings()) {
        setLastError("strict mode: the translation produced warnings");
        return false;
    }
    return true;
}

}  // namespace dragoman
