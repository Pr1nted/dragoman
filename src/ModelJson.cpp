/* The interchange model as text.
 *
 * This is the only view of a map that crosses the C ABI whole, and it is what
 * a binding in Python or Go reads instead of an accessor per field. It is
 * therefore a published schema, versioned with the ABI and documented in
 * docs/model.md -- fields may be added, and existing ones do not change
 * meaning without the ABI version moving.
 *
 * The province raster is the one thing not written out. A world map is
 * 8192x4096, and thirty-three million province ids as JSON text is a hundred
 * megabytes to say what the accompanying PNG already says. It is summarised
 * by its dimensions and a digest instead, which is enough to tell two rasters
 * apart -- which is all any caller has ever wanted from it here.
 */
#include "ModelJson.h"

namespace dragoman {

uint64_t rasterDigest(const std::vector<uint32_t>& raster) {
    /* FNV-1a over the id stream. Not a cryptographic claim -- this exists to
     * notice a raster that changed, not to resist one crafted to collide. */
    uint64_t h = 1469598103934665603ull;
    for (uint32_t v : raster) {
        for (int b = 0; b < 4; ++b) {
            h ^= (v >> (b * 8)) & 0xff;
            h *= 1099511628211ull;
        }
    }
    return h;
}

namespace {

Json dateToJson(const Date& d) {
    return Json{{"year", d.year}, {"month", d.month}, {"day", d.day},
                {"turn", d.turn}, {"ad", d.ad}};
}

Date dateFromJson(const Json& j) {
    Date d;
    if (!j.is_object()) return d;
    d.year = j.value("year", 1);
    d.month = j.value("month", 1);
    d.day = j.value("day", 1);
    d.turn = j.value("turn", int64_t(0));
    d.ad = j.value("ad", true);
    return d;
}

Json relationToJson(const Relation& r) {
    Json j = Json::object();
    if (r.ally) j["ally"] = true;
    if (r.non_aggression) j["non_aggression"] = true;
    if (r.guarantee) j["guarantee"] = true;
    if (r.truce) j["truce"] = true;
    if (r.at_war) j["at_war"] = true;
    return j;
}

}  // namespace

Json worldToJson(const World& w) {
    Json j = Json::object();
    j["schema"] = "dragoman/world/1";
    j["name"] = w.name;
    j["description"] = w.description;
    j["author"] = w.author;
    j["license"] = w.license;
    j["date"] = dateToJson(w.date);
    j["origin"] = w.origin == 1 ? "odmap" : w.origin == 2 ? "gd5" : "unknown";
    j["raster"] = Json{{"width", w.width},
                       {"height", w.height},
                       {"pixels", static_cast<uint64_t>(w.raster.size())},
                       {"digest", rasterDigest(w.raster)}};

    Json provinces = Json::array();
    for (const auto& p : w.provinces) {
        Json o = Json::object();
        o["id"] = p.id;
        o["name"] = p.name;
        o["owner"] = p.owner;
        o["is_sea"] = p.is_sea;
        o["is_coastal"] = p.is_coastal;
        o["terrain"] = p.terrain;
        o["population"] = p.population;
        o["industry"] = p.industry;
        o["fortification"] = p.fortification;
        o["port_level"] = p.port_level;
        if (p.has_center) o["center"] = Json::array({p.center_x, p.center_y});
        if (p.has_neighbors) o["neighbors"] = p.neighbors;
        if (!p.cores.empty()) o["cores"] = p.cores;
        if (!p.resources.empty()) {
            Json r = Json::object();
            for (const auto& kv : p.resources) {
                r[kv.first] = Json{{"a", kv.second.a}, {"b", kv.second.b}};
            }
            o["resources"] = r;
        }
        if (!p.extra.empty()) o["extra"] = p.extra;
        provinces.push_back(std::move(o));
    }
    j["provinces"] = provinces;

    Json nations = Json::array();
    for (const auto& n : w.nations) {
        Json o = Json::object();
        o["key"] = n.key;
        o["name"] = n.name;
        o["adjective"] = n.adjective;
        o["color"] = formatHexColor(n.color);
        o["treasury"] = n.treasury;
        o["leader_name"] = n.leader_name;
        o["leader_title"] = n.leader_title;
        o["playable"] = n.playable;
        if (!n.flag_name.empty()) o["flag"] = n.flag_name;
        o["flag_bytes"] = static_cast<uint64_t>(n.flag_bytes.size());
        if (!n.claims.empty()) o["claims"] = n.claims;
        if (!n.relations.empty()) {
            Json r = Json::object();
            for (const auto& kv : n.relations) {
                Json one = relationToJson(kv.second);
                if (!one.empty()) r[kv.first] = one;
            }
            if (!r.empty()) o["relations"] = r;
        }
        if (!n.extra.empty()) o["extra"] = n.extra;
        nations.push_back(std::move(o));
    }
    j["nations"] = nations;

    Json events = Json::array();
    for (const auto& e : w.events) {
        Json o = Json::object();
        o["name"] = e.name;
        o["owner"] = e.owner;
        o["trigger"] = e.trigger;
        o["fire_once"] = e.fire_once;
        Json cs = Json::array();
        for (const auto& c : e.conditions) {
            cs.push_back(Json{{"kind", c.kind}, {"op", c.op}, {"value", c.value},
                              {"subject", c.subject}, {"chain", c.chain}});
        }
        o["conditions"] = cs;
        Json as = Json::array();
        for (const auto& a : e.actions) {
            Json one = Json{{"kind", a.kind}, {"target", a.target}, {"message", a.message}};
            if (!a.params.empty()) one["params"] = a.params;
            as.push_back(one);
        }
        o["actions"] = as;
        events.push_back(std::move(o));
    }
    j["events"] = events;

    Json scripts = Json::array();
    for (const auto& s : w.scripts) {
        scripts.push_back(Json{{"name", s.name}, {"entrypoint", s.entrypoint}, {"text", s.text}});
    }
    j["scripts"] = scripts;

    j["sidecar"] = w.sidecar;
    Json blobs = Json::object();
    for (const auto& kv : w.sidecar_blobs) blobs[kv.first] = static_cast<uint64_t>(kv.second.size());
    j["sidecar_blobs"] = blobs;

    return j;
}

bool worldFromJson(const Json& j, World& w, Report& report) {
    if (!j.is_object()) {
        report.error("model.badjson", "the interchange model must be a JSON object");
        return false;
    }
    w.name = j.value("name", std::string());
    w.description = j.value("description", std::string());
    w.author = j.value("author", std::string());
    w.license = j.value("license", std::string());
    w.date = dateFromJson(j.value("date", Json::object()));
    const std::string origin = j.value("origin", std::string("unknown"));
    w.origin = origin == "odmap" ? 1 : origin == "gd5" ? 2 : 0;

    if (j.contains("raster") && j["raster"].is_object()) {
        w.width = j["raster"].value("width", 0);
        w.height = j["raster"].value("height", 0);
        /* The pixels themselves are not in this document -- see the note at
         * the top of the file. A world rebuilt from JSON alone therefore has
         * dimensions and no raster, which every writer reports rather than
         * silently emitting an empty map. */
    }

    if (j.contains("provinces") && j["provinces"].is_array()) {
        for (const auto& o : j["provinces"]) {
            Province p;
            p.id = o.value("id", int64_t(0));
            p.name = o.value("name", std::string());
            p.owner = o.value("owner", std::string());
            p.is_sea = o.value("is_sea", false);
            p.is_coastal = o.value("is_coastal", false);
            p.terrain = o.value("terrain", std::string());
            p.population = o.value("population", int64_t(0));
            p.industry = o.value("industry", 0);
            p.fortification = o.value("fortification", 0);
            p.port_level = o.value("port_level", 0);
            if (o.contains("center") && o["center"].is_array() && o["center"].size() >= 2) {
                p.center_x = o["center"][0].get<int>();
                p.center_y = o["center"][1].get<int>();
                p.has_center = true;
            }
            if (o.contains("neighbors") && o["neighbors"].is_array()) {
                for (const auto& n : o["neighbors"]) p.neighbors.push_back(n.get<int64_t>());
                p.has_neighbors = true;
            }
            if (o.contains("cores") && o["cores"].is_array()) {
                for (const auto& c : o["cores"]) p.cores.push_back(c.get<std::string>());
            }
            if (o.contains("resources") && o["resources"].is_object()) {
                for (auto it = o["resources"].begin(); it != o["resources"].end(); ++it) {
                    Resource r;
                    r.a = it.value().value("a", 0.0);
                    r.b = it.value().value("b", 0.0);
                    p.resources[it.key()] = r;
                }
            }
            if (o.contains("extra")) p.extra = o["extra"];
            w.provinces.push_back(std::move(p));
        }
    }

    if (j.contains("nations") && j["nations"].is_array()) {
        for (const auto& o : j["nations"]) {
            Nation n;
            n.key = o.value("key", std::string());
            n.name = o.value("name", std::string());
            n.adjective = o.value("adjective", std::string());
            parseHexColor(o.value("color", std::string("#808080")), n.color);
            n.treasury = o.value("treasury", 0.0);
            n.leader_name = o.value("leader_name", std::string());
            n.leader_title = o.value("leader_title", std::string());
            n.playable = o.value("playable", true);
            n.flag_name = o.value("flag", std::string());
            if (o.contains("claims") && o["claims"].is_array()) {
                for (const auto& c : o["claims"]) n.claims.push_back(c.get<int64_t>());
            }
            if (o.contains("relations") && o["relations"].is_object()) {
                for (auto it = o["relations"].begin(); it != o["relations"].end(); ++it) {
                    Relation r;
                    r.ally = it.value().value("ally", false);
                    r.non_aggression = it.value().value("non_aggression", false);
                    r.guarantee = it.value().value("guarantee", false);
                    r.truce = it.value().value("truce", false);
                    r.at_war = it.value().value("at_war", false);
                    n.relations[it.key()] = r;
                }
            }
            if (o.contains("extra")) n.extra = o["extra"];
            w.nations.push_back(std::move(n));
        }
    }

    if (j.contains("events") && j["events"].is_array()) {
        for (const auto& o : j["events"]) {
            Event e;
            e.name = o.value("name", std::string());
            e.owner = o.value("owner", std::string());
            e.trigger = o.value("trigger", std::string("both"));
            e.fire_once = o.value("fire_once", true);
            if (o.contains("conditions") && o["conditions"].is_array()) {
                for (const auto& c : o["conditions"]) {
                    Condition cond;
                    cond.kind = c.value("kind", std::string());
                    cond.op = c.value("op", std::string());
                    cond.value = c.value("value", std::string());
                    cond.subject = c.value("subject", std::string());
                    cond.chain = c.value("chain", std::string("AND"));
                    e.conditions.push_back(cond);
                }
            }
            if (o.contains("actions") && o["actions"].is_array()) {
                for (const auto& a : o["actions"]) {
                    Action act;
                    act.kind = a.value("kind", std::string());
                    act.target = a.value("target", std::string());
                    act.message = a.value("message", std::string());
                    if (a.contains("params")) act.params = a["params"];
                    e.actions.push_back(act);
                }
            }
            w.events.push_back(std::move(e));
        }
    }

    if (j.contains("scripts") && j["scripts"].is_array()) {
        for (const auto& o : j["scripts"]) {
            ScriptSource s;
            s.name = o.value("name", std::string());
            s.text = o.value("text", std::string());
            s.entrypoint = o.value("entrypoint", false);
            w.scripts.push_back(std::move(s));
        }
    }

    if (j.contains("sidecar")) w.sidecar = j["sidecar"];
    return true;
}

}  // namespace dragoman
