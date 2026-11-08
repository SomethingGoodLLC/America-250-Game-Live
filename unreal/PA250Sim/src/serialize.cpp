// Scenario loading, canonical save format, state hash, and JSON bridges.
#include <deque>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include "internal.hpp"
#include "nlohmann/json.hpp"

namespace pa250 {

using json = nlohmann::json;

void rebuild_adjacency(CampaignState& s) {
    for (auto& kv : s.provinces) kv.second.adjacent.clear();
    for (const auto& ek : s.edges) {
        const Edge& e = ek.second;
        if (!s.provinces.count(e.a) || !s.provinces.count(e.b)) continue;
        s.provinces[e.a].adjacent.push_back(e.b);
        s.provinces[e.b].adjacent.push_back(e.a);
    }
    for (auto& kv : s.provinces) {
        auto& adj = kv.second.adjacent;
        std::sort(adj.begin(), adj.end());
        adj.erase(std::unique(adj.begin(), adj.end()), adj.end());
    }
}

std::vector<std::string> validate_scenario_graph(const CampaignState& s) {
    std::vector<std::string> errors;
    std::set<int> indices;
    int max_idx = 0;
    for (const auto& kv : s.provinces) {
        if (kv.second.map_index < 1) errors.push_back("map_index < 1 for " + kv.first);
        if (!indices.insert(kv.second.map_index).second) errors.push_back("duplicate map_index " + kv.first);
        max_idx = std::max(max_idx, kv.second.map_index);
    }
    if (static_cast<int>(indices.size()) != static_cast<int>(s.provinces.size()))
        errors.push_back("map_index count mismatch");
    for (int i = 1; i <= max_idx; ++i)
        if (!indices.count(i)) errors.push_back("map_index gap at " + std::to_string(i));
    if (max_idx != static_cast<int>(s.provinces.size())) errors.push_back("map_index not contiguous 1..N");

    std::set<std::string> edge_ids;
    for (const auto& ek : s.edges) {
        const Edge& e = ek.second;
        if (e.id != ek.first) errors.push_back("edge key/id mismatch " + ek.first);
        if (!edge_ids.insert(e.id).second) errors.push_back("duplicate edge id " + e.id);
        if (!s.provinces.count(e.a) || !s.provinces.count(e.b))
            errors.push_back("edge endpoint missing: " + e.id);
    }

    // Connectivity via BFS on adjacency.
    if (!s.provinces.empty()) {
        std::string start = s.provinces.begin()->first;
        std::set<std::string> seen{start};
        std::deque<std::string> q{start};
        while (!q.empty()) {
            std::string cur = q.front();
            q.pop_front();
            for (const auto& n : s.provinces.at(cur).adjacent)
                if (seen.insert(n).second) q.push_back(n);
        }
        if (seen.size() != s.provinces.size()) errors.push_back("province graph not connected");
    }
    return errors;
}

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
    return {{"type", t.type},
            {"hours_remaining", t.hours_remaining},
            {"army_id", t.army_id},
            {"province_id", t.province_id}};
}
Treaty treaty_from_json(const json& j) {
    Treaty t;
    t.type = j.at("type").get<std::string>();
    t.hours_remaining = j.at("hours_remaining").get<int>();
    t.army_id = j.value("army_id", "");
    t.province_id = j.value("province_id", "");
    return t;
}
json army_to_json(const Army& a) {
    return {{"id", a.id},
            {"name", a.name},
            {"faction", a.faction},
            {"province", a.province},
            {"previous_province", a.previous_province},
            {"units", a.units},
            {"morale", a.morale},
            {"path", a.path},
            {"hours_into_edge", a.hours_into_edge}};
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
    a.path = j.value("path", std::vector<std::string>{});
    a.hours_into_edge = j.value("hours_into_edge", 0);
    return a;
}
json faction_to_json(const FactionInfo& f) {
    return {{"display_name", f.display_name},
            {"envoy_name", f.envoy_name},
            {"envoy_title", f.envoy_title},
            {"allowed_clause_types", f.allowed_clause_types},
            {"protected_provinces", f.protected_provinces},
            {"max_ceasefire_days", f.max_ceasefire_days},
            {"max_safe_passage_days", f.max_safe_passage_days},
            {"controls_harbor", f.controls_harbor},
            {"envoy_loyalty", f.envoy_loyalty},
            {"envoy_defected", f.envoy_defected},
            {"envoy_title_granted", f.envoy_title_granted},
            {"grievance", f.grievance},
            {"snub_until_hour", f.snub_until_hour}};
}
FactionInfo faction_from_json(const json& j) {
    FactionInfo f;
    f.display_name = j.value("display_name", "");
    f.envoy_name = j.value("envoy_name", "");
    f.envoy_title = j.value("envoy_title", "");
    f.allowed_clause_types = j.value("allowed_clause_types", std::vector<std::string>{});
    f.protected_provinces = j.value("protected_provinces", std::vector<std::string>{});
    f.max_ceasefire_days = j.value("max_ceasefire_days", j.value("max_ceasefire_turns", 365));
    f.max_safe_passage_days = j.value("max_safe_passage_days", j.value("max_safe_passage_turns", 90));
    f.controls_harbor = j.value("controls_harbor", false);
    f.envoy_loyalty = j.value("envoy_loyalty", 0);
    f.envoy_defected = j.value("envoy_defected", false);
    f.envoy_title_granted = j.value("envoy_title_granted", "");
    f.grievance = j.value("grievance", 0);
    f.snub_until_hour = j.value("snub_until_hour", int64_t{0});
    return f;
}
json province_to_json(const Province& p) {
    return {{"id", p.id},
            {"name", p.name},
            {"owner", p.owner},
            {"map_index", p.map_index},
            {"terrain", p.terrain},
            {"adjacent", p.adjacent},
            {"polygon", pts_to_json(p.polygon)},
            {"label", json::array({p.label.x, p.label.y})},
            {"supply_value", p.supply_value}};
}
Province province_from_json(const json& j) {
    Province p;
    p.id = j.at("id").get<std::string>();
    p.name = j.value("name", p.id);
    p.owner = j.at("owner").get<std::string>();
    p.map_index = j.value("map_index", 0);
    p.terrain = j.value("terrain", std::vector<std::string>{});
    p.adjacent = j.value("adjacent", std::vector<std::string>{});
    std::sort(p.adjacent.begin(), p.adjacent.end());
    p.polygon = pts_from_json(j.value("polygon", json::array()));
    if (j.contains("label")) p.label = {j.at("label").at(0).get<double>(), j.at("label").at(1).get<double>()};
    p.supply_value = j.value("supply_value", 1);
    return p;
}
Edge edge_from_json(const json& j) {
    Edge e;
    e.id = j.at("id").get<std::string>();
    e.a = j.at("a").get<std::string>();
    e.b = j.at("b").get<std::string>();
    e.type = j.at("type").get<std::string>();
    e.march_hours = j.value("march_hours", 1);
    e.can_dismantle = j.value("can_dismantle", false);
    e.note = j.value("note", "");
    return e;
}
json edge_to_json(const Edge& e) {
    json j{{"id", e.id}, {"a", e.a}, {"b", e.b}, {"type", e.type}, {"march_hours", e.march_hours}};
    if (e.can_dismantle) j["can_dismantle"] = true;
    if (!e.note.empty()) j["note"] = e.note;
    return j;
}
json command_to_json(const Command& c) {
    return {{"apply_on_hour", c.apply_on_hour},
            {"sequence", c.sequence},
            {"kind", static_cast<int>(c.payload.kind)},
            {"army_id", c.payload.army_id},
            {"dest_province", c.payload.dest_province},
            {"edge_path", c.payload.edge_path},
            {"result_json", c.payload.result_json}};
}
Command command_from_json(const json& j) {
    Command c;
    c.apply_on_hour = j.at("apply_on_hour").get<int64_t>();
    c.sequence = j.at("sequence").get<uint64_t>();
    c.payload.kind = static_cast<CommandKind>(j.at("kind").get<int>());
    c.payload.army_id = j.value("army_id", "");
    c.payload.dest_province = j.value("dest_province", "");
    c.payload.edge_path = j.value("edge_path", std::vector<std::string>{});
    c.payload.result_json = j.value("result_json", "");
    return c;
}

