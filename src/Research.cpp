/* Research, between a tree of named nodes and a table of levelled techs.
 *
 * Open Doctrines has 78 research nodes, built in C++ in Game_Research.cpp, and
 * a country either has a node or does not. Greater Diplomacy 5 has 35
 * technologies, each with a level and a ceiling. Neither is a relabelling of
 * the other, so only the part that genuinely corresponds is translated.
 *
 * What corresponds is the numbered ladders. `ind1`..`ind10` is a scale of
 * industry and GD5's factory and refining techs are a scale of the same thing;
 * `navy1`..`navy10` and GD5's destroyer, submarine and carrier lines likewise.
 * A ladder is taken as a prefix -- ind4 means ind1 through ind4 -- which is
 * exactly how the dependencies in Game_Research.cpp read.
 *
 * What is deliberately NOT translated:
 *
 *   - `fort1`..`fort6` and `port1`..`port3`. GD5 has no fortification or port
 *     technology at all, so any value here would be invented.
 *   - The named army nodes -- professional_army, combined_arms, total_war and
 *     the rest. They are not a ladder: they branch, and several sit in mutex
 *     groups where researching one forecloses another. Taking a prefix of a
 *     list like that would hand a country two nodes the game says it may not
 *     hold together.
 *
 * The mapping is lossy in both directions and makes no pretence otherwise. It
 * is not what a round trip relies on: the exact GD5 table rides in the sidecar
 * and is restored from there, so this file's output is the *interoperable*
 * form, for a game or a person reading the map on the other side.
 */
#include "Formats.h"

#include <algorithm>
#include <cmath>

