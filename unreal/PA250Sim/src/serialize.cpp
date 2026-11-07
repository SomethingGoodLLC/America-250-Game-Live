// Scenario loading, canonical save format, state hash, and JSON bridges.
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "internal.hpp"
#include "nlohmann/json.hpp"

namespace pa250 {

using json = nlohmann::json;

namespace {

json pts_to_json(const std::vector<Point>& v) {
    json a = json::array();
    for (const auto& p : v) a.push_back(json::array({p.x, p.y}));
    return a;
}
std::vector<Point> pts_from_json(const json& a) {
    std::vector<Point> v;
    for (const auto& p : a) v.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
    return v;
}
json treaty_to_json(const Treaty& t) {
    return {{"type", t.type}, {"turns_remaining", t.turns_remaining}, {"army_id", t.army_id}, {"province_id", t.province_id}};
}
Treaty treaty_from_json(const json& j) {
    Treaty t;
    t.type = j.at("type").get<std::string>();
    t.turns_remaining = j.at("turns_remaining").get<int>();
    t.army_id = j.value("army_id", "");
    t.province_id = j.value("province_id", "");
    return t;
}
json army_to_json(const Army& a) {
    return {{"id", a.id}, {"name", a.name}, {"faction", a.faction}, {"province", a.province},
            {"previous_province", a.previous_province}, {"units", a.units}, {"morale", a.morale}};
}
Army army_from_json(const json& j) {
    Army a;
    a.id = j.at("id").get<std::string>();
    a.name = j.value("name", a.id);
    a.faction = j.at("faction").get<std::string>();
    a.province = j.at("province").get<std::string>();
    a.previous_province = j.value("previous_province", "");
    a.units = j.at("units").get<std::map<std::string, int>>();
    a.morale = j.value("morale", 70);
    return a;
}
json faction_to_json(const FactionInfo& f) {
    return {{"display_name", f.display_name}, {"envoy_name", f.envoy_name}, {"envoy_title", f.envoy_title},
            {"allowed_clause_types", f.allowed_clause_types}, {"protected_provinces", f.protected_provinces},
            {"max_ceasefire_turns", f.max_ceasefire_turns}, {"max_safe_passage_turns", f.max_safe_passage_turns}};
}
FactionInfo faction_from_json(const json& j) {
    FactionInfo f;
    f.display_name = j.value("display_name", "");
    f.envoy_name = j.value("envoy_name", "");
    f.envoy_title = j.value("envoy_title", "");
    f.allowed_clause_types = j.value("allowed_clause_types", std::vector<std::string>{});
    f.protected_provinces = j.value("protected_provinces", std::vector<std::string>{});
    f.max_ceasefire_turns = j.value("max_ceasefire_turns", 3);
    f.max_safe_passage_turns = j.value("max_safe_passage_turns", 3);
    return f;
}
json province_to_json(const Province& p) {
    return {{"id", p.id}, {"name", p.name}, {"owner", p.owner}, {"adjacent", p.adjacent},
            {"polygon", pts_to_json(p.polygon)}, {"label", json::array({p.label.x, p.label.y})},
            {"supply_value", p.supply_value}};
}
Province province_from_json(const json& j) {
    Province p;
    p.id = j.at("id").get<std::string>();
    p.name = j.value("name", p.id);
    p.owner = j.at("owner").get<std::string>();
    p.adjacent = j.at("adjacent").get<std::vector<std::string>>();
    std::sort(p.adjacent.begin(), p.adjacent.end());
    p.polygon = pts_from_json(j.value("polygon", json::array()));
    if (j.contains("label")) p.label = {j.at("label").at(0).get<double>(), j.at("label").at(1).get<double>()};
    p.supply_value = j.value("supply_value", 1);
    return p;
}

CampaignState state_from_json(const json& j, bool is_save) {
    CampaignState s;
    if (is_save && j.at("schema_version").get<int>() != kSchemaVersion) throw std::runtime_error("unsupported save version");
    s.scenario = j.value("scenario", "");
    s.seed = j.at("seed").get<uint64_t>();
    s.player_faction = j.value("player_faction", std::string(kContinental));
    s.ai_objective = j.value("ai_objective", "");
    s.turn = is_save ? j.at("turn").get<int>() : 0;
    for (const auto& p : j.at("provinces")) {
        Province pr = province_from_json(p);
        s.provinces[pr.id] = pr;
    }
    for (const auto& a : j.at("armies")) {
        Army ar = army_from_json(a);
        s.armies[ar.id] = ar;
    }
    for (auto it = j.at("factions").begin(); it != j.at("factions").end(); ++it) s.factions[it.key()] = faction_from_json(it.value());
    s.supplies = j.at("supplies").get<std::map<std::string, int>>();
    s.relations = j.at("relations").get<std::map<std::string, int>>();
    s.prisoners_held = j.at("prisoners_held").get<std::map<std::string, int>>();
    if (is_save) {
        for (const auto& t : j.at("treaties")) s.treaties.push_back(treaty_from_json(t));
        for (const auto& m : j.at("memory"))
            s.memory.push_back({m.at("turn").get<int>(), m.at("faction").get<std::string>(), m.at("text").get<std::string>()});
        const json& pb = j.at("pending_battle");
        s.pending_battle = {pb.at("active").get<bool>(), pb.at("province").get<std::string>(),
                            pb.at("attacker_army").get<std::string>(), pb.at("defender_army").get<std::string>()};
        s.log = j.at("log").get<std::vector<std::string>>();
    }
    // Integrity: adjacency symmetric and ids resolvable.
    for (const auto& kv : s.provinces)
        for (const auto& n : kv.second.adjacent) {
            auto it = s.provinces.find(n);
            if (it == s.provinces.end() || !detail::contains(it->second.adjacent, kv.first))
                throw std::runtime_error("asymmetric or unknown adjacency: " + kv.first + " -> " + n);
        }
    for (const auto& kv : s.armies)
        if (!s.provinces.count(kv.second.province)) throw std::runtime_error("army in unknown province: " + kv.first);
    return s;
}

}  // namespace

std::string read_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

CampaignState load_scenario(const std::string& text) {
    try {
        return state_from_json(json::parse(text), false);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("scenario parse error: ") + e.what());
    }
}