CampaignState state_from_json(const json& j, bool is_save) {
    CampaignState s;
    if (is_save) {
        const int ver = j.at("schema_version").get<int>();
        if (ver != kSchemaVersion)
            throw std::runtime_error("unsupported save version " + std::to_string(ver) +
                                     " (need " + std::to_string(kSchemaVersion) +
                                     "); turn-based v1 saves cannot be loaded");
    }
    s.scenario = j.value("scenario", "");
    s.seed = j.at("seed").get<uint64_t>();
    s.player_faction = j.value("player_faction", std::string(kContinental));
    s.ai_objective = j.value("ai_objective", "");
    s.march_hours_per_day = j.value("march_hours_per_day", kDefaultMarchHoursPerDay);
    if (j.contains("now")) {
        s.now.hour_index = j.at("now").at("hour_index").get<int64_t>();
    } else if (j.contains("start_ymdh")) {
        const json& y = j.at("start_ymdh");
        s.now = from_ymdh(YMDH{y.at("y").get<int32_t>(), static_cast<uint8_t>(y.at("m").get<int>()),
                               static_cast<uint8_t>(y.at("d").get<int>()),
                               static_cast<uint8_t>(y.value("h", 0))});
    } else if (!is_save) {
        s.now = from_ymdh(YMDH{1775, 4, 18, 20});
    }
    for (const auto& p : j.at("provinces")) {
        Province pr = province_from_json(p);
        s.provinces[pr.id] = pr;
    }
    if (j.contains("edges")) {
        for (const auto& e : j.at("edges")) {
            Edge ed = edge_from_json(e);
            s.edges[ed.id] = ed;
        }
    }
    rebuild_adjacency(s);
    for (const auto& a : j.at("armies")) {
        Army ar = army_from_json(a);
        s.armies[ar.id] = ar;
    }
    for (auto it = j.at("factions").begin(); it != j.at("factions").end(); ++it)
        s.factions[it.key()] = faction_from_json(it.value());
    s.supplies = j.at("supplies").get<std::map<std::string, int>>();
    s.relations = j.at("relations").get<std::map<std::string, int>>();
    s.prisoners_held = j.at("prisoners_held").get<std::map<std::string, int>>();
    s.player_gold = j.value("player_gold", 500);
    s.player_vassal = j.value("player_vassal", false);
    s.player_title = j.value("player_title", "");
    s.fiefs = j.value("fiefs", std::map<std::string, std::string>{});
    if (is_save) {
        for (const auto& t : j.at("treaties")) s.treaties.push_back(treaty_from_json(t));
        for (const auto& m : j.at("memory"))
            s.memory.push_back({m.at("hour_index").get<int64_t>(), m.at("faction").get<std::string>(),
                                m.at("text").get<std::string>()});
        const json& pb = j.at("pending_battle");
        s.pending_battle = {pb.at("active").get<bool>(), pb.at("province").get<std::string>(),
                            pb.at("attacker_army").get<std::string>(), pb.at("defender_army").get<std::string>()};
        s.log = j.at("log").get<std::vector<std::string>>();
        s.next_command_seq = j.value("next_command_seq", 1);
        if (j.contains("pending_commands"))
            for (const auto& c : j.at("pending_commands")) s.pending_commands.push_back(command_from_json(c));
    }
    for (const auto& kv : s.armies)
        if (!s.provinces.count(kv.second.province)) throw std::runtime_error("army in unknown province: " + kv.first);
    auto gerr = validate_scenario_graph(s);
    if (!gerr.empty() && !s.edges.empty()) throw std::runtime_error("scenario graph invalid: " + gerr.front());
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
    j["now"] = {{"hour_index", s.now.hour_index}};
    j["seed"] = s.seed;
    j["player_faction"] = s.player_faction;
    j["ai_objective"] = s.ai_objective;
    j["march_hours_per_day"] = s.march_hours_per_day;
    j["provinces"] = json::array();
    for (const auto& kv : s.provinces) j["provinces"].push_back(province_to_json(kv.second));
    j["edges"] = json::array();
    for (const auto& kv : s.edges) j["edges"].push_back(edge_to_json(kv.second));
    j["armies"] = json::array();
    for (const auto& kv : s.armies) j["armies"].push_back(army_to_json(kv.second));
    j["factions"] = json::object();
    for (const auto& kv : s.factions) j["factions"][kv.first] = faction_to_json(kv.second);
    j["supplies"] = s.supplies;
    j["relations"] = s.relations;
    j["prisoners_held"] = s.prisoners_held;
    j["player_gold"] = s.player_gold;
    j["player_vassal"] = s.player_vassal;
    j["player_title"] = s.player_title;
    j["fiefs"] = s.fiefs;
    j["treaties"] = json::array();
    for (const auto& t : s.treaties) j["treaties"].push_back(treaty_to_json(t));
    j["memory"] = json::array();
    for (const auto& m : s.memory)
        j["memory"].push_back({{"hour_index", m.hour_index}, {"faction", m.faction}, {"text", m.text}});
    j["pending_battle"] = {{"active", s.pending_battle.active},
                           {"province", s.pending_battle.province},
                           {"attacker_army", s.pending_battle.attacker_army},
                           {"defender_army", s.pending_battle.defender_army}};
    j["pending_commands"] = json::array();
    for (const auto& c : s.pending_commands) j["pending_commands"].push_back(command_to_json(c));
    j["next_command_seq"] = s.next_command_seq;
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
    j["hour_index"] = c.hour_index;
    j["relation"] = c.relation;
    j["allowed_clause_types"] = c.allowed_clause_types;
    j["limits"] = {{"max_ceasefire_days", c.max_ceasefire_days},
                   {"max_safe_passage_days", c.max_safe_passage_days}};
    j["protected_provinces"] = c.protected_provinces;
    j["provinces"] = c.provinces;
    j["armies"] = c.armies;
    j["prisoners_held"] = c.prisoners_held;
    j["treaties"] = json::array();
    for (const auto& t : c.treaties) j["treaties"].push_back(treaty_to_json(t));
    j["memory"] = c.memory;
    j["envoy_loyalty"] = c.envoy_loyalty;
    j["envoy_defected"] = c.envoy_defected;
    j["player_gold"] = c.player_gold;
    j["defect_price"] = c.defect_price;
    j["titles"] = c.titles;
    j["army_provinces"] = c.army_provinces;
    j["army_strengths"] = c.army_strengths;
    j["adjacency"] = c.adjacency;
    j["grievance"] = c.grievance;
    j["snubbed_hours_left"] = c.snubbed_hours_left;
    j["mood"] = c.mood;
    j["player_vassal"] = c.player_vassal;
    return j.dump();
}

