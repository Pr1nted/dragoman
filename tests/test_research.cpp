/* Research: what the two games agree on, and what is left alone.
 *
 * The mapping is lossy by design, so these check the properties that matter
 * rather than exact tables -- see docs/research.md. */
#include "Check.h"
#include "Fixture.h"

using namespace dragoman;

/* A cut-down stand-in for GD5's tech tree. Only `max_lvl` is read, and the
 * ceilings differ on purpose: the whole point is that a level is meaningless
 * without one. */
static Json techTree() {
    Json t = Json::object();
    const std::pair<const char*, int> ceilings[] = {
        {"factory", 25},         {"fuel_refining", 50},  {"resource_refining", 100},
        {"destroyer", 26},       {"submarine", 26},      {"aircraft_carrier", 17},
        {"artillery", 20},       {"general_recruitment", 50}, {"infantry_type", 101},
        /* The flags. GD5 really does give these a ceiling of 1. */
        {"basic_factory", 1},    {"bergius_process", 1}, {"battleship", 1},
        {"dreadnought", 1},      {"basic_recruitment", 1},
    };
    for (const auto& c : ceilings) t[c.first] = Json{{"max_lvl", c.second}};
    return t;
}

static bool has(const std::vector<std::string>& v, const char* id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

/* Half a ladder's ceiling is half the ladder, and it is a prefix: ind1..ind5
 * present, ind6..ind10 absent. Not "some five of the ten". */
static void testLevelsBecomeAPrefix() {
    /* 13 of 25 is just past half, and rounding up makes it six of ten. */
    const auto nodes = researchNodesFromGd5(Json{{"factory", 13}}, techTree());
    for (const char* id : {"ind1", "ind2", "ind3", "ind4", "ind5", "ind6"}) CHECK(has(nodes, id));
    for (const char* id : {"ind7", "ind8", "ind10"}) CHECK(!has(nodes, id));
}

/* A ceiling of 25 and a ceiling of 50 must place a nation at the same rung
 * when it is equally far along each -- otherwise the tech with the tallest
 * ceiling silently decides every ladder it appears in. */
static void testCeilingsAreRespected() {
    const auto by_factory = researchNodesFromGd5(Json{{"factory", 20}}, techTree());         /* 20/25 */
    const auto by_refining = researchNodesFromGd5(Json{{"fuel_refining", 40}}, techTree());  /* 40/50 */
    CHECK(by_factory == by_refining);
}

/* The bug this file was written after. `basic_factory` has a ceiling of 1: it
 * is held or not held, and read as a fraction it is 1.0. Letting it vote on
 * position carried every nation of a converted map to `ind10` off a single
 * unlock. It is worth the first rung -- the country has entered the field --
 * and not one more. */
static void testFlagsDoNotCarryALadder() {
    const auto unlock_only = researchNodesFromGd5(
        Json{{"basic_factory", 1}, {"bergius_process", 1}}, techTree());
    CHECK(has(unlock_only, "ind1"));
    for (const char* id : {"ind2", "ind5", "ind10"}) CHECK(!has(unlock_only, id));

    /* And it must not drag a graded tech's answer up either. */
    const auto with_level =
        researchNodesFromGd5(Json{{"factory", 5}, {"basic_factory", 1}}, techTree());
    CHECK(has(with_level, "ind2"));
    CHECK(!has(with_level, "ind3"));

    /* Same for the navy: `battleship` and `dreadnought` are flags too. */
    const auto navy = researchNodesFromGd5(Json{{"battleship", 1}, {"dreadnought", 1}}, techTree());
    CHECK(has(navy, "navy1"));
    CHECK(!has(navy, "navy2"));
}

/* Any progress at all earns the first rung. A country with one factory has
 * industry; recording none would be the larger error. */
static void testAnyProgressEarnsTheFirstRung() {
    const auto nodes = researchNodesFromGd5(Json{{"factory", 1}}, techTree());
    CHECK(has(nodes, "ind1"));
    CHECK(!has(nodes, "ind2"));
}

/* Zero is not "a little". An empty table yields nothing, and so does a table
 * of zeroes -- which is what several stock GD5 scenarios actually ship. */
static void testZeroYieldsNothing() {
    CHECK(researchNodesFromGd5(Json::object(), techTree()).empty());
    CHECK(researchNodesFromGd5(Json{{"factory", 0}, {"destroyer", 0}}, techTree()).empty());
}

/* Without a tech tree there is no ceiling to divide by. Treating the raw level
 * as a fraction would put every nation at the top of every ladder, so a tech
 * with no known ceiling does not vote at all. */
static void testUnknownCeilingDoesNotVote() {
    CHECK(researchNodesFromGd5(Json{{"factory", 13}}, Json::object()).empty());
    /* A tech absent from an otherwise valid tree is the same case, and note
     * that it is *not* the flag case above: a missing ceiling is a gap in what
     * we were handed, not a fact about the country, so it grants nothing --
     * not even the first rung. */
    Json partial = techTree();
    partial.erase("factory");
    CHECK(researchNodesFromGd5(Json{{"factory", 13}}, partial).empty());
}

/* GD5 has no fortification or port technology, so nothing may invent one --
 * neither from industry, which is the nearest thing, nor from anything else. */
static void testForticationAndPortsAreNeverInvented() {
    const auto nodes = researchNodesFromGd5(
        Json{{"factory", 25}, {"destroyer", 20}, {"artillery", 30}, {"infantry_type", 101}},
        techTree());
    for (const char* id : {"fort1", "fort6", "port1", "port3"}) CHECK(!has(nodes, id));
    /* The branching army nodes are excluded for the same reason: they sit in
     * mutex groups, and a prefix would grant two the game forbids together. */
    for (const char* id : {"professional_army", "combined_arms", "total_war"})
        CHECK(!has(nodes, id));
}

/* Any infantry at all is the root of the army tree, and it is the only thing
 * `infantry_type` contributes -- it must not drag a ladder up with it. */
static void testInfantryGivesTheArmyRoot() {
    const auto nodes = researchNodesFromGd5(Json{{"infantry_type", 91}}, techTree());
    CHECK(has(nodes, "basic_training"));
    CHECK(!has(nodes, "ind1"));
    CHECK(!has(nodes, "navy1"));
}

/* Nodes come out sorted and without repeats, so two maps with the same
 * research produce byte-identical files. Several techs feed one ladder, which
 * is where duplicates would come from. */
static void testOutputIsCanonical() {
    const auto nodes =
        researchNodesFromGd5(Json{{"factory", 25}, {"fuel_refining", 10}}, techTree());
    CHECK(std::is_sorted(nodes.begin(), nodes.end()));
    CHECK(std::adjacent_find(nodes.begin(), nodes.end()) == nodes.end());
}

/* Back the other way: a node list becomes levels, and those levels read back
 * as the same node list. The levels themselves are not preserved -- ten rungs
 * cannot hold twenty-five -- but the rung a nation stands on is, exactly, and
 * for every rung of every ladder. Rounding that gained a rung per crossing
 * would make a map creep up the tree each time it changed hands. */
static void testNodesSurviveTheReturnTrip() {
    const char* const ladders[][2] = {{"ind", "10"}, {"navy", "10"}, {"arty", "3"},
                                      {"conscript", "6"}};
    for (const auto& ladder : ladders) {
        const int length = std::atoi(ladder[1]);
        for (int rungs = 1; rungs <= length; ++rungs) {
            std::vector<std::string> nodes;
            for (int i = 1; i <= rungs; ++i) nodes.push_back(ladder[0] + std::to_string(i));

            const Json research = researchGd5FromNodes(nodes, techTree());
            const auto back = researchNodesFromGd5(research, techTree());

            for (int i = 1; i <= length; ++i) {
                const std::string id = ladder[0] + std::to_string(i);
                CHECK(has(back, id.c_str()) == (i <= rungs));
            }
        }
    }
}

/* Flags are the destination map's business. Writing them from a rung would
 * hand a country holding `navy1` a dreadnought. */
static void testWritingLeavesFlagsAlone() {
    const Json research = researchGd5FromNodes({"navy1", "navy2", "ind1"}, techTree());
    CHECK(research.contains("destroyer"));
    for (const char* id : {"battleship", "dreadnought", "basic_factory", "bergius_process"})
        CHECK(!research.contains(id));
}

/* Writing needs the ceilings as much as reading does: with no tree there is
 * nothing to scale against, and a table of raw rung counts would be wrong in
 * whichever installation read it. */
static void testWritingWithoutATreeYieldsNothing() {
    CHECK(researchGd5FromNodes({"ind1", "ind2"}, Json::object()).empty());
}

int main() {
    testLevelsBecomeAPrefix();
    testCeilingsAreRespected();
    testAnyProgressEarnsTheFirstRung();
    testZeroYieldsNothing();
    testUnknownCeilingDoesNotVote();
    testForticationAndPortsAreNeverInvented();
    testInfantryGivesTheArmyRoot();
    testOutputIsCanonical();
    testFlagsDoNotCarryALadder();
    testNodesSurviveTheReturnTrip();
    testWritingLeavesFlagsAlone();
    testWritingWithoutATreeYieldsNothing();
    return check::finish("test_research");
}