std::string serialize(const CampaignState& s) {
    json j;
    j["schema_version"] = kSchemaVersion;
    j["scenario"] = s.scenario;
    j["turn"] = s.turn;
    j["seed"] = s.seed;
    j["player_faction"] = s.player_faction;
    j["ai_objective"] = s.ai_objective;
    j["provinces"] = json::array();
    for (const auto& kv : s.provinces) j["provinces"].push_back(province_to_json(kv.second));
    j["armies"] = json::array();
    for (const auto& kv : s.armies) j["armies"].push_back(army_to_json(kv.second));
    j["factions"] = json::object();
    for (const auto& kv : s.factions) j["factions"][kv.first] = faction_to_json(kv.second);
    j["supplies"] = s.supplies;
    j["relations"] = s.relations;
    j["prisoners_held"] = s.prisoners_held;
    j["treaties"] = json::array();
    for (const auto& t : s.treaties) j["treaties"].push_back(treaty_to_json(t));
    j["memory"] = json::array();
    for (const auto& m : s.memory) j["memory"].push_back({{"turn", m.turn}, {"faction", m.faction}, {"text", m.text}});
    j["pending_battle"] = {{"active", s.pending_battle.active}, {"province", s.pending_battle.province},
                           {"attacker_army", s.pending_battle.attacker_army}, {"defender_army", s.pending_battle.defender_army}};
    j["log"] = s.log;
    return j.dump();
}

CampaignState deserialize(const std::string& text) {
    try {
        return state_from_json(json::parse(text), true);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("save parse error: ") + e.what());
    }
}

uint64_t state_hash(const CampaignState& s) { return detail::fnv1a(serialize(s)); }

std::string state_hash_hex(const CampaignState& s) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(state_hash(s)));
    return buf;
}

