/* The C ABI: the only entry point anything outside this library uses. */
#include <dragoman/dragoman.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>

#include "Formats.h"
#include "ModelJson.h"

namespace fs = std::filesystem;
using namespace dragoman;

struct dg_world {
    World w;
};

struct dg_report {
    Report r;
    /* The strings handed out by dg_report_code/message must outlive the call,
     * and the Report already owns them, so they are returned by pointer into
     * it. Nothing here copies. */
};

namespace {

Options fromC(const dg_options* o) {
    Options opt;
    if (!o) return opt;
    opt.carry_sidecar = o->carry_sidecar != 0;
    opt.derive_geometry = o->derive_geometry != 0;
    opt.translate_scripts = o->translate_scripts != 0;
    opt.strict = o->strict != 0;
    opt.reencode_images = o->reencode_images != 0;
    opt.synthesise_ocean = o->synthesise_ocean != 0;
    return opt;
}

char* dupString(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

bool loadInto(const std::string& path, dg_format fmt, const Options& opt, World& world,
              Report& report) {
    int detected = fmt == DG_FORMAT_UNKNOWN ? detectFormat(path) : static_cast<int>(fmt);
    if (detected == 1) return readOdMap(path, opt, world, report);
    if (detected == 2) return readGd5Map(path, opt, world, report);
    if (detected == 3) return readUncivMap(path, opt, world, report);
    setLastError(path + " is neither an Open Doctrines .odmap nor a GD5 map directory");
    return false;
}

/* Scripts and events are the same intent in two shapes, and only the shape
 * the destination reads is generated -- doing both would put a translated
 * script and the events it came from into the same map, and the game would
 * run both. */
void bridgeScripts(World& world, dg_format to, const Options& opt, Report& report) {
    if (!opt.translate_scripts) return;
    if (to == DG_FORMAT_GD5 && world.events.empty() && !world.scripts.empty()) {
        const std::string owner = world.nations.empty() ? std::string() : world.nations.front().key;
        scriptsToEvents(world.scripts, owner, world.events, report);
    } else if (to == DG_FORMAT_ODMAP && world.scripts.empty() && !world.events.empty()) {
        eventsToScripts(world.events, world.scripts, report);
    }
}

bool saveFrom(const World& world, const std::string& path, dg_format fmt, const Options& opt,
              Report& report) {
    if (fmt == DG_FORMAT_ODMAP) return writeOdMap(path, world, opt, report);
    if (fmt == DG_FORMAT_GD5) return writeGd5Map(path, world, opt, report);
    if (fmt == DG_FORMAT_UNCIV) return writeUncivMap(path, world, opt, report);
    setLastError("no such output format");
    return false;
}

/* The view two maps are compared as.
 *
 * The sidecar itself is left out and replaced by the parts of it that are
 * content rather than bookkeeping. Its zip member order and the model
 * snapshot are records of how a map was carried, not of what it says, and
 * comparing them would fail a round trip for having happened.
 */
Json comparableView(const World& w) {
    Json j = worldToJson(w);
    j.erase("sidecar");

    /* Province adjacency, centres and coastal flags are dropped, because they
     * are not facts a map states -- they are functions of the province raster,
     * recomputed identically from it by whichever side needs them, and the
     * raster itself is compared by digest below. Keeping them would fail a
     * round trip merely for having derived, on the way home, a neighbour list
     * the Open Doctrines map never had to store in the first place. */
    if (j.contains("provinces")) {
        for (auto& p : j["provinces"]) {
            p.erase("is_coastal");
            p.erase("center");
            p.erase("neighbors");
        }
    }

    Json carried = Json::object();
    const auto od = w.sidecar.find("od");
    if (od != w.sidecar.end() && od->contains("files")) carried["od_files"] = (*od)["files"];
    if (od != w.sidecar.end() && od->contains("ships")) carried["od_ships"] = (*od)["ships"];
    const auto gd5 = w.sidecar.find("gd5");
    if (gd5 != w.sidecar.end() && gd5->contains("meta")) carried["gd5_meta"] = (*gd5)["meta"];
    j["carried"] = carried;
    return j;
}

/* Every fact in `expected` is present and equal in `actual`.
 *
 * Containment rather than equality, because a round trip is allowed to come
 * home richer than it left. Crossing to GD5 and back picks up that game's
 * hand-painted terrain layer, which the returning .odmap then carries so the
 * next crossing keeps it. That is a gain, and failing the check for it would
 * punish the library for working. Losing anything, which is the thing this
 * function exists to catch, still fails.
 */
bool jsonContains(const Json& expected, const Json& actual, std::string& where) {
    /* An empty value asserts nothing. Open Doctrines has no terrain field, so
     * every province it describes has an empty terrain; the map that comes
     * home from GD5 has "ocean" and "plains" in those slots. Requiring them to
     * still be empty would be requiring the crossing to have learnt nothing.
     * A value that was set and came back empty is the reverse case, and still
     * fails -- the check below is on `expected`, not on `actual`. */
    if (expected.is_null()) return true;
    if (expected.is_string() && expected.get<std::string>().empty()) return true;
    if ((expected.is_array() || expected.is_object()) && expected.empty()) return true;

    if (expected.is_object()) {
        if (!actual.is_object()) { where = "type changed"; return false; }
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const auto found = actual.find(it.key());
            if (found == actual.end()) {
                where = it.key() + " went missing";
                return false;
            }
            std::string inner;
            if (!jsonContains(it.value(), *found, inner)) {
                where = it.key() + (inner.empty() ? "" : " -> " + inner);
                return false;
            }
        }
        return true;
    }
    if (expected.is_array()) {
        if (!actual.is_array() || actual.size() != expected.size()) {
            where = "list length changed";
            return false;
        }
        for (size_t i = 0; i < expected.size(); ++i) {
            std::string inner;
            if (!jsonContains(expected[i], actual[i], inner)) {
                where = "[" + std::to_string(i) + "]" + (inner.empty() ? "" : " -> " + inner);
                return false;
            }
        }
        return true;
    }
    if (expected != actual) {
        where = "value changed";
        return false;
    }
    return true;
}

std::string scratchPath(const char* tag) {
    static std::atomic<unsigned> counter{0};
    const unsigned n = counter.fetch_add(1);
    fs::path p = fs::temp_directory_path()
                 / ("dragoman-" + std::string(tag) + "-" + std::to_string(n));
    return p.string();
}

}  // namespace

