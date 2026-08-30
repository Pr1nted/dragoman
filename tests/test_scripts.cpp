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
     * the script is a library that never executes. Version 2 since 0.4.1: the
     * generated body is still version 1 syntax, which version 2 accepts, but
     * declaring 1 pins a script the block editor may reopen to a dialect the
     * game is moving away from. */
    CHECK(startsWith(text, "#OD/MapEngine/2"));
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


/* ------------------------------------------------- engine version 2 */

/* Helper: translate one script and hand back the actions and the report. */
static void translateOne(const char* text, std::vector<Event>& events, Report& report) {
    std::vector<ScriptSource> scripts{script("v2.txt", text)};
    scriptsToEvents(scripts, "GER", events, report);
}

static const Action* firstAction(const std::vector<Event>& events) {
    for (const auto& e : events) {
        if (!e.actions.empty()) return &e.actions.front();
    }
    return nullptr;
}

/* Version 2 puts an operator between the reference and the value. Reading past
 * it was not a missing feature but a wrong answer: `set var.gold = 100` set the
 * variable to the literal string "=", silently, in a translation that
 * otherwise looked like it had worked. */
static void testAssignmentOperatorIsNotMistakenForTheValue() {
    Report report;
    std::vector<Event> events;
    translateOne("waitUntil map.turn >= 3\nset var.gold = 100\n", events, report);

    const Action* a = firstAction(events);
    REQUIRE(a != nullptr);
    CHECK_EQ(a->kind, std::string("set_var"));
    CHECK_EQ(a->target, std::string("gold"));
    CHECK_EQ(a->message, std::string("100"));
}

/* The same line written the way version 2 lets you write it. The engine
 * normalises this to a `set` before doing anything else and so does this
 * library, so the two forms have to reach the same place. */
static void testBareAssignmentIsTheSameAsSet() {
    Report withSet, without;
    std::vector<Event> a, b;
    translateOne("waitUntil map.turn >= 3\nset var.gold = 100\n", a, withSet);
    translateOne("waitUntil map.turn >= 3\nvar.gold = 100\n", b, without);

    const Action* x = firstAction(a);
    const Action* y = firstAction(b);
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    CHECK_EQ(x->kind, y->kind);
    CHECK_EQ(x->target, y->target);
    CHECK_EQ(x->message, y->message);
}

/* A quoted value survives the operator, spaces and all. */
static void testQuotedValueCrossesWithAnOperator() {
    Report report;
    std::vector<Event> events;
    translateOne("waitUntil map.turn >= 3\nset country.GER.name = \"German Empire\"\n",
                 events, report);
    const Action* a = firstAction(events);
    REQUIRE(a != nullptr);
    CHECK_EQ(a->kind, std::string("edit_name"));
    CHECK_EQ(a->message, std::string("German Empire"));
}

/* What GD5 cannot express is refused, and refused BY NAME. A report that says
 * "loops, conditionals or collections" when the script used `label` sends the
 * reader looking for a loop that is not there. */
static void testVersion2ConstructsAreRefusedByName() {
    struct Case { const char* line; const char* mentions; };
    const Case cases[] = {
        {"set var.gold += 100",      "compound assignment"},
        {"var.gold++",               "compound assignment"},
        {"set var.gold = var.x + 1", "arithmetic"},
        {"label start",              "label"},
        {"jump start",               "jump"},
        {"dialog bob \"hello\"",     "dialog"},
        {"print \"hi\"",             "print"},
        {"try",                      "try"},
        {"for i 1 10",               "for"},
        {"unless var.x == 1",        "unless"},
    };

    for (const auto& c : cases) {
        Report report;
        std::vector<Event> events;
        const std::string text = std::string("waitUntil map.turn >= 3\n") + c.line + "\n";
        translateOne(text.c_str(), events, report);

        /* Nothing is translated: a script that is refused is refused whole. */
        CHECK(events.empty());

        bool named = false;
        for (const auto& e : report.entries()) {
            if (e.message.find(c.mentions) != std::string::npos) named = true;
        }
        CHECK(named);
        if (!named) {
            std::fprintf(stderr, "       %s was not named in the report\n", c.line);
            for (const auto& e : report.entries()) {
                std::fprintf(stderr, "         got: %s\n", e.message.c_str());
            }
        }
    }
}

/* A generated script declares the engine it is meant for. */
static void testGeneratedScriptsDeclareVersion2() {
    Event e;
    e.name = "test";
    e.owner = "GER";
    Condition c;
    c.kind = "turn";
    c.op = ">=";
    c.value = "5";
    c.chain = "AND";
    e.conditions.push_back(c);

    Report report;
    std::vector<Event> events{e};
    std::vector<ScriptSource> scripts;
    eventsToScripts(events, scripts, report);
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0].text.rfind("#OD/MapEngine/2", 0) == 0);
}

int main() {
    testStagesBecomeEvents();
    testEventsBecomeGd5Json();
    testGd5EventsBecomeScripts();
    testUnsupportedIsReportedNotGuessed();
    testNonAndChainIsReported();
    testUnsupportedActionBecomesAComment();
    testLibrariesAreNotEntryPoints();
    testAssignmentOperatorIsNotMistakenForTheValue();
    testBareAssignmentIsTheSameAsSet();
    testQuotedValueCrossesWithAnOperator();
    testVersion2ConstructsAreRefusedByName();
    testGeneratedScriptsDeclareVersion2();
    return check::finish("test_scripts");
}
