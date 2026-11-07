// PA250Sim core types. Plain C++17: no Unreal headers, no UObject.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pa250 {

inline constexpr const char* kBritish = "british";
inline constexpr const char* kContinental = "continental";
inline constexpr int kSchemaVersion = 1;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Province {
    std::string id;
    std::string name;
    std::string owner;
    std::vector<std::string> adjacent;  // sorted
    std::vector<Point> polygon;         // map-space outline for the UI
    Point label;                        // label anchor for the UI
    int supply_value = 1;
};

struct Army {
    std::string id;
    std::string name;
    std::string faction;
    std::string province;
    std::string previous_province;
    std::map<std::string, int> units;  // unit class -> strength
    int morale = 70;
};

struct Treaty {
    std::string type;  // "ceasefire" | "safe_passage"
    int turns_remaining = 0;
    std::string army_id;       // safe_passage only
    std::string province_id;   // safe_passage only
};

struct MemoryEntry {
    int turn = 0;
    std::string faction;
    std::string text;
};

struct PendingBattle {
    bool active = false;
    std::string province;
    std::string attacker_army;
    std::string defender_army;
};

struct FactionInfo {
    std::string display_name;
    std::string envoy_name;
    std::string envoy_title;
    std::vector<std::string> allowed_clause_types;
    std::vector<std::string> protected_provinces;  // red line: may never be ceded
    int max_ceasefire_turns = 3;
    int max_safe_passage_turns = 3;
};

struct CampaignState {
    int turn = 0;
    uint64_t seed = 0;
    std::string scenario;
    std::string player_faction = kContinental;
    std::string ai_objective;  // province the AI marches toward
    std::map<std::string, Province> provinces;
    std::map<std::string, Army> armies;
    std::map<std::string, FactionInfo> factions;
    std::map<std::string, int> supplies;         // faction -> supplies
    std::map<std::string, int> relations;        // AI faction -> attitude toward player, -100..100
    std::map<std::string, int> prisoners_held;   // holding faction -> enemy prisoners held
    std::vector<Treaty> treaties;
    std::vector<MemoryEntry> memory;
    PendingBattle pending_battle;
    std::vector<std::string> log;
};

// ---- Orders -------------------------------------------------------------
struct Order {
    std::string army_id;
    std::string to_province;  // equal to current province = hold
};
struct Orders {
    std::vector<Order> orders;
};

// ---- Battle -------------------------------------------------------------
struct SideSetup {
    std::string faction;
    std::string army_id;
    std::map<std::string, int> tickets;  // per unit class, copied from army units
    int morale = 0;
    std::string retreat_province;
};
struct BattleSetup {
    bool valid = false;
    std::string province_id;
    std::string province_name;
    std::string objective = "green";
    uint64_t seed = 0;
    std::string attacker_faction;
    std::string defender_faction;
    std::string player_faction;
    std::map<std::string, SideSetup> sides;  // keyed by faction
};
struct BattleResult {
    std::string province_id;
    std::string winner;
    std::string objective_holder;  // faction holding the objective at the end, or ""
    std::map<std::string, std::map<std::string, int>> losses;  // faction -> class -> lost
    std::string retreat_province;  // loser's retreat; empty = sim chooses
};

// ---- Diplomacy ----------------------------------------------------------
struct Clause {
    std::string type;
    int turns = -1;
    std::map<std::string, int> counts;  // prisoner_exchange: faction -> prisoners released
    bool has_counts = false;
    std::string army_id;
    std::string province_id;
};
struct DiplomacyProposal {
    std::string spoken_line;  // display-only; never read by the sim
    std::vector<Clause> clauses;
};
struct DiplomacyContext {
    std::string faction;        // the envoy's faction
    std::string counterpart;
    int turn = 0;
    int relation = 0;
    std::vector<std::string> allowed_clause_types;
    int max_ceasefire_turns = 3;
    int max_safe_passage_turns = 3;
    std::vector<std::string> protected_provinces;
    std::map<std::string, std::string> provinces;  // id -> owner
    std::map<std::string, std::string> armies;     // id -> faction
    std::map<std::string, int> prisoners_held;
    std::vector<Treaty> treaties;
    std::vector<std::string> memory;  // most recent entries, text only
};
struct DiplomacyResult {
    std::string faction;  // envoy faction
    bool accepted = false;
    DiplomacyProposal proposal;
};

}  // namespace pa250
