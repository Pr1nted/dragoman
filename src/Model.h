/* The interchange model: what both games are saying, in one vocabulary.
 *
 * Neither game's own layout is used as the pivot. Open Doctrines stores a map
 * as a dozen parallel JSON objects keyed by province id; Greater Diplomacy 5
 * stores one object per province with everything inline. Translating directly
 * between them would need one function per pair of fields and would double
 * again the day a third game appeared. Everything here is instead the union
 * of what the two describe, and each format owns exactly one reader and one
 * writer against it.
 *
 * The rule for a field: it lives here if both games have a concept for it,
 * even under different names. If only one does -- Open Doctrines' ethnic
 * minorities, GD5's research tree -- it rides in `extra` and, on the way out,
 * in the sidecar, so that a round trip restores it even though the other game
 * can do nothing with it.
 */
#ifndef DRAGOMAN_MODEL_H
#define DRAGOMAN_MODEL_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dragoman {

using Json = nlohmann::ordered_json;

/* A date both games can hold. Open Doctrines writes "July 1914 AD" and steps
 * one month per turn; GD5 keeps day/month/year and a turn counter and steps a
 * configurable number of days. Keeping all four fields means neither loses
 * what it had, and the one it does not use is simply not written. */
struct Date {
    int  year  = 1;
    int  month = 1;   /* 1-12, human numbering; GD5's own 0-11 is converted */
    int  day   = 1;
    /* int64_t, not long: `long` is 64-bit on Linux and macOS and 32-bit on
     * MSVC, so this field silently changed width with the compiler and reading
     * a turn counter out of JSON narrowed on Windows. Every other integer in
     * this model is already fixed-width for the same reason -- the two games'
     * files are read by whichever build happens to open them. */
    int64_t turn = 0;
    bool ad    = true;
};

/* Open Doctrines models a deposit as a pair (`a`, `b`) -- the surface figure
 * the player sees and the reserve behind it. GD5 has a flat quantity per
 * resource, so `b` is dropped on the way out and restored from the sidecar on
 * the way back. */
struct Resource {
    double a = 0.0;
    double b = 0.0;
};

struct Province {
    int64_t     id = 0;             /* the shared identity; both rasters encode it */
    std::string name;
    std::string owner;              /* nation key, or "" for unowned */

    /* Geometry. Open Doctrines derives adjacency and centroids from the
     * raster at load and never stores them; GD5 stores both and will not run
     * without them. `has_*` records whether the number was read or computed,
     * which the report needs in order to say so. */
    int32_t              center_x = 0, center_y = 0;
    bool                 has_center = false;
    std::vector<int64_t> neighbors;
    bool                 has_neighbors = false;

    bool        is_sea     = false;
    bool        is_coastal = false;
    std::string terrain;            /* canonical token; see Terrain.h */

    int64_t population    = 0;
    int     industry      = 0;
    int     fortification = 0;
    int     port_level    = 0;      /* 0 = no port */

    std::map<std::string, Resource> resources;
    std::vector<std::string>        cores;   /* nations holding a claim here */

    Json extra = Json::object();    /* fields only one side has */
};

struct Relation {
    bool ally           = false;
    bool non_aggression = false;
    bool guarantee      = false;
    bool truce          = false;
    bool at_war         = false;
};

struct Nation {
    /* The key is what every other structure refers to. Open Doctrines keys by
     * ISO 3166 alpha-3 and GD5 by display name, so one of the two is always
     * synthesised -- see NationKey.h, which also records the pairing in the
     * sidecar so the synthesis is not repeated differently next time. */
    std::string key;
    std::string name;               /* display name: "German Empire"          */
    std::string adjective;          /* GD5 has one; Open Doctrines does not   */

    uint32_t    color = 0;          /* 0xRRGGBB */
    double      treasury = 0.0;

    std::string leader_name;
    std::string leader_title;

    /* Open Doctrines points at a PNG inside the archive ("flags/GER.png");
     * GD5 inlines base64 in `flag_data`. Both are carried as bytes plus the
     * name they had, and each writer emits its own shape. */
    std::string          flag_name;
    std::vector<uint8_t> flag_bytes;

    bool playable = true;