namespace dragoman {

namespace {

/* One Open Doctrines ladder and the GD5 technologies that measure the same
 * thing. Progress along the ladder is the furthest any of them has got, as a
 * fraction of its own ceiling -- GD5 tech ceilings run from 1 to 101, so a raw
 * level means nothing without it. */
struct Ladder {
    const char* nodes[11];   /* prefix order, nullptr-terminated */
    const char* techs[6];    /* nullptr-terminated */
};

const Ladder kLadders[] = {
    {{"ind1", "ind2", "ind3", "ind4", "ind5", "ind6", "ind7", "ind8", "ind9", "ind10", nullptr},
     {"factory", "fuel_refining", "resource_refining", "bergius_process", "basic_factory",
      nullptr}},

    {{"navy1", "navy2", "navy3", "navy4", "navy5", "navy6", "navy7", "navy8", "navy9", "navy10",
      nullptr},
     {"destroyer", "submarine", "aircraft_carrier", "battleship", "dreadnought", nullptr}},

    /* arty4 onwards splits into a and b variants, so the ladder stops where it
     * stops being one. */
    {{"arty1", "arty2", "arty3", nullptr},
     {"artillery", nullptr}},

    {{"conscript1", "conscript2", "conscript3", "conscript4", "conscript5", "conscript6", nullptr},
     {"general_recruitment", "recruitment_buildings", "basic_recruitment", nullptr}},
};

/* The root of the army tree. Open Doctrines gives it to every country that has
 * anything at all, so a nation with any infantry at all gets it here too. */
const char* kArmyRoot = "basic_training";
const char* kArmyRootTechs[] = {"infantry_type", "militia", nullptr};

double ceilingOf(const Json& ceilings, const char* tech) {
    if (!ceilings.is_object() || !ceilings.contains(tech) || !ceilings[tech].is_object()) return 0.0;
    return ceilings[tech].value("max_lvl", 0.0);
}

double levelOf(const Json& research, const char* tech) {
    if (!research.contains(tech) || !research[tech].is_number()) return 0.0;
    return research[tech].get<double>();
}

/* How far along its own scale a technology has got, or -1 for one that has no
 * scale to be along.
 *
 * Two kinds of technology answer to that. A ceiling of 1 is not a ladder, it
 * is a flag: `basic_factory`, `battleship`, `dreadnought`, `bergius_process`
 * and `basic_recruitment` are all held or not held. Reading such a flag as a
 * fraction makes it 1.0, and a single unlock would then carry a country to the
 * top of a ten-rung ladder -- which is exactly what it did, and is why every
 * nation of a converted map came back with `ind1` through `ind10`. An unknown
 * ceiling is the other kind: without the tech tree there is nothing to divide
 * by, and treating the raw level as a fraction would put everything at the top
 * of everything. Neither gets a vote on position. */
double fractionOf(const Json& research, const Json& ceilings, const char* tech) {
    const double ceiling = ceilingOf(ceilings, tech);
    /* Nothing known about the technology at all, which is not the same as a
     * flag: a flag is a fact about the country, an absent ceiling is a gap in
     * what we were given, and a gap must not grant anything. */
    if (ceiling <= 0.0) return 0.0;
    if (ceiling <= 1.0) return -1.0;
    return std::min(1.0, std::max(0.0, levelOf(research, tech)) / ceiling);
}

}  // namespace

std::vector<std::string> researchNodesFromGd5(const Json& research, const Json& tech_tree) {
    std::vector<std::string> nodes;
    if (!research.is_object() || research.empty()) return nodes;

    for (const auto& ladder : kLadders) {
        double best = 0.0;
        bool unlocked = false;
        for (int t = 0; ladder.techs[t]; ++t) {
            const double fraction = fractionOf(research, tech_tree, ladder.techs[t]);
            if (fraction < 0.0) {
                /* A flag says the country has entered this field and nothing
                 * more, which is worth the first rung and not the ladder. */
                unlocked = unlocked || levelOf(research, ladder.techs[t]) > 0.0;
                continue;
            }
            best = std::max(best, fraction);
        }
        if (best <= 0.0 && !unlocked) continue;

        int length = 0;
        while (ladder.nodes[length]) ++length;
        /* Rounded up, so any progress at all earns the first rung: a country
         * with one factory has industry, and saying it has none would be the
         * larger error. */
        int reached = std::min(length, static_cast<int>(std::ceil(best * length)));
        if (unlocked) reached = std::max(reached, 1);
        for (int i = 0; i < reached; ++i) nodes.push_back(ladder.nodes[i]);
    }

    for (int t = 0; kArmyRootTechs[t]; ++t) {
        if (levelOf(research, kArmyRootTechs[t]) > 0.0) {
            nodes.push_back(kArmyRoot);
            break;
        }
    }

    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    return nodes;
}

/* Note what this does not write: `basic_training`. Open Doctrines gives the
 * root of the army tree to every country it loads, so its presence separates
 * nobody from anybody and there is nothing for it to say about infantry_type
 * on the other side. */
Json researchGd5FromNodes(const std::vector<std::string>& nodes, const Json& tech_tree) {
    Json research = Json::object();
    if (nodes.empty() || !tech_tree.is_object()) return research;

    const auto has = [&nodes](const char* id) {
        return std::find(nodes.begin(), nodes.end(), id) != nodes.end();
    };

    for (const auto& ladder : kLadders) {
        int length = 0, reached = 0;
        while (ladder.nodes[length]) {
            if (has(ladder.nodes[length])) reached = length + 1;
            ++length;
        }
        if (reached == 0) continue;
        const double fraction = static_cast<double>(reached) / length;

        for (int t = 0; ladder.techs[t]; ++t) {
            const char* tech = ladder.techs[t];
            /* Flags are left exactly as the destination map has them. One bit
             * cannot be scaled to a rung, and guessing it either invents a
             * late-game unlock -- `dreadnought` for a country holding navy1 --
             * or withholds an early one. */
            const double ceiling = ceilingOf(tech_tree, tech);
            if (ceiling <= 1.0) continue;
            /* Rounded down, because reading rounds up: floor on the way out
             * and ceil on the way back land on the rung the country started
             * on, where rounding both ways would gain it one per crossing. */
            research[tech] = static_cast<int>(std::floor(fraction * ceiling));
        }
    }
    return research;
}

}  // namespace dragoman
