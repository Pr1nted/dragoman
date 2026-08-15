/* Scripts: what crosses, and what is refused out loud rather than guessed at. */
#include "Check.h"
#include "Fixture.h"

using namespace dragoman;

static ScriptSource script(const char* name, const char* text, bool entry = true) {
    ScriptSource s;
    s.name = name;
    s.text = text;
    s.entrypoint = entry;
    return s;
}

/* The shared subset: top-level stages gated by `waitUntil`, which is exactly
 * the shape a GD5 event has. */
static void testStagesBecomeEvents() {
    Report report;
    std::vector<Event> events;
    std::vector<ScriptSource> scripts{script("crisis.txt",
        "#OD/MapEngine/1\n"
        "set country.GER.treasury 5000\n"
        "waitUntil map.turn >= 12\n"
        "set country.GER.at_war_with RUS true\n"
        "set province.42.owner GER\n"
        "waitUntil map.turn >= 24\n"
        "set country.GER.at_war_with RUS false\n")};

    scriptsToEvents(scripts, "GER", events, report);
    REQUIRE(events.size() >= 2);

    /* The stage gated on turn 12 declares the war. */
    const Event* war = nullptr;
    for (const auto& e : events) {
        for (const auto& a : e.actions) {
            if (a.kind == "declare_war") war = &e;
        }
    }
    REQUIRE(war != nullptr);
    REQUIRE(war->conditions.size() == 1);
    CHECK_EQ(war->conditions[0].kind, std::string("turn"));
    CHECK_EQ(war->conditions[0].op, std::string(">="));
    CHECK_EQ(war->conditions[0].value, std::string("12"));
    CHECK_EQ(war->owner, std::string("GER"));

    bool sawTerritory = false;
    for (const auto& a : war->actions) {
        if (a.kind == "give_territory") {
            sawTerritory = true;
            CHECK_EQ(a.message, std::string("42"));
            CHECK_EQ(a.target, std::string("GER"));
        }
    }
    CHECK(sawTerritory);
}

/* Written into GD5's own vocabulary, which is what its editor and its turn
 * processor both read. */
static void testEventsBecomeGd5Json() {
    Report report;
    std::vector<Event> events;
    std::vector<ScriptSource> scripts{script("x.txt",
        "#OD/MapEngine/1\n"
        "waitUntil map.turn >= 12\n"
        "set country.GER.at_war_with RUS true\n")};
    scriptsToEvents(scripts, "GER", events, report);

    Json nations = Json::object();
    nations["German Empire"] = Json{{"scripted_events", Json::array()}};
    std::map<std::string, std::string> keys{{"GER", "German Empire"},
                                            {"RUS", "Russian Empire"}};
    eventsToGd5(events, keys, nations, report);

    const Json& out = nations["German Empire"]["scripted_events"];
    REQUIRE(out.is_array() && !out.empty());
    const Json& e = out[0];
    CHECK_EQ(e["conditions"][0]["type"].get<std::string>(), std::string("Turn Number"));
    CHECK_EQ(e["conditions"][0]["operator"].get<std::string>(), std::string(">="));
    CHECK_EQ(e["conditions"][0]["chain"].get<std::string>(), std::string("AND"));
    CHECK_EQ(e["actions"][0]["type"].get<std::string>(), std::string("Declare War"));
    /* Targets are nation display names on the GD5 side, not ISO codes. */
    CHECK_EQ(e["actions"][0]["target"].get<std::string>(), std::string("Russian Empire"));
    CHECK(e["fire_once"].get<bool>());
}

