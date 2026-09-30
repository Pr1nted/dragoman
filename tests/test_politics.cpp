/* The political axis, guarantees and truces: three things that could not cross
 * until Greater Diplomacy 5 grew a field for them.
 *
 * The axis is the delicate one. Both games measure the same thing on scales a
 * factor of ten apart, and Open Doctrines stores the NEGATION of it in memory
 * -- its loader flips both compass axes on the way in, having once shipped
 * without doing so and loaded the Soviet Union as hard right. This library
 * reads the file, not the struct, so no flip belongs here; these pin that down
 * so a later reader cannot "fix" it back.
 */
#include <dragoman/dragoman.h>

#include <filesystem>
#include <string>
#include <vector>

#include "Check.h"
#include "Fixture.h"
#include "Support.h"

using namespace dragoman;
namespace fs = std::filesystem;

/* Sign and scale, stated once, so the two conversions cannot drift apart
 * quietly. Positive is authoritarian on BOTH sides. */
static void testTheAxisKeepsItsSignAndScale() {
    const std::pair<double, int> cases[] = {
        {100, 10}, {89, 9}, {30, 3}, {0, 0}, {-20, -2}, {-89, -9}, {-100, -10},
    };
    for (const auto& c : cases) {
        CHECK_EQ(gd5PoliticalValueFromAxis(c.first), c.second);
    }
    /* And back, exactly, for anything GD5 could have said. */
    for (int v = -10; v <= 10; ++v) {
        CHECK_EQ(gd5PoliticalValueFromAxis(axisFromGd5PoliticalValue(v)), v);
    }
    /* Out of range is clamped rather than wrapped. */
    CHECK_EQ(gd5PoliticalValueFromAxis(500.0), 10);
    CHECK_EQ(gd5PoliticalValueFromAxis(-500.0), -10);
}

static Nation* nationNamed(World& w, const std::string& key) {
    for (auto& n : w.nations) {
        if (n.key == key) return &n;
    }
    return nullptr;
}

/* A country's position survives the trip out and home, EXACTLY, including the
 * nine values in ten that GD5's coarser scale cannot represent. */
static void testTheAxisComesHomeUnchanged() {
    World w = fixture::makeWorld();
    REQUIRE(!w.nations.empty());
    /* 89 is the case that matters: it is not a multiple of ten, so a naive
     * round trip returns 90. */
    const double values[] = {89.0, -20.0, 0.0, 100.0, -96.0};
    size_t i = 0;
    for (auto& n : w.nations) {
        n.political_axis = values[i % (sizeof(values) / sizeof(values[0]))];
        n.has_political_axis = true;
        ++i;
    }

    Options opt;
    Report report;
    const std::string odmap = fixture::scratch("politics.odmap");
    const std::string gd5 = fixture::scratch("politics-gd5");
    const std::string back = fixture::scratch("politics-back.odmap");
    fs::remove_all(gd5);
    REQUIRE(writeOdMap(odmap, w, opt, report));

    World crossing;
    Report r2;
    REQUIRE(readOdMap(odmap, opt, crossing, r2));
    REQUIRE(writeGd5Map(gd5, crossing, opt, r2));

    World home;
    Report r3;
    REQUIRE(readGd5Map(gd5, opt, home, r3));
    REQUIRE(writeOdMap(back, home, opt, r3));

    World returned;
    Report r4;
    REQUIRE(readOdMap(back, opt, returned, r4));

    for (const auto& original : w.nations) {
        const Nation* got = nationNamed(returned, original.key);
        if (!got) continue;
        CHECK(got->has_political_axis);
        CHECK_EQ(got->political_axis, original.political_axis);
    }
}

/* ...unless somebody moved it over there, in which case their value is the
 * true one and the remembered original must not overwrite it. */
static void testAValueChangedInGd5Wins() {
    World w = fixture::makeWorld();
    REQUIRE(!w.nations.empty());
    for (auto& n : w.nations) {
        n.political_axis = 89.0;
        n.has_political_axis = true;
    }

    Options opt;
    Report report;
    const std::string odmap = fixture::scratch("politics-changed.odmap");
    const std::string gd5 = fixture::scratch("politics-changed-gd5");
    fs::remove_all(gd5);
    REQUIRE(writeOdMap(odmap, w, opt, report));

    World crossing;
    Report r2;
    REQUIRE(readOdMap(odmap, opt, crossing, r2));
    REQUIRE(writeGd5Map(gd5, crossing, opt, r2));

    World home;
    Report r3;
    REQUIRE(readGd5Map(gd5, opt, home, r3));
    /* GD5's own doing: the player moved the country the other way. */
    for (auto& n : home.nations) {
        n.political_axis = -60.0;
        n.has_political_axis = true;
    }

    const std::string back = fixture::scratch("politics-changed-back.odmap");
    Report r4;
    REQUIRE(writeOdMap(back, home, opt, r4));

    World returned;
    Report r5;
    REQUIRE(readOdMap(back, opt, returned, r5));
    for (const auto& n : returned.nations) {
        if (!n.has_political_axis) continue;
        CHECK_EQ(n.political_axis, -60.0);
    }
}

/* A guarantee belongs to the guarantor on both sides, so it must not come home
 * pointing the other way. */