DiplomacyContext context_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        DiplomacyContext c;
        c.faction = j.at("faction").get<std::string>();
        c.counterpart = j.at("counterpart").get<std::string>();
        if (j.contains("hour_index"))
            c.hour_index = j.at("hour_index").get<int64_t>();
        else if (j.contains("turn"))
            c.hour_index = j.at("turn").get<int64_t>();  // legacy test vectors
        c.relation = j.at("relation").get<int>();
        c.allowed_clause_types = j.at("allowed_clause_types").get<std::vector<std::string>>();
        const json& lim = j.at("limits");
        c.max_ceasefire_days = lim.value("max_ceasefire_days", lim.value("max_ceasefire_turns", 365));
        c.max_safe_passage_days = lim.value("max_safe_passage_days", lim.value("max_safe_passage_turns", 90));
        c.protected_provinces = j.at("protected_provinces").get<std::vector<std::string>>();
        c.provinces = j.at("provinces").get<std::map<std::string, std::string>>();
        c.armies = j.at("armies").get<std::map<std::string, std::string>>();
        c.prisoners_held = j.at("prisoners_held").get<std::map<std::string, int>>();
        for (const auto& t : j.at("treaties")) {
            if (t.contains("hours_remaining"))
                c.treaties.push_back(treaty_from_json(t));
            else {
                Treaty tr;
                tr.type = t.at("type").get<std::string>();
                tr.hours_remaining = t.value("turns_remaining", 0) * 24;
                tr.army_id = t.value("army_id", "");
                tr.province_id = t.value("province_id", "");
                c.treaties.push_back(tr);
            }
        }
        c.memory = j.value("memory", std::vector<std::string>{});
        c.envoy_loyalty = j.value("envoy_loyalty", 0);
        c.envoy_defected = j.value("envoy_defected", false);
        c.player_gold = j.value("player_gold", 0);
        c.defect_price = j.value("defect_price", 0);
        c.titles = j.value("titles", std::map<std::string, int>{});
        c.army_provinces = j.value("army_provinces", std::map<std::string, std::string>{});
        c.army_strengths = j.value("army_strengths", std::map<std::string, int64_t>{});
        c.adjacency = j.value("adjacency", std::map<std::string, std::vector<std::string>>{});
        c.grievance = j.value("grievance", 0);
        c.snubbed_hours_left = j.value("snubbed_hours_left", 0);
        c.mood = j.value("mood", "calm");
        c.player_vassal = j.value("player_vassal", false);
        return c;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("context parse error: ") + e.what());
    }
}