static void testGd5EventsBecomeScripts() {
    Report report;
    Json raw = Json::array();
    raw.push_back(Json{
        {"name", "Barbarossa"},
        {"trigger_type", "AI Only"},
        {"fire_once", true},
        {"conditions", Json::array({Json{{"type", "Turn Number"}, {"operator", ">="},
                                         {"value", "24"}, {"chain", "AND"}}})},
        {"actions", Json::array({Json{{"type", "Declare War"}, {"target", "RUS"}}})}});

    std::vector<Event> events;
    eventsFromGd5(raw, "GER", events, report);
    REQUIRE(events.size() == 1);
    CHECK_EQ(events[0].trigger, std::string("ai"));

    std::vector<ScriptSource> scripts;
    eventsToScripts(events, scripts, report);
    REQUIRE(scripts.size() == 1);

    const std::string& text = scripts[0].text;
    CHECK(scripts[0].entrypoint);
    /* The header is what makes Open Doctrines run a file at all; without it
     * the script is a library that never executes. */
    CHECK(startsWith(text, "#OD/MapEngine/1"));
    CHECK(text.find("waitUntil map.turn >= 24") != std::string::npos);
    CHECK(text.find("set country.GER.at_war_with RUS true") != std::string::npos);
}

/* The refusals. Each of these is something one side can say and the other
 * cannot, and each must produce a warning naming it rather than a silently
 * dropped line. */
static void testUnsupportedIsReportedNotGuessed() {
    Report report;
    std::vector<Event> events;
    std::vector<ScriptSource> scripts{script("loops.txt",
        "#OD/MapEngine/1\n"
        "foreach province in country.SYR\n"
        "    set province.population 250000\n"
        "next\n")};

    scriptsToEvents(scripts, "SYR", events, report);
    CHECK(events.empty());
    CHECK(report.hasWarnings());

    bool named = false;
    for (const auto& d : report.entries()) {
        if (d.code == "script.unsupported") named = true;
    }
    CHECK(named);
}

static void testNonAndChainIsReported() {
    Report report;
    Event e;
    e.name = "either or";
    e.owner = "GER";
    e.trigger = "both";
    Condition a;
    a.kind = "turn";
    a.op = ">=";
    a.value = "12";
    a.chain = "AND";
    Condition b;
    b.kind = "turn";
    b.op = "<=";
    b.value = "40";
    b.chain = "OR";  /* Open Doctrines has no boolean operators at all */
    e.conditions = {a, b};
    Action act;
    act.kind = "declare_war";
    act.target = "RUS";
    e.actions = {act};

    std::vector<ScriptSource> scripts;
    eventsToScripts({e}, scripts, report);
    REQUIRE(scripts.size() == 1);

    bool named = false;
    for (const auto& d : report.entries()) {
        if (d.code == "script.chain") named = true;
    }
    CHECK(named);
    /* The condition it could not gate on is still written down, as a comment,
     * so a map maker opening the script can see what was meant. */
    CHECK(scripts[0].text.find("# also required (OR)") != std::string::npos);
}

static void testUnsupportedActionBecomesAComment() {
    Report report;
    Event e;
    e.name = "spawn";
    e.owner = "GER";
    Action act;
    act.kind = "spawn_unit";
    act.target = "GER";
    e.actions = {act};

    std::vector<ScriptSource> scripts;
    eventsToScripts({e}, scripts, report);
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0].text.find("# unsupported action: spawn_unit") != std::string::npos);
    CHECK(report.hasWarnings());
}

/* A library has no engine header and must never be treated as an entry point,
 * or a map that crosses starts running code that was only ever included. */
static void testLibrariesAreNotEntryPoints() {
    Report report;
    std::vector<Event> events;
    std::vector<ScriptSource> scripts{
        script("lib.txt", "set country.GER.treasury 1\n", /*entry=*/false)};
    scriptsToEvents(scripts, "GER", events, report);
    CHECK(events.empty());
}

int main() {
    testStagesBecomeEvents();
    testEventsBecomeGd5Json();
    testGd5EventsBecomeScripts();
    testUnsupportedIsReportedNotGuessed();
    testNonAndChainIsReported();
    testUnsupportedActionBecomesAComment();
    testLibrariesAreNotEntryPoints();
    return check::finish("test_scripts");
}