// ---- Diplomacy bridges ---------------------------------------------------
std::string context_to_json(const DiplomacyContext& c) {
    json j;
    j["faction"] = c.faction;
    j["counterpart"] = c.counterpart;
    j["turn"] = c.turn;
    j["relation"] = c.relation;
    j["allowed_clause_types"] = c.allowed_clause_types;
    j["limits"] = {{"max_ceasefire_turns", c.max_ceasefire_turns}, {"max_safe_passage_turns", c.max_safe_passage_turns}};
    j["protected_provinces"] = c.protected_provinces;
    j["provinces"] = c.provinces;
    j["armies"] = c.armies;
    j["prisoners_held"] = c.prisoners_held;
    j["treaties"] = json::array();
    for (const auto& t : c.treaties) j["treaties"].push_back(treaty_to_json(t));
    j["memory"] = c.memory;
    return j.dump();
}

DiplomacyContext context_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        DiplomacyContext c;
        c.faction = j.at("faction").get<std::string>();
        c.counterpart = j.at("counterpart").get<std::string>();
        c.turn = j.at("turn").get<int>();
        c.relation = j.at("relation").get<int>();
        c.allowed_clause_types = j.at("allowed_clause_types").get<std::vector<std::string>>();
        c.max_ceasefire_turns = j.at("limits").at("max_ceasefire_turns").get<int>();
        c.max_safe_passage_turns = j.at("limits").at("max_safe_passage_turns").get<int>();
        c.protected_provinces = j.at("protected_provinces").get<std::vector<std::string>>();
        c.provinces = j.at("provinces").get<std::map<std::string, std::string>>();
        c.armies = j.at("armies").get<std::map<std::string, std::string>>();
        c.prisoners_held = j.at("prisoners_held").get<std::map<std::string, int>>();
        for (const auto& t : j.at("treaties")) c.treaties.push_back(treaty_from_json(t));
        c.memory = j.value("memory", std::vector<std::string>{});
        return c;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("context parse error: ") + e.what());
    }
}

namespace {

json clause_to_json(const Clause& c) {
    json j{{"type", c.type}};
    if (c.type == "ceasefire") j["turns"] = c.turns;
    else if (c.type == "prisoner_exchange") j["counts"] = c.counts;
    else if (c.type == "safe_passage") j.update({{"army_id", c.army_id}, {"province_id", c.province_id}, {"turns", c.turns}});
    else if (c.type == "cede_province") j["province_id"] = c.province_id;
    return j;
}

// Integers must fit in int32: a silent narrowing (4294967299 -> 3) would let the sim accept a clause
// the Python validator rejects.
bool int_field(const json& v, int& out) {
    constexpr int64_t kMin = std::numeric_limits<int>::min();
    constexpr int64_t kMax = std::numeric_limits<int>::max();
    if (v.is_number_unsigned()) {
        if (v.get<uint64_t>() > static_cast<uint64_t>(kMax)) return false;
        out = static_cast<int>(v.get<uint64_t>());
        return true;
    }
    if (!v.is_number_integer()) return false;
    const int64_t n = v.get<int64_t>();
    if (n < kMin || n > kMax) return false;
    out = static_cast<int>(n);
    return true;
}

}  // namespace

std::string proposal_to_json(const DiplomacyProposal& p) {
    json j{{"spoken_line", p.spoken_line}, {"clauses", json::array()}};
    for (const auto& c : p.clauses) j["clauses"].push_back(clause_to_json(c));
    return j.dump();
}

