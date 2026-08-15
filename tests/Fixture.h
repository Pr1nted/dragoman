/* A small map that exercises every field the model has.
 *
 * Built in memory rather than committed as a file, because the two real map
 * formats belong to their own projects and neither is this repository's to
 * redistribute. It is deliberately awkward in the places that have broken
 * things: a province whose centroid falls outside itself, a province touching
 * the wrap seam, a nation with no ISO code of its own, a sea province, and a
 * script with a stage the other game cannot express.
 */
#ifndef DRAGOMAN_TEST_FIXTURE_H
#define DRAGOMAN_TEST_FIXTURE_H

#include <filesystem>
#include <string>

#include "Formats.h"

namespace fixture {

using namespace dragoman;

inline std::string scratch(const std::string& name) {
    const std::filesystem::path dir =
        std::filesystem::path(DRAGOMAN_BINARY_DIR) / "test-scratch";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

/* 64x32. Province 1 is open sea covering the top half; 2 and 3 are land side
 * by side; 4 is a crescent wrapped round 3, so its centroid lands in 3 and the
 * centre finder has to go looking; 5 straddles the left and right edges, so it
 * is its own neighbour across the seam unless the wrap is handled. */
inline World makeWorld() {
    World w;
    w.name = "Powder Keg Test";
    w.description = "A small map built by the test suite.";
    w.author = "dragoman tests";
    w.license = "CC-BY-4.0";
    w.date.year = 1914;
    w.date.month = 7;
    w.date.day = 1;
    w.width = 64;
    w.height = 32;
    w.raster.assign(static_cast<size_t>(w.width) * w.height, 0);

    auto put = [&w](int x, int y, uint32_t id) {
        w.raster[static_cast<size_t>(y) * w.width + x] = id;
    };

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (y < 10) { put(x, y, 1); continue; }              // sea
            if (x >= 2 && x < 20) { put(x, y, 2); continue; }    // a plain block
            if (x >= 24 && x < 40 && y >= 14 && y < 24) { put(x, y, 3); continue; }
            if (x >= 22 && x < 42 && y >= 12 && y < 26) { put(x, y, 4); continue; }  // crescent
            if (x < 2 || x >= 62) { put(x, y, 5); continue; }    // across the seam
            put(x, y, 6);
        }
    }

    auto province = [&](int64_t id, const char* name, const char* owner, bool sea) {
        Province p;
        p.id = id;
        p.name = name;
        p.owner = owner;
        p.is_sea = sea;
        p.population = id * 100000;
        p.industry = static_cast<int>(id % 4);
        p.fortification = static_cast<int>(id % 3);
        p.port_level = (id == 2) ? 1 : 0;
        if (!sea) {
            Resource gold;
            gold.a = 9.6 + id;
            gold.b = 3.8;
            p.resources["gold"] = gold;
        }
        w.provinces.push_back(p);
    };
    province(1, "North Sea", "", true);
    province(2, "Rhineland", "GER", false);
    province(3, "Bohemia", "AUH", false);
    province(4, "Galicia", "AUH", false);
    province(5, "Kamchatka", "RUS", false);
    province(6, "Steppe", "RUS", false);

    auto nation = [&](const char* key, const char* name, uint32_t color, double treasury) {
        Nation n;
        n.key = key;
        n.name = name;
        n.color = color;
        n.treasury = treasury;
        n.leader_name = "";
        w.nations.push_back(n);
    };
    nation("GER", "German Empire", 0x8bc64b, 123.2);
    nation("AUH", "Austria-Hungary", 0xc6a44b, 57.21);
    nation("RUS", "Russian Empire", 0x4b7cc6, 88.0);
    /* No ISO code of its own, so a crossing has to invent one and remember it. */
    nation("FIU", "Free Imperial City of Ulm", 0xcccccc, 1.0);

    w.findNation("GER")->relations["AUH"].ally = true;
    w.findNation("GER")->relations["RUS"].non_aggression = true;
    w.findNation("AUH")->relations["GER"].ally = true;
    w.findNation("RUS")->relations["GER"].at_war = true;

    w.findNation("GER")->claims.push_back(3);
    w.findProvince(3)->cores.push_back("GER");

    ScriptSource s;
    s.name = "opening.txt";
    s.entrypoint = true;
    s.text =
        "#OD/MapEngine/1\n"
        "# the July crisis\n"
        "set country.GER.treasury 5000\n"
        "waitUntil map.turn >= 12\n"
        "set country.GER.at_war_with RUS true\n"
        "set province.3.owner GER\n";
    w.scripts.push_back(s);

    w.origin = 1;
    return w;
}

}  // namespace fixture

#endif