    /* Where the country sits on the one axis both games have: NEGATIVE is
     * libertarian, POSITIVE authoritarian, and the scale is Open Doctrines'
     * -100..+100 because it is the finer of the two.
     *
     * THE SIGN IS NOT OBVIOUS and getting it backwards is silent. Open
     * Doctrines' compass FILE stores `auth`, positive for authoritarian, which
     * is what this is; its in-memory PoliticalCompass stores `social`, which is
     * the NEGATION of that, and its loader flips both axes on the way in. This
     * library only ever sees the file. Greater Diplomacy 5's `political_value`
     * agrees with the file's sign and runs -10..+10, so it is this divided by
     * ten. See docs/mapping.md.
     *
     * has_political_axis distinguishes "centrist" from "the map did not say",
     * which matters because zero is a legitimate position in both games and
     * both of them default to it. */
    double political_axis = 0.0;
    bool   has_political_axis = false;

    std::map<std::string, Relation> relations;
    std::vector<int64_t>            claims;

    Json extra = Json::object();
};

/* ------------------------------------------------------------------ scripts
 *
 * The two scripting systems are not the same kind of thing. Open Doctrines'
 * is an imperative line-based language with loops and suspension points; GD5's
 * is a list of declarative events, each a set of conditions and a set of
 * actions, evaluated once per turn. The model holds the declarative shape,
 * because it is the smaller of the two and every GD5 event maps onto it
 * exactly, while an Open Doctrines script maps onto it only when it is
 * shaped like one -- top-level stages gated by `waitUntil`. Anything more
 * (loops, nested conditionals) is reported and carried verbatim rather than
 * mistranslated.
 */
struct Condition {
    std::string kind;      /* canonical: "turn", "at_war_with", "variable", ... */
    std::string op;        /* "==", "!=", ">", "<", ">=", "<=", "between"       */
    std::string value;
    std::string subject;   /* nation key the condition is about, when not self  */
    std::string chain;     /* how it joins the previous: AND OR XOR NOR NAND    */
};

struct Action {
    std::string kind;      /* canonical: "declare_war", "set_owner", "set_var"  */
    std::string target;
    std::string message;
    Json        params = Json::object();
};

struct Event {
    std::string            name;
    std::string            owner;      /* nation the event belongs to */
    std::string            trigger;    /* "ai" | "player" | "both" */
    bool                   fire_once = true;
    std::vector<Condition> conditions;
    std::vector<Action>    actions;
};

/* A source file kept as it was written, so that a script we could only
 * partly translate is still recoverable on the way back. */
struct ScriptSource {
    std::string name;
    std::string text;
    bool        entrypoint = false;
};

struct World {
    std::string name;
    std::string description;
    std::string author;
    std::string license;
    Date        date;

    /* The province raster, one province id per pixel, row-major, 0 for
     * nothing. Both games paint the same picture and disagree only on how a
     * pixel spells an id -- Open Doctrines big-endian, GD5 little-endian --
     * so holding ids rather than colours makes the difference a detail of
     * each writer instead of a conversion. */
    int                   width = 0, height = 0;
    std::vector<uint32_t> raster;

    std::vector<Province> provinces;
    std::vector<Nation>   nations;

    std::vector<Event>        events;
    std::vector<ScriptSource> scripts;

    /* Everything the source had and the model has no field for, keyed by the
     * file it came from. Written beside the destination map and read back on
     * the return trip. */
    Json                                          sidecar = Json::object();
    std::map<std::string, std::vector<uint8_t>>   sidecar_blobs;

    /* The model as it stood the last time this map was written by Dragoman.
     * Carrying the opaque files was never enough on its own: a province's
     * population is a modelled field with no GD5 counterpart at all, so
     * crossing drops it and only a record of what it was can put it back.
     * Restored from here by restoreUnrepresentable(), and deliberately not
     * part of the model's own JSON -- it is provenance, not content, and
     * including it would nest each crossing inside the next. */
    Json prior_model = Json::object();

    int origin = 0;  /* dg_format the world was read from */

    const Province* findProvince(int64_t id) const;
    const Nation*   findNation(const std::string& key) const;
    Province*       findProvince(int64_t id);
    Nation*         findNation(const std::string& key);
};

}  // namespace dragoman

#endif