std::vector<std::string> proposal_from_json(const std::string& text, DiplomacyProposal& out) {
    using detail::err;
    std::vector<std::string> errors;
    out = DiplomacyProposal{};
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {err("malformed_proposal", "not a JSON object")};
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.key() != "spoken_line" && it.key() != "clauses") errors.push_back(err("unexpected_field", it.key()));
    if (j.contains("spoken_line")) {
        if (!j["spoken_line"].is_string()) errors.push_back(err("malformed_proposal", "spoken_line"));
        else out.spoken_line = j["spoken_line"].get<std::string>();  // stored for display only
    }
    if (!j.contains("clauses")) return errors;
    if (!j["clauses"].is_array()) {
        errors.push_back(err("malformed_proposal", "clauses must be an array"));
        return errors;
    }
    for (const auto& cj : j["clauses"]) {
        Clause c;
        if (!cj.is_object() || !cj.contains("type") || !cj["type"].is_string()) {
            errors.push_back(err("malformed_clause", "missing type"));
            continue;
        }
        c.type = cj["type"].get<std::string>();
        static const std::map<std::string, std::vector<std::string>> kFields = {
            {"ceasefire", {"type", "turns"}},
            {"prisoner_exchange", {"type", "counts"}},
            {"safe_passage", {"type", "army_id", "province_id", "turns"}},
            {"cede_province", {"type", "province_id"}}};
        auto kf = kFields.find(c.type);
        for (auto it = cj.begin(); it != cj.end(); ++it) {
            const std::string& k = it.key();
            if (kf != kFields.end() && !detail::contains(kf->second, k)) {
                errors.push_back(err("unexpected_field", c.type + "." + k));
                continue;
            }
            if (k == "turns") {
                if (!int_field(it.value(), c.turns)) errors.push_back(err("malformed_clause", "turns"));
            } else if (k == "army_id" || k == "province_id") {
                if (!it.value().is_string()) errors.push_back(err("malformed_clause", k));
                else (k == "army_id" ? c.army_id : c.province_id) = it.value().get<std::string>();
            } else if (k == "counts") {
                c.has_counts = true;
                if (!it.value().is_object()) {
                    errors.push_back(err("malformed_clause", "counts"));
                    continue;
                }
                for (auto ci = it.value().begin(); ci != it.value().end(); ++ci) {
                    int n = 0;
                    if (!int_field(ci.value(), n)) errors.push_back(err("malformed_clause", "counts." + ci.key()));
                    else c.counts[ci.key()] = n;
                }
            }
        }
        out.clauses.push_back(c);
    }
    return errors;
}

// ---- Battle bridges ------------------------------------------------------
std::string setup_to_json(const BattleSetup& b) {
    json j;
    j["valid"] = b.valid;
    j["province_id"] = b.province_id;
    j["province_name"] = b.province_name;
    j["objective"] = b.objective;
    j["seed"] = b.seed;
    j["attacker_faction"] = b.attacker_faction;
    j["defender_faction"] = b.defender_faction;
    j["player_faction"] = b.player_faction;
    j["sides"] = json::object();
    for (const auto& kv : b.sides)
        j["sides"][kv.first] = {{"faction", kv.second.faction}, {"army_id", kv.second.army_id},
                                {"tickets", kv.second.tickets}, {"morale", kv.second.morale},
                                {"retreat_province", kv.second.retreat_province}};
    return j.dump();
}

BattleSetup setup_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        BattleSetup b;
        b.valid = j.at("valid").get<bool>();
        b.province_id = j.at("province_id").get<std::string>();
        b.province_name = j.at("province_name").get<std::string>();
        b.objective = j.at("objective").get<std::string>();
        b.seed = j.at("seed").get<uint64_t>();
        b.attacker_faction = j.at("attacker_faction").get<std::string>();
        b.defender_faction = j.at("defender_faction").get<std::string>();
        b.player_faction = j.at("player_faction").get<std::string>();
        for (auto it = j.at("sides").begin(); it != j.at("sides").end(); ++it) {
            SideSetup s;
            s.faction = it.value().at("faction").get<std::string>();
            s.army_id = it.value().at("army_id").get<std::string>();
            s.tickets = it.value().at("tickets").get<std::map<std::string, int>>();
            s.morale = it.value().at("morale").get<int>();
            s.retreat_province = it.value().at("retreat_province").get<std::string>();
            b.sides[it.key()] = s;
        }
        return b;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("setup parse error: ") + e.what());
    }
}

std::string result_to_json(const BattleResult& r) {
    json j{{"province_id", r.province_id}, {"winner", r.winner}, {"objective_holder", r.objective_holder},
           {"losses", r.losses}, {"retreat_province", r.retreat_province}};
    return j.dump();
}

BattleResult result_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        BattleResult r;
        r.province_id = j.at("province_id").get<std::string>();
        r.winner = j.at("winner").get<std::string>();
        r.objective_holder = j.value("objective_holder", "");
        r.losses = j.value("losses", std::map<std::string, std::map<std::string, int>>{});
        r.retreat_province = j.value("retreat_province", "");
        return r;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("result parse error: ") + e.what());
    }
}

}  // namespace pa250