namespace {

json clause_to_json(const Clause& c) {
    json j{{"type", c.type}};
    if (c.type == "ceasefire")
        j["days"] = c.days;
    else if (c.type == "prisoner_exchange")
        j["counts"] = c.counts;
    else if (c.type == "safe_passage")
        j.update({{"army_id", c.army_id}, {"province_id", c.province_id}, {"days", c.days}});
    else if (c.type == "cede_province")
        j["province_id"] = c.province_id;
    else if (c.type == "defect")
        j.update({{"title", c.title}, {"gold", c.gold}, {"provinces", c.provinces}});
    else if (c.type == "surrender")
        j.update({{"army_id", c.army_id}, {"days", c.days}});
    else if (c.type == "vassalage")
        j.update({{"title", c.title}, {"gold", c.gold}, {"provinces", c.provinces}});
    else if (c.type == "take_offense")
        j.update({{"reason", c.reason}, {"days", c.days}});
    return j;
}

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
        if (!j["spoken_line"].is_string())
            errors.push_back(err("malformed_proposal", "spoken_line"));
        else
            out.spoken_line = j["spoken_line"].get<std::string>();
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
        if (cj.contains("turns")) {
            errors.push_back(err("turns_not_allowed", "use days instead of turns"));
            continue;
        }
        static const std::map<std::string, std::vector<std::string>> kFields = {
            {"ceasefire", {"type", "days"}},
            {"prisoner_exchange", {"type", "counts"}},
            {"safe_passage", {"type", "army_id", "province_id", "days"}},
            {"cede_province", {"type", "province_id"}},
            {"defect", {"type", "title", "gold", "provinces"}},
            {"surrender", {"type", "army_id", "days"}},
            {"vassalage", {"type", "title", "gold", "provinces"}},
            {"take_offense", {"type", "reason", "days"}}};
        auto kf = kFields.find(c.type);
        for (auto it = cj.begin(); it != cj.end(); ++it) {
            const std::string& k = it.key();
            if (kf != kFields.end() && !detail::contains(kf->second, k)) {
                errors.push_back(err("unexpected_field", c.type + "." + k));
                continue;
            }
            if (k == "days") {
                if (!int_field(it.value(), c.days)) errors.push_back(err("malformed_clause", "days"));
            } else if (k == "army_id" || k == "province_id") {
                if (!it.value().is_string())
                    errors.push_back(err("malformed_clause", k));
                else
                    (k == "army_id" ? c.army_id : c.province_id) = it.value().get<std::string>();
            } else if (k == "reason") {
                if (!it.value().is_string())
                    errors.push_back(err("malformed_clause", "reason"));
                else
                    c.reason = it.value().get<std::string>();
            } else if (k == "title") {
                if (!it.value().is_string())
                    errors.push_back(err("malformed_clause", "title"));
                else
                    c.title = it.value().get<std::string>();
            } else if (k == "gold") {
                if (!int_field(it.value(), c.gold)) errors.push_back(err("malformed_clause", "gold"));
            } else if (k == "provinces") {
                if (!it.value().is_array()) {
                    errors.push_back(err("malformed_clause", "provinces"));
                    continue;
                }
                for (const auto& pv : it.value()) {
                    if (!pv.is_string())
                        errors.push_back(err("malformed_clause", "provinces"));
                    else
                        c.provinces.push_back(pv.get<std::string>());
                }
            } else if (k == "counts") {
                c.has_counts = true;
                if (!it.value().is_object()) {
                    errors.push_back(err("malformed_clause", "counts"));
                    continue;
                }
                for (auto ci = it.value().begin(); ci != it.value().end(); ++ci) {
                    int n = 0;
                    if (!int_field(ci.value(), n))
                        errors.push_back(err("malformed_clause", "counts." + ci.key()));
                    else
                        c.counts[ci.key()] = n;
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
    j["terrain_tags"] = b.terrain_tags;
    j["sides"] = json::object();
    for (const auto& kv : b.sides)
        j["sides"][kv.first] = {{"faction", kv.second.faction},
                                {"army_id", kv.second.army_id},
                                {"tickets", kv.second.tickets},
                                {"morale", kv.second.morale},
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
        b.terrain_tags = j.value("terrain_tags", std::vector<std::string>{});
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
    json j{{"province_id", r.province_id},
           {"winner", r.winner},
           {"objective_holder", r.objective_holder},
           {"objectives_held", r.objectives_held},
           {"losses", r.losses},
           {"retreat_province", r.retreat_province}};
    return j.dump();
}

BattleResult result_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        BattleResult r;
        r.province_id = j.at("province_id").get<std::string>();
        r.winner = j.at("winner").get<std::string>();
        r.objective_holder = j.value("objective_holder", "");
        r.objectives_held = j.value("objectives_held", std::map<std::string, std::string>{});
        r.losses = j.value("losses", std::map<std::string, std::map<std::string, int>>{});
        r.retreat_province = j.value("retreat_province", "");
        return r;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("result parse error: ") + e.what());
    }
}

}  // namespace pa250
