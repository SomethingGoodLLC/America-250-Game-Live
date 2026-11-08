// PA250Sim core types. Plain C++17: no Unreal headers, no UObject.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pa250 {

inline constexpr const char* kBritish = "british";
inline constexpr const char* kContinental = "continental";
inline constexpr int kSchemaVersion = 2;
inline constexpr int64_t kEpochYear = 1607;  // 1607-01-01 00:00 = hour_index 0 (proleptic Gregorian)
inline constexpr int kDefaultMarchHoursPerDay = 10;

struct GameTime {
    int64_t hour_index = 0;
};

struct YMDH {
    int32_t y = 1607;
    uint8_t m = 1;
    uint8_t d = 1;
    uint8_t h = 0;
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Edge {
    std::string id;
    std::string a;
    std::string b;
    std::string type;  // road | neck | bridge | ferry | boat
    int march_hours = 1;
    bool can_dismantle = false;
    std::string note;
};

struct Province {
    std::string id;
    std::string name;
    std::string owner;
    int map_index = 0;                      // 1..N explicit; never from file order
    std::vector<std::string> terrain;       // tags
    std::vector<std::string> adjacent;      // derived from edges, sorted
    std::vector<Point> polygon;             // map-space outline (legacy / bootstrap)
    Point label;                            // label anchor for the UI
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
    std::vector<std::string> path;  // edge ids remaining (current edge first)
    int hours_into_edge = 0;
};

struct Treaty {
    std::string type;  // "ceasefire" | "safe_passage"
    int hours_remaining = 0;
    std::string army_id;       // safe_passage only
    std::string province_id;   // safe_passage only
};

struct MemoryEntry {
    int64_t hour_index = 0;
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
    int max_ceasefire_days = 365;
    int max_safe_passage_days = 90;
    bool controls_harbor = false;
    // Persuasion ("turn the envoy"): sim-owned so it is deterministic and saved.
    int envoy_loyalty = 0;                 // 0..100; higher = costlier to turn
    bool envoy_defected = false;
    std::string envoy_title_granted;
    int grievance = 0;               // 0..100; insults and failed talks raise it, time lowers it
    int64_t snub_until_hour = 0;     // refuses to treat until this hour_index
};

// ---- Commands -----------------------------------------------------------
enum class CommandKind : uint8_t {
    MoveArmy = 1,
    HaltArmy = 2,
    ApplyBattleResult = 3,
    ApplyDiplomacy = 4,
};

struct CommandPayload {
    CommandKind kind = CommandKind::MoveArmy;
    std::string army_id;
    std::string dest_province;           // MoveArmy
    std::vector<std::string> edge_path;  // MoveArmy (optional precomputed)
    // ApplyBattleResult / ApplyDiplomacy stored as JSON text for round-trip simplicity
    std::string result_json;
};

struct Command {
    int64_t apply_on_hour = 0;
    uint64_t sequence = 0;
    CommandPayload payload;
};

enum class Interrupt : uint8_t { None = 0, ArmiesInContact, DiplomacyRequest, EventPopup };

struct CampaignState {
    GameTime now;
    uint64_t seed = 0;
    std::string scenario;
    std::string player_faction = kContinental;
    std::string ai_objective;  // province the AI marches toward
    int march_hours_per_day = kDefaultMarchHoursPerDay;
    std::map<std::string, Province> provinces;
    std::map<std::string, Edge> edges;
    std::map<std::string, Army> armies;
    std::map<std::string, FactionInfo> factions;
    std::map<std::string, int> supplies;         // faction -> supplies
    std::map<std::string, int> relations;        // AI faction -> attitude toward player, -100..100
    std::map<std::string, int> prisoners_held;   // holding faction -> enemy prisoners held
    bool player_vassal = false;                  // pledged to the Crown (rogue army under its protection)
    std::string player_title;                    // title the Crown granted the player
    int player_gold = 500;                       // treasury the player can offer
    std::map<std::string, std::string> fiefs;    // province -> envoy name it was granted to
    std::vector<Treaty> treaties;
    std::vector<MemoryEntry> memory;
    PendingBattle pending_battle;
    std::vector<Command> pending_commands;
    uint64_t next_command_seq = 1;
    std::vector<std::string> log;
};

struct PathPlan {
    bool ok = false;
    std::string error;
    std::vector<std::string> edges;
    std::vector<std::string> provinces;  // including start
    int total_march_hours = 0;
    GameTime arrival;
};

// ---- Orders (legacy test helper: one-step intent) -----------------------
struct Order {
    std::string army_id;
    std::string to_province;
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
    std::vector<std::string> terrain_tags;
    std::map<std::string, SideSetup> sides;  // keyed by faction
};
struct BattleResult {
    std::string province_id;
    std::string winner;
    std::string objective_holder;  // plurality owner of posts at the end, or ""
    std::map<std::string, std::string> objectives_held;  // post_id -> faction (omit neutral)
    std::map<std::string, std::map<std::string, int>> losses;  // faction -> class -> lost
    std::string retreat_province;  // loser's retreat; empty = sim chooses
};

struct TickOutput {
    CampaignState state;
    std::vector<Interrupt> interrupts;
    std::vector<BattleSetup> battles;
};

// ---- Diplomacy ----------------------------------------------------------
struct Clause {
    std::string type;
    int days = -1;  // ceasefire / safe_passage (wire field; turns rejected)
    std::map<std::string, int> counts;  // prisoner_exchange: faction -> prisoners released
    bool has_counts = false;
    std::string army_id;
    std::string province_id;
    // defect: what the player pays. Gold and lands come out of the player's own holdings.
    std::string title;
    int gold = -1;
    std::vector<std::string> provinces;
    std::string reason;  // take_offense: disrespect | insolence | threat | deceit
};
struct DiplomacyProposal {
    std::string spoken_line;  // display-only; never read by the sim
    std::vector<Clause> clauses;
};
struct DiplomacyContext {
    std::string faction;        // the envoy's faction
    std::string counterpart;
    int64_t hour_index = 0;
    int relation = 0;
    std::vector<std::string> allowed_clause_types;
    int max_ceasefire_days = 365;
    int max_safe_passage_days = 90;
    std::vector<std::string> protected_provinces;
    std::map<std::string, std::string> provinces;  // id -> owner
    std::map<std::string, std::string> armies;     // id -> faction
    std::map<std::string, int> prisoners_held;
    std::vector<Treaty> treaties;
    std::vector<std::string> memory;  // most recent entries, text only
    // Persuasion inputs (read-only for the model; the sim re-checks every number).
    int envoy_loyalty = 0;
    bool envoy_defected = false;
    int player_gold = 0;
    int defect_price = 0;                  // offer value needed to turn the envoy
    std::map<std::string, int> titles;     // grantable title -> offer value
    // Battlefield picture for surrender ("honors of war").
    std::map<std::string, std::string> army_provinces;
    std::map<std::string, int64_t> army_strengths;
    std::map<std::string, std::vector<std::string>> adjacency;
    // Mood: failed or rude diplomacy makes the envoy refuse to treat for a while.
    int grievance = 0;
    int snubbed_hours_left = 0;   // > 0: he will not negotiate
    std::string mood = "calm";   // calm | irritated | insulted | angry
    bool player_vassal = false;
};
struct DiplomacyResult {
    std::string faction;  // envoy faction
    bool accepted = false;
    DiplomacyProposal proposal;
};

}  // namespace pa250
