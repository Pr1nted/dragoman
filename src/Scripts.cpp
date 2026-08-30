/* Translating between two scripting systems that are not the same kind of
 * thing.
 *
 * Open Doctrines' is imperative and line-based. A script runs top to bottom
 * when the map loads, and `waitUntil` suspends it until a condition holds, so
 * a script is really a sequence of stages each gated by one comparison. It
 * also has loops, nested conditionals, arrays and linked lists.
 *
 * GD5's is declarative. A nation owns a list of events, each a set of
 * conditions joined by AND/OR/XOR/NOR/NAND and a set of actions, and every
 * event is tested once a turn.
 *
 * The overlap is exactly the shape both can express: a gate, and some things
 * that happen when it opens. An Open Doctrines script written as top-level
 * `waitUntil` stages maps onto GD5 events one stage at a time, and a GD5
 * event maps back onto a script with one `waitUntil` and some `set` lines.
 * Everything outside that overlap -- a `foreach` over a country's provinces,
 * an event whose conditions are chained with XOR -- is reported by name and
 * left alone rather than half-translated. The original text and the original
 * event list both ride in the sidecar, so what cannot cross still comes back.
 */
#include "Formats.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace dragoman {

namespace {

/* `country.USA.treasury` -> {"country", "USA", "treasury"}. */
std::vector<std::string> dotted(const std::string& token) {
    return splitOn(token, '.');
}

std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') { inQuotes = !inQuotes; cur.push_back(c); continue; }
        if (!inQuotes && (c == ' ' || c == '\t')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

bool isComparison(const std::string& s) {
    return s == "==" || s == "!=" || s == ">" || s == "<" || s == ">=" || s == "<=";
}

/* The block openers Open Doctrines has and GD5 has no way to express. */
bool opensBlock(const std::string& head) {
    return head == "if" || head == "foreach" || head == "while" || head == "for"
        || head == "repeat" || head == "unless" || head == "try";
}

/* The statements Open Doctrines' engine version 2 added, named so a script
 * that uses one is reported as what it is rather than as "loops, conditionals
 * or collections" -- which was the whole of the language when this was
 * written, and is now a guess that is usually wrong. Kept in the engine's own
 * order (ScriptEngine.cpp, isVersion2Statement) to make the two easy to
 * compare when it gains another. */
bool isVersion2Statement(const std::string& kw) {
    static const char* kV2[] = {"for", "repeat", "break", "continue", "print", "elseif",
                                "unless", "label", "jump", "spawn", "stop",
                                "try", "catch", "endtry", "dialog"};
    for (const char* k : kV2) {
        if (kw == k) return true;
    }
    return false;
}

/* A reference: a name, then any of name characters and dots. ASCII only, on
 * purpose -- the engine's own version of this uses <cctype>, which answers
 * according to the caller's locale, and that is the bug test_locale exists to
 * stop this library repeating. */
bool looksLikeRef(const std::string& r) {
    if (r.empty()) return false;
    if (!(asciiAlpha(static_cast<unsigned char>(r[0])) || r[0] == '_')) return false;
    for (char c : r) {
        if (!(asciiAlnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return true;
}

/* Open Doctrines version 2 lets an assignment be written the way C would:
 * `var.gold = 100`, `var.gold += 100`, `var.i++`, `++var.i`. The engine
 * normalises all of them to a `set` line before doing anything else, and so
 * does its linter and its block editor -- one normaliser, three callers. This
 * is a fourth, and it has to agree with them or a script that the game runs
 * one way is translated another.
 *
 * Deliberately a mirror rather than a shared implementation: this library
 * carries no code from either game. See docs/scripting.md for what that costs
 * and how the two are kept level. */
std::string normaliseAssignment(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) return raw;

    /* ++ref / --ref */
    if (line.size() > 2 && (line.compare(0, 2, "++") == 0 || line.compare(0, 2, "--") == 0)) {
        const std::string ref = line.substr(2);
        if (looksLikeRef(ref)) return "set " + ref + (line[0] == '+' ? " += 1" : " -= 1");
        return raw;
    }
    /* ref++ / ref-- */
    if (line.size() > 2 && (line.compare(line.size() - 2, 2, "++") == 0 ||
                            line.compare(line.size() - 2, 2, "--") == 0)) {
        const std::string ref = line.substr(0, line.size() - 2);
        if (looksLikeRef(ref)) {
            return "set " + ref + (line[line.size() - 2] == '+' ? " += 1" : " -= 1");
        }
        return raw;
    }

    /* ref = expr, and the compound forms. `set` is optional on these. */
    const size_t sp = line.find_first_of(" \t=+-*/");
    if (sp == std::string::npos || sp == 0) return raw;
    const std::string head = line.substr(0, sp);
    if (!looksLikeRef(head)) return raw;
    const size_t o = line.find_first_not_of(" \t", sp);
    if (o == std::string::npos) return raw;
    static const char* kOps[] = {"+=", "-=", "*=", "/=", "="};
    for (const char* op : kOps) {
        const size_t len = std::strlen(op);
        if (line.compare(o, len, op) != 0) continue;
        /* `==` is a comparison and belongs to a condition, not an assignment. */
        if (op[0] == '=' && line.compare(o, 2, "==") == 0) return raw;
        return "set " + head + " " + line.substr(o);
    }
    return raw;
}

/* Is this the whole of a value, or the beginning of a sum?
 *
 * `set var.gold = 100` is the version 1 line spelled differently and crosses
 * unchanged. `set var.gold = var.x + 1` is arithmetic, and a GD5 event sets a
 * variable rather than computing one, so it does not. Telling them apart is
 * the difference between translating a script and inventing one. */
bool isPlainValue(const std::vector<std::string>& rhs) {
    if (rhs.empty()) return false;
    if (rhs.size() == 1) return true;
    /* A quoted string is one value however many spaces are inside it, and
     * words() keeps the quotes on. */
    return rhs.front().size() >= 2 && rhs.front().front() == '"' && rhs.back().back() == '"';
}

}  // namespace

/* ------------------------------------------- Open Doctrines script -> events */

/* Reads one Open Doctrines script and appends the events it is equivalent to.
 * Returns false when the script uses something outside the shared subset, in
 * which case nothing is appended and the caller reports it. */
static bool odScriptToEvents(const ScriptSource& src, const std::string& default_owner,
                             std::vector<Event>& out, Report& report) {
    std::vector<Event> staged;
    Event current;
    current.name = src.name;
    current.owner = default_owner;
    current.trigger = "both";
    current.fire_once = true;

    bool sawUnsupported = false;
    /* What made it unsupported, for the report. Empty until something does. */
    std::string unsupportedKind;
    int  stage = 0;

    for (const std::string& raw : splitLines(src.text)) {
        /* Normalised first, exactly as the engine does, so `var.gold = 100`
         * and `set var.gold = 100` are one case here as they are there. */
        const std::string line = normaliseAssignment(trim(raw));
        if (line.empty() || line[0] == '#') continue;

        const std::vector<std::string> w = words(line);
        if (w.empty()) continue;
        const std::string& head = w[0];

        if (head == "include") {
            report.warn("script.include",
                        src.name + " uses `include`, which GD5 has no counterpart for; the "
                                   "included library is carried unchanged but not inlined");
            continue;
        }

        if (opensBlock(head) || head == "else" || head == "endif" || head == "next"
            || head == "endwhile" || head == "elseif" || head == "catch"
            || head == "endtry" || head == "array" || head == "list") {
            if (unsupportedKind.empty()) {
                unsupportedKind = (head == "array" || head == "list")
                                      ? "collections (`" + head + "`)"
                                      : "a `" + head + "` block";
            }
            sawUnsupported = true;
            continue;
        }

        /* The rest of version 2's statements. None has a GD5 counterpart --
         * its events are a gate and a list of actions, with no control flow to
         * jump around inside -- so they are named and the script is carried. */
        if (isVersion2Statement(head)) {
            if (unsupportedKind.empty()) unsupportedKind = "`" + head + "`";
            sawUnsupported = true;
            continue;
        }

        if (head == "waitUntil" || startsWith(head, "waitUntil(")) {
            /* A stage boundary. Whatever was collected belongs to the stage
             * that just ended; the new gate opens the next one. */
            if (!current.actions.empty() || stage == 0) {
                staged.push_back(current);
            }
            current = Event();
            current.name = src.name + " stage " + std::to_string(++stage);
            current.owner = default_owner;
            current.trigger = "both";
            current.fire_once = true;

            /* `waitUntil(x >= 1)` and `waitUntil x >= 1` are both accepted by
             * the engine, so the parentheses are stripped before splitting. */
            std::string condText = trim(line.substr(head == "waitUntil" ? 9 : 9));
            if (!condText.empty() && condText.front() == '(') condText = condText.substr(1);
            if (!condText.empty() && condText.back() == ')') condText.pop_back();

            const std::vector<std::string> cw = words(condText);
            Condition c;
            c.chain = "AND";
            if (cw.size() >= 3 && isComparison(cw[1])) {
                const std::vector<std::string> lhs = dotted(cw[0]);
                c.op = cw[1];
                c.value = unquote(cw[2]);
                if (lhs.size() == 2 && lhs[0] == "map" && lhs[1] == "turn") {
                    c.kind = "turn";
                } else if (lhs.size() == 2 && lhs[0] == "map" && lhs[1] == "date") {
                    c.kind = "date";
                } else if (lhs.size() == 2 && lhs[0] == "var") {
                    c.kind = "variable";
                    c.subject = lhs[1];
                } else if (lhs.size() == 3 && lhs[0] == "country") {
                    c.kind = "country." + lhs[2];
                    c.subject = lhs[1];
                } else {
                    c.kind = "unsupported";
                    c.value = condText;
                }
            } else if (cw.size() == 2) {
                /* `waitUntil country.USA.at_war_with RUS` -- a bare predicate. */
                const std::vector<std::string> lhs = dotted(cw[0]);
                if (lhs.size() == 3 && lhs[0] == "country") {
                    c.kind = "country." + lhs[2];
                    c.subject = lhs[1];
                    c.op = "==";
                    c.value = cw[1];
                } else {
                    c.kind = "unsupported";
                    c.value = condText;
                }
            } else {
                c.kind = "unsupported";
                c.value = condText;
            }
            current.conditions.push_back(c);
            continue;
        }

        if (head == "set" && w.size() >= 3) {
            /* Version 2 puts an operator between the reference and the value.
             * Reading past it is not a missing feature but a wrong answer: the
             * third word used to be the value, and taking it now assigns the
             * literal string "=" to the variable, with no warning, in a
             * translation that otherwise looks like it worked. */
            std::vector<std::string> v(w.begin(), w.end());
            {
                static const char* kCompound[] = {"+=", "-=", "*=", "/="};
                bool compound = false;
                for (const char* op : kCompound) {
                    if (v[2] == op) { compound = true; break; }
                }
                if (compound) {
                    /* A GD5 event sets a variable; it cannot fold one against
                     * what is already there. */
                    unsupportedKind = "compound assignment (" + v[2] + ")";
                    sawUnsupported = true;
                    continue;
                }
                if (v[2] == "=") {
                    const std::vector<std::string> rhs(v.begin() + 3, v.end());
                    if (!isPlainValue(rhs)) {
                        unsupportedKind = "an arithmetic assignment";
                        sawUnsupported = true;
                        continue;
                    }
                    /* Drop the operator and the line is the version 1 form. */
                    v.erase(v.begin() + 2);
                }
            }
            const std::vector<std::string>& w = v;
            const std::vector<std::string> lhs = dotted(w[1]);
            Action a;
            if (lhs.size() == 3 && lhs[0] == "country") {
                const std::string& field = lhs[2];
                a.target = w.size() >= 3 ? w[2] : "";
                const bool on = w.size() < 4 || toLower(w[3]) != "false";
                if (field == "at_war_with") {
                    a.kind = on ? "declare_war" : "ceasefire";
                    current.owner = lhs[1];
                } else if (field == "allied_with") {
                    a.kind = on ? "join_faction" : "leave_faction";
                    current.owner = lhs[1];
                } else if (field == "treasury") {
                    a.kind = "set_treasury";
                    a.target = lhs[1];
                    a.message = w[2];
                } else if (field == "name") {
                    a.kind = "edit_name";
                    a.target = lhs[1];
                    a.message = unquote(w[2]);
                } else {
                    a.kind = "unsupported";
                    a.message = line;
                }
            } else if (lhs.size() == 3 && lhs[0] == "province") {
                if (lhs[2] == "owner") {
                    a.kind = "give_territory";
                    a.target = w[2];
                    a.message = lhs[1];
                } else {
                    a.kind = "unsupported";
                    a.message = line;
                }
            } else if (lhs.size() == 2 && lhs[0] == "var") {
                a.kind = "set_var";
                a.target = lhs[1];
                a.message = unquote(w[2]);
            } else if (lhs.size() == 2 && lhs[0] == "map" && lhs[1] == "date") {
                a.kind = "unsupported";
                a.message = line;
            } else {
                a.kind = "unsupported";
                a.message = line;
            }
            current.actions.push_back(a);
            continue;
        }

        if (unsupportedKind.empty()) unsupportedKind = "`" + head + "`";
        sawUnsupported = true;
    }

    if (!current.actions.empty()) staged.push_back(current);

    if (sawUnsupported) {
        report.warn("script.unsupported",
                    src.name + " uses " +
                        (unsupportedKind.empty() ? std::string("something") : unsupportedKind) +
                        ", which GD5's event system cannot express; it was not translated and "
                        "is carried unchanged in the sidecar instead");
        return false;
    }

    for (auto& e : staged) {
        if (e.actions.empty() && e.conditions.empty()) continue;
        out.push_back(std::move(e));
    }
    return true;
}

void scriptsToEvents(const std::vector<ScriptSource>& scripts, const std::string& default_owner,
                     std::vector<Event>& out, Report& report) {
    for (const auto& s : scripts) {
        if (!s.entrypoint) continue;  /* libraries only run when included */
        odScriptToEvents(s, default_owner, out, report);
    }
}

/* ------------------------------------------------------ events -> GD5 JSON */

namespace {

/* The model's canonical condition names, in the words GD5's editor writes. */
std::string gd5ConditionType(const Condition& c, bool& supported) {
    supported = true;
    /* A condition GD5 has and this library does not model was read under its
     * own name with a "gd5:" prefix, and is written straight back out. That
     * keeps a GD5 -> Open Doctrines -> GD5 trip lossless for conditions this
     * code has never heard of, including any the game gains after it was
     * written -- and there are thirty of them, from "Occupying All Cores Of"
     * to "Is Faction Leader". Only the Open Doctrines side has to understand a
     * condition to express it; the GD5 side only has to carry it. */
    if (startsWith(c.kind, "gd5:")) return c.kind.substr(4);
    if (c.kind == "turn") return "Turn Number";
    if (c.kind == "variable") return "Variable";
    if (c.kind == "country.at_war_with") return "At War With";
    if (c.kind == "country.allied_with") return "In Faction With";
    if (c.kind == "at_war_with") return "At War With";
    if (c.kind == "in_faction_with") return "In Faction With";
    if (c.kind == "country_exists") return "Country Exists";
    if (c.kind == "always") return "True";
    supported = false;
    return std::string();
}

std::string gd5ActionType(const Action& a, bool& supported) {
    supported = true;
    if (startsWith(a.kind, "gd5:")) return a.kind.substr(4);
    if (a.kind == "declare_war") return "Declare War";
    if (a.kind == "ceasefire") return "Send Ceasefire";
    if (a.kind == "join_faction") return "Join Faction";
    if (a.kind == "create_faction") return "Create Faction";
    if (a.kind == "invite_faction") return "Invite to Faction";
    if (a.kind == "give_territory") return "Give Territory";
    if (a.kind == "set_var") return "Set Variable";
    if (a.kind == "edit_name") return "Edit Name";
    if (a.kind == "message") return "Send Custom Message";
    if (a.kind == "spawn_unit") return "Spawn Unit";
    supported = false;
    return std::string();
}

}  // namespace

void eventsToGd5(const std::vector<Event>& events,
                 const std::map<std::string, std::string>& key_to_name,
                 Json& nation_data, Report& report) {
    auto nameOf = [&](const std::string& key) -> std::string {
        const auto it = key_to_name.find(key);
        return it != key_to_name.end() ? it->second : key;
    };

    int dropped = 0;
    for (const auto& e : events) {
        const std::string owner = nameOf(e.owner);
        if (!nation_data.contains(owner)) {
            report.warn("script.owner",
                        "an event belongs to \"" + e.owner
                            + "\", which is not a nation on this map; it was not written");
            continue;
        }

        Json conditions = Json::array();
        bool ok = true;
        for (const auto& c : e.conditions) {
            bool supported = false;
            const std::string type = gd5ConditionType(c, supported);
            if (!supported) {
                report.warn("script.condition",
                            "the condition \"" + c.kind + "\" in event \"" + e.name
                                + "\" has no GD5 counterpart; the event was not written");
                ok = false;
                break;
            }
            Json cond = Json::object();
            cond["type"] = type;
            cond["operator"] = c.op.empty() ? "==" : c.op;
            cond["value"] = c.value;
            cond["chain"] = c.chain.empty() ? "AND" : c.chain;
            if (type == "Variable") cond["variable"] = c.subject;
            conditions.push_back(cond);
        }
        if (!ok) { ++dropped; continue; }

        Json actions = Json::array();
        for (const auto& a : e.actions) {
            bool supported = false;
            const std::string type = gd5ActionType(a, supported);
            if (!supported) {
                report.warn("script.action",
                            "the action \"" + a.kind + "\" in event \"" + e.name
                                + "\" has no GD5 counterpart and was left out of that event");
                continue;
            }
            Json act = Json::object();
            act["type"] = type;
            act["target"] = a.kind == "give_territory" ? nameOf(a.target)
                            : a.kind == "set_var"      ? a.target
                                                       : nameOf(a.target);
            if (!a.message.empty()) act["message"] = a.message;
            for (auto p = a.params.begin(); p != a.params.end(); ++p) act[p.key()] = p.value();
            actions.push_back(act);
        }
        if (actions.empty()) { ++dropped; continue; }

        Json evt = Json::object();
        evt["name"] = e.name;
        evt["trigger_type"] = e.trigger == "ai" ? "AI Only"
                              : e.trigger == "player" ? "Player Only"
                                                      : "Both";
        evt["fire_once"] = e.fire_once;
        evt["conditions"] = conditions;
        evt["actions"] = actions;
        nation_data[owner]["scripted_events"].push_back(evt);
    }

    if (dropped > 0) {
        report.warn("script.dropped",
                    std::to_string(dropped)
                        + " event(s) could not be expressed as GD5 scripted events; the original "
                          "scripts are carried in the sidecar and return on the way back");
    }
}

/* ------------------------------------------------------ GD5 JSON -> events */

void eventsFromGd5(const Json& raw, const std::string& owner, std::vector<Event>& out,
                   Report& report) {
    if (!raw.is_array()) return;
    for (const auto& r : raw) {
        Event e;
        e.owner = owner;
        e.name = r.value("name", std::string("event"));
        e.fire_once = r.value("fire_once", true);
        const std::string trig = r.value("trigger_type", std::string("AI Only"));
        e.trigger = trig == "AI Only" ? "ai" : trig == "Player Only" ? "player" : "both";

        /* GD5 still reads maps written before conditions became a list, where
         * an event carried one condition inline. Both shapes are accepted here
         * for the same reason its own loader accepts both. */
        Json conditions = Json::array();
        if (r.contains("conditions") && r["conditions"].is_array()) {
            conditions = r["conditions"];
        } else if (r.contains("condition_type")) {
            conditions.push_back(Json{{"type", r["condition_type"]},
                                      {"operator", "=="},
                                      {"value", r.value("condition_val", std::string())},
                                      {"chain", "AND"}});
        }

        for (const auto& c : conditions) {
            Condition cond;
            const std::string type = c.value("type", std::string());
            if (type == "Turn Number") cond.kind = "turn";
            else if (type == "Variable") { cond.kind = "variable"; cond.subject = c.value("variable", std::string()); }
            else if (type == "At War With") cond.kind = "at_war_with";
            else if (type == "In Faction With") cond.kind = "in_faction_with";
            else if (type == "Country Exists") cond.kind = "country_exists";
            else if (type == "True") cond.kind = "always";
            else {
                cond.kind = "gd5:" + type;
                report.warn("script.condition",
                            "the GD5 condition \"" + type + "\" in event \"" + e.name
                                + "\" has no Open Doctrines counterpart; the event is carried in "
                                  "the sidecar but not written as a script");
            }
            cond.op = c.value("operator", std::string("=="));
            cond.value = c.value("value", std::string());
            cond.chain = c.value("chain", std::string("AND"));
            e.conditions.push_back(cond);
        }

        Json actions = Json::array();
        if (r.contains("actions") && r["actions"].is_array()) actions = r["actions"];
        else if (r.contains("action_type")) {
            actions.push_back(Json{{"type", r["action_type"]},
                                   {"target", r.value("action_target", std::string("None"))}});
        }

        for (const auto& a : actions) {
            Action act;
            const std::string type = a.value("type", std::string());
            if (type == "Declare War") act.kind = "declare_war";
            else if (type == "Send Ceasefire") act.kind = "ceasefire";
            else if (type == "Join Faction") act.kind = "join_faction";
            else if (type == "Create Faction") act.kind = "create_faction";
            else if (type == "Invite to Faction") act.kind = "invite_faction";
            else if (type == "Give Territory") act.kind = "give_territory";
            else if (type == "Set Variable") act.kind = "set_var";
            else if (type == "Edit Name") act.kind = "edit_name";
            else if (type == "Send Custom Message") act.kind = "message";
            else if (type == "Spawn Unit") act.kind = "spawn_unit";
            else act.kind = "gd5:" + type;
            act.target = a.value("target", std::string());
            act.message = a.value("message", std::string());
            for (auto f = a.begin(); f != a.end(); ++f) {
                if (f.key() != "type" && f.key() != "target" && f.key() != "message") {
                    act.params[f.key()] = f.value();
                }
            }
            e.actions.push_back(act);
        }

        out.push_back(std::move(e));
    }
}

/* ------------------------------------- events -> Open Doctrines script text */

std::string eventToOdScript(const Event& e, Report& report) {
    /* The engine version a generated script declares. Only version 1 syntax is
 * written -- `waitUntil` and `set ref value`, which version 2 still accepts --
 * but declaring 1 asks the game to run it under a dialect the game is moving
 * away from, and a script the block editor may reopen should not be pinned to
 * the old one. */
    std::string out = "#OD/MapEngine/2\n";
    out += "# " + e.name + "\n";
    out += "# Translated from a Greater Diplomacy 5 scripted event owned by " + e.owner + ".\n";

    /* Open Doctrines allows one comparison per condition and no boolean
     * operators, but consecutive `waitUntil`s are gates in sequence, which is
     * an AND over time. That covers AND chains exactly. It covers nothing
     * else, so the rest is said plainly in a comment rather than guessed at. */
    bool complex = false;
    for (size_t i = 1; i < e.conditions.size(); ++i) {
        const std::string& chain = e.conditions[i].chain;
        if (!chain.empty() && chain != "AND") complex = true;
    }
    if (complex) {
        report.warn("script.chain",
                    "event \"" + e.name
                        + "\" chains its conditions with something other than AND, which the Open "
                          "Doctrines script language cannot express; only the first condition was "
                          "written as a gate and the rest are left as comments");
    }

    bool wroteGate = false;
    for (size_t i = 0; i < e.conditions.size(); ++i) {
        const Condition& c = e.conditions[i];
        const bool usable = !complex || i == 0;
        std::string expr;
        if (c.kind == "turn") expr = "map.turn " + (c.op.empty() ? "==" : c.op) + " " + c.value;
        else if (c.kind == "variable") expr = "var." + c.subject + " " + (c.op.empty() ? "==" : c.op) + " " + c.value;
        else if (c.kind == "at_war_with") expr = "country." + e.owner + ".at_war_with " + c.value;
        else if (c.kind == "in_faction_with") expr = "country." + e.owner + ".allied_with " + c.value;
        else if (c.kind == "always") continue;

        if (expr.empty()) {
            out += "# unsupported condition: " + c.kind + " " + c.op + " " + c.value + "\n";
            continue;
        }
        if (usable) {
            out += "waitUntil " + expr + "\n";
            wroteGate = true;
        } else {
            out += "# also required (" + c.chain + "): " + expr + "\n";
        }
    }
    if (!wroteGate) out += "# fires as soon as the map loads\n";

    for (const auto& a : e.actions) {
        if (a.kind == "declare_war") {
            out += "set country." + e.owner + ".at_war_with " + a.target + " true\n";
        } else if (a.kind == "ceasefire") {
            out += "set country." + e.owner + ".at_war_with " + a.target + " false\n";
        } else if (a.kind == "join_faction" || a.kind == "invite_faction"
                   || a.kind == "create_faction") {
            out += "set country." + e.owner + ".allied_with " + a.target + " true\n";
        } else if (a.kind == "give_territory") {
            for (const auto& id : splitOn(a.message, ',')) {
                const std::string t = trim(id);
                if (!t.empty()) out += "set province." + t + ".owner " + a.target + "\n";
            }
        } else if (a.kind == "set_var") {
            out += "set var." + a.target + " " + a.message + "\n";
        } else {
            out += "# unsupported action: " + a.kind
                   + (a.target.empty() ? "" : " -> " + a.target) + "\n";
            report.warn("script.action",
                        "the GD5 action \"" + a.kind + "\" in event \"" + e.name
                            + "\" has no Open Doctrines counterpart; it was written as a comment "
                              "and preserved in the sidecar");
        }
    }
    return out;
}

void eventsToScripts(const std::vector<Event>& events, std::vector<ScriptSource>& out,
                     Report& report) {
    int index = 0;
    for (const auto& e : events) {
        ScriptSource s;
        s.name = "gd5_event_" + std::to_string(++index) + ".txt";
        s.text = eventToOdScript(e, report);
        s.entrypoint = true;
        out.push_back(std::move(s));
    }
    if (!events.empty()) {
        report.info("script.translated",
                    "translated " + std::to_string(events.size())
                        + " GD5 scripted event(s) into Open Doctrines entry scripts");
    }
}

}  // namespace dragoman