static void testAGuaranteeKeepsItsDirection() {
    World w = fixture::makeWorld();
    REQUIRE(w.nations.size() >= 2);
    const std::string a = w.nations[0].key;
    const std::string b = w.nations[1].key;
    w.nations[0].relations[b].guarantee = true;
    w.nations[0].relations[b].truce = true;

    Options opt;
    Report report;
    const std::string odmap = fixture::scratch("guarantee.odmap");
    const std::string gd5 = fixture::scratch("guarantee-gd5");
    const std::string back = fixture::scratch("guarantee-back.odmap");
    fs::remove_all(gd5);
    REQUIRE(writeOdMap(odmap, w, opt, report));

    World crossing;
    Report r2;
    REQUIRE(readOdMap(odmap, opt, crossing, r2));
    REQUIRE(writeGd5Map(gd5, crossing, opt, r2));

    World home;
    Report r3;
    REQUIRE(readGd5Map(gd5, opt, home, r3));
    REQUIRE(writeOdMap(back, home, opt, r3));

    World returned;
    Report r4;
    REQUIRE(readOdMap(back, opt, returned, r4));

    const Nation* guarantor = nationNamed(returned, a);
    const Nation* target = nationNamed(returned, b);
    REQUIRE(guarantor != nullptr);
    REQUIRE(target != nullptr);

    const auto r = guarantor->relations.find(b);
    REQUIRE(r != guarantor->relations.end());
    CHECK(r->second.guarantee);
    CHECK(r->second.truce);

    /* And the other way round is NOT asserted: a guarantee is one country's
     * promise, not a pair's. */
    const auto back_r = target->relations.find(a);
    if (back_r != target->relations.end()) {
        CHECK(!back_r->second.guarantee);
    }
}

/* A non-aggression pact still has no counterpart, and is refused out loud
 * rather than written as a truce -- a truce expires and a pact does not. */
static void testANonAggressionPactIsReportedNotInvented() {
    World w = fixture::makeWorld();
    REQUIRE(w.nations.size() >= 2);
    w.nations[0].relations[w.nations[1].key].non_aggression = true;

    Options opt;
    Report report;
    const std::string gd5 = fixture::scratch("nap-gd5");
    fs::remove_all(gd5);
    REQUIRE(writeGd5Map(gd5, w, opt, report));

    bool said = false;
    for (const auto& e : report.entries()) {
        if (e.message.find("non-aggression") != std::string::npos) said = true;
    }
    CHECK(said);

    /* Not smuggled in as a truce. */
    World read;
    Report r2;
    REQUIRE(readGd5Map(gd5, opt, read, r2));
    for (const auto& n : read.nations) {
        for (const auto& kv : n.relations) {
            CHECK(!kv.second.truce);
        }
    }
}


/* A war written the way the GAME writes it is a war this library reads.
 *
 * Open Doctrines' relations.json uses "war"; this read "atWar" and nothing
 * else, so every war in every shipped map was dropped on the way in -- thirty
 * in the world map, Ukraine-Russia and India-Pakistan among them -- and no
 * conversion said so, because a relation that is never read cannot be reported
 * as lost. Found by comparing a returned archive against the one that set out
 * rather than by any test passing or failing.
 */
static void testAWarWrittenTheGamesWayIsRead() {
    World w = fixture::makeWorld();
    REQUIRE(w.nations.size() >= 2);
    const std::string a = w.nations[0].key;
    const std::string b = w.nations[1].key;

    Options opt;
    Report report;
    const std::string odmap = fixture::scratch("war-key.odmap");
    REQUIRE(writeOdMap(odmap, w, opt, report));

    /* Rewrite relations.json the way the game does, with "war". */
    Zip zip;
    std::string err;
    REQUIRE(readZip(odmap, zip, err));
    Json rel = Json::object();
    rel[a] = Json{{b, Json{{"war", true}}}};
    rel[b] = Json{{a, Json{{"war", true}}}};
    zip.putText("relations.json", rel.dump());
    REQUIRE(writeZip(odmap, zip, err));

    World read;
    Report r2;
    REQUIRE(readOdMap(odmap, opt, read, r2));
    const Nation* n = nationNamed(read, a);
    REQUIRE(n != nullptr);
    const auto it = n->relations.find(b);
    REQUIRE(it != n->relations.end());
    CHECK(it->second.at_war);

    /* And it goes back out under the key the game reads. */
    const std::string again = fixture::scratch("war-key-out.odmap");
    Report r3;
    REQUIRE(writeOdMap(again, read, opt, r3));
    Zip back;
    REQUIRE(readZip(again, back, err));
    const Json written = Json::parse(back.text("relations.json"), nullptr, false);
    REQUIRE(!written.is_discarded());
    /* REQUIRE, not CHECK: indexing a key that is not there aborts on
     * nlohmann's assert, which reports a crash instead of a failed test. */
    REQUIRE(written.contains(a));
    REQUIRE(written[a].contains(b));
    CHECK(written[a][b].value("war", false));
}

int main() {
    testTheAxisKeepsItsSignAndScale();
    testTheAxisComesHomeUnchanged();
    testAValueChangedInGd5Wins();
    testAGuaranteeKeepsItsDirection();
    testANonAggressionPactIsReportedNotInvented();
    testAWarWrittenTheGamesWayIsRead();
    return check::finish("test_politics");
}