extern "C" {

const char* dg_version_string(void) { return DRAGOMAN_VERSION_STRING; }
int         dg_version_major(void) { return DRAGOMAN_VERSION_MAJOR; }
int         dg_version_minor(void) { return DRAGOMAN_VERSION_MINOR; }
int         dg_version_patch(void) { return DRAGOMAN_VERSION_PATCH; }
int         dg_abi_version(void) { return DRAGOMAN_ABI_VERSION; }

const char* dg_format_name(dg_format fmt) {
    switch (fmt) {
        case DG_FORMAT_ODMAP: return "odmap";
        case DG_FORMAT_GD5: return "gd5";
        case DG_FORMAT_UNCIV: return "unciv";
        default: return "unknown";
    }
}

dg_format dg_detect(const char* path) {
    if (!path) return DG_FORMAT_UNKNOWN;
    return static_cast<dg_format>(detectFormat(path));
}

void dg_options_defaults(dg_options* out) {
    if (!out) return;
    out->carry_sidecar = 1;
    out->derive_geometry = 1;
    out->translate_scripts = 1;
    out->strict = 0;
    out->reencode_images = 0;
    out->synthesise_ocean = 1;
}

int dg_report_count(const dg_report* r) {
    return r ? static_cast<int>(r->r.entries().size()) : 0;
}

int dg_report_severity(const dg_report* r, int i) {
    if (!r || i < 0 || i >= static_cast<int>(r->r.entries().size())) return -1;
    return r->r.entries()[i].severity;
}

const char* dg_report_code(const dg_report* r, int i) {
    if (!r || i < 0 || i >= static_cast<int>(r->r.entries().size())) return "";
    return r->r.entries()[i].code.c_str();
}

const char* dg_report_message(const dg_report* r, int i) {
    if (!r || i < 0 || i >= static_cast<int>(r->r.entries().size())) return "";
    return r->r.entries()[i].message.c_str();
}

int dg_report_worst(const dg_report* r) { return r ? r->r.worst() : 0; }

char* dg_report_to_json(const dg_report* r) {
    if (!r) return dupString("[]");
    return dupString(r->r.toJson().dump(1, ' '));
}

void dg_report_free(dg_report* r) { delete r; }

void dg_string_free(char* s) { std::free(s); }

const char* dg_last_error(void) { return lastError(); }

dg_world* dg_load(const char* path, dg_format fmt, const dg_options* opts,
                  dg_report** out_report) {
    auto* report = new (std::nothrow) dg_report();
    if (out_report) *out_report = report;
    if (!path || !report) {
        setLastError("dg_load needs a path");
        return nullptr;
    }
    auto* world = new (std::nothrow) dg_world();
    if (!world) {
        setLastError("out of memory");
        return nullptr;
    }
    try {
        if (!loadInto(path, fmt, fromC(opts), world->w, report->r)) {
            delete world;
            return nullptr;
        }
    } catch (const std::exception& ex) {
        setLastError(std::string("failed to read ") + path + ": " + ex.what());
        delete world;
        return nullptr;
    }
    if (!out_report) delete report;
    return world;
}

int dg_save(const dg_world* w, const char* path, dg_format fmt, const dg_options* opts,
            dg_report** out_report) {
    auto* report = new (std::nothrow) dg_report();
    if (out_report) *out_report = report;
    if (!w || !path || !report) {
        setLastError("dg_save needs a world and a path");
        return 1;
    }
    int rc = 0;
    try {
        const Options opt = fromC(opts);
        World copy = w->w;
        bridgeScripts(copy, fmt, opt, report->r);
        rc = saveFrom(copy, path, fmt, opt, report->r) ? 0 : 1;
    } catch (const std::exception& ex) {
        setLastError(std::string("failed to write ") + path + ": " + ex.what());
        rc = 1;
    }
    if (!out_report) delete report;
    return rc;
}

int dg_convert(const char* in_path, const char* out_path, dg_format to, const dg_options* opts,
               dg_report** out_report) {
    auto* report = new (std::nothrow) dg_report();
    if (out_report) *out_report = report;
    if (!in_path || !out_path || !report) {
        setLastError("dg_convert needs an input and an output path");
        return 1;
    }
    int rc = 0;
    try {
        const Options opt = fromC(opts);
        World world;
        if (!loadInto(in_path, DG_FORMAT_UNKNOWN, opt, world, report->r)) {
            rc = 1;
        } else {
            bridgeScripts(world, to, opt, report->r);
            rc = saveFrom(world, out_path, to, opt, report->r) ? 0 : 1;
        }
    } catch (const std::exception& ex) {
        setLastError(std::string("conversion failed: ") + ex.what());
        rc = 1;
    }
    if (!out_report) delete report;
    return rc;
}

int dg_roundtrip_check(const char* path, dg_format to, const dg_options* opts,
                       dg_report** out_report) {
    auto* report = new (std::nothrow) dg_report();
    if (out_report) *out_report = report;
    if (!path || !report) {
        setLastError("dg_roundtrip_check needs a path");
        return -1;
    }

    int result = -1;
    const std::string mid = scratchPath("mid");
    const std::string back = scratchPath("back");

    try {
        const Options opt = fromC(opts);
        World original;
        if (!loadInto(path, DG_FORMAT_UNKNOWN, opt, original, report->r)) {
            if (!out_report) delete report;
            return -1;
        }
        const dg_format from = static_cast<dg_format>(original.origin);

        World crossing = original;
        bridgeScripts(crossing, to, opt, report->r);
        if (!saveFrom(crossing, mid, to, opt, report->r)) {
            if (!out_report) delete report;
            return -1;
        }

        World returned;
        if (!loadInto(mid, to, opt, returned, report->r)) {
            if (!out_report) delete report;
            return -1;
        }
        bridgeScripts(returned, from, opt, report->r);
        if (!saveFrom(returned, back, from, opt, report->r)) {
            if (!out_report) delete report;
            return -1;
        }

        World reloaded;
        if (!loadInto(back, from, opt, reloaded, report->r)) {
            if (!out_report) delete report;
            return -1;
        }

        /* Compared as the model sees them rather than as bytes. Two zip files
         * holding identical members are not identical files -- deflate is not
         * required to be deterministic across implementations, and Open
         * Doctrines' own packer is Python's zlib while this is miniz. What
         * matters, and what is asserted here, is that every province, nation,
         * relation, claim, script and carried file came back the same, and
         * that the province raster hashes to the same value. */
        const Json a = comparableView(original);
        const Json b = comparableView(reloaded);
        std::string where;
        result = jsonContains(a, b, where) ? 1 : 0;

        if (result == 0) {
            /* Name the field, not the map. "The round trip changed something"
             * is the first thing anyone has to go and find out for themselves. */
            report->r.error("roundtrip.differs", "the round trip did not preserve " + where);
            for (const char* section : {"provinces", "nations", "events", "scripts", "raster",
                                        "date", "name", "carried"}) {
                if (!a.contains(section) || !b.contains(section)) continue;
                std::string inner;
                if (!jsonContains(a[section], b[section], inner)) {
                    report->r.error("roundtrip.differs",
                                    std::string("  in \"") + section + "\": " + inner);
                }
            }
        } else {
            report->r.info("roundtrip.ok",
                           "the map returned from " + std::string(dg_format_name(to))
                               + " with every modelled field and the province raster unchanged");
        }
    } catch (const std::exception& ex) {
        setLastError(std::string("round trip failed: ") + ex.what());
        result = -1;
    }

    std::error_code ec;
    fs::remove_all(mid, ec);
    fs::remove_all(back, ec);
    if (!out_report) delete report;
    return result;
}

void dg_world_free(dg_world* w) { delete w; }

int dg_world_province_count(const dg_world* w) {
    return w ? static_cast<int>(w->w.provinces.size()) : 0;
}

int dg_world_nation_count(const dg_world* w) {
    return w ? static_cast<int>(w->w.nations.size()) : 0;
}

int dg_world_script_count(const dg_world* w) {
    return w ? static_cast<int>(w->w.scripts.size() + w->w.events.size()) : 0;
}

const char* dg_world_name(const dg_world* w) { return w ? w->w.name.c_str() : ""; }

dg_format dg_world_origin(const dg_world* w) {
    return w ? static_cast<dg_format>(w->w.origin) : DG_FORMAT_UNKNOWN;
}

char* dg_world_to_json(const dg_world* w) {
    if (!w) return dupString("{}");
    try {
        return dupString(worldToJson(w->w).dump(1, ' '));
    } catch (const std::exception& ex) {
        setLastError(std::string("could not serialise the model: ") + ex.what());
        return nullptr;
    }
}

dg_world* dg_world_from_json(const char* json, dg_report** out_report) {
    auto* report = new (std::nothrow) dg_report();
    if (out_report) *out_report = report;
    if (!json || !report) {
        setLastError("dg_world_from_json needs a document");
        return nullptr;
    }
    auto* world = new (std::nothrow) dg_world();
    if (!world) {
        setLastError("out of memory");
        return nullptr;
    }
    try {
        if (!worldFromJson(Json::parse(json), world->w, report->r)) {
            delete world;
            return nullptr;
        }
    } catch (const std::exception& ex) {
        setLastError(std::string("could not parse the model: ") + ex.what());
        delete world;
        return nullptr;
    }
    if (!out_report) delete report;
    return world;
}

}  /* extern "C" */
