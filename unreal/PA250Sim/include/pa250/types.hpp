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
    // Development tracks (0..3 each, gameplay tuning). Total development is
    // 1 + the sum; effects live in logistics/clock/battle, never in UI code.
    int farm_level = 0;      // monthly provisions + supply income
    int workshop_level = 0;  // faster cartridge manufacture
    int depot_level = 0;     // faster shipments into this province
    int fort_level = 0;      // defender morale/readiness bonus at this province
    // Discrete City View buildings (non-stackable). Soft-default false on old saves.
    bool church = false;          // Meeting House — Support
    bool militia_muster = false;  // Militia Muster — defender muster bonus
    int manpower_used = 0;        // men raised here this month (resets on the 1st)
};

// Timed muster of one company in a town. Gold and stores are charged on start; on completion the
// men join the player's army standing in the town, or form a new one there.
struct RecruitJob {
    std::string id;
    std::string province_id;
    std::string unit_id;     // catalog id (militia_company | provincial_regulars)
    std::string unit_class;  // Army.units key (militia | line_infantry)
    int men = 0;
    int hours_remaining = 0;
    int hours_total = 0;
    int paid_gold = 0;
    int paid_muskets = 0;
    int paid_provisions = 0;
};

// Timed City View construction. Gold is charged on start; completion applies the building.
struct ConstructionJob {
    std::string id;
    std::string province_id;
    std::string building_id;  // farm|workshop|depot|fort|church|militia_muster
    bool is_upgrade = false;
    int hours_remaining = 0;
    int hours_total = 0;
    int paid_gold = 0;
    std::string status;  // underway | complete | cancelled
};

struct Army {
    std::string id;
    std::string name;
    std::string faction;
    std::string province;
    std::string previous_province;
    std::map<std::string, int> units;    // unit class -> fit (fieldable)
    std::map<std::string, int> sick;     // unit class -> sick (not fieldable)
    std::map<std::string, int> wounded;  // unit class -> wounded (not fieldable)
    int morale = 70;
    int readiness = 100;  // 0..100 material/rest readiness (campaign, not battlefield morale)
    int fatigue = 0;      // 0..100; rises with fighting, falls with rest
    std::vector<std::string> path;  // edge ids remaining (current edge first)
    int hours_into_edge = 0;
};

// Usable war materials at one province (not gold; not abstract faction "supplies").
// Funds stay on CampaignState.player_gold. Capture changes owner, not local_support.
struct ProvinceStores {
    int powder = 0;
    int cartridges = 0;
    int muskets = 0;
    int bayonets = 0;
    int provisions = 0;
    int lead = 0;   // cartridge crafting input (gameplay tuning)
    int paper = 0;  // cartridge crafting input (gameplay tuning)
};

// Legacy faction bag kept for soft-migration of older schema-2 saves only.
struct FactionStores {
    int powder = 0;
    int arms = 0;  // maps to muskets on migrate
    int provisions = 0;
};

// Paper units temporarily held back from fielding (not equipment, readiness, or morale).
struct PersonnelCommitment {
    std::string id;
    std::string army_id;
    std::string unit_class;  // e.g. militia
    int count = 0;
    std::string reason;
    int64_t release_on_hour = 0;
};

// Disclosed finite locality reserve abstraction (not a real warehouse network).
struct DonorReserve {
    std::string id;
    std::string label;  // shown to the player as a gameplay abstraction
    int powder = 0;
    int arms = 0;
    int provisions = 0;
};

// In-transit material. Commit ≠ receive. Purchase has empty source_province;
// transfer debits source at departure and credits dest only on delivery.
struct Shipment {
    std::string id;
    std::string donor_reserve_id;  // optional donor abstraction (L7)
    std::string source_province;   // empty = market/donor purchase
    std::string dest_province;     // province ledger credited on delivery
    std::string dest_faction;      // legacy; prefer dest_province
    int powder = 0;
    int cartridges = 0;
    int muskets = 0;
    int bayonets = 0;
    int arms = 0;  // legacy alias for muskets on soft migrate
    int provisions = 0;
    int lead = 0;
    int paper = 0;
    int64_t commit_hour = 0;
    int64_t deliver_on_hour = 0;
    std::string status;  // committed | in_transit | delivered | lost
};

// Clock-driven cartridge manufacture: powder + lead/paper + workers → cartridges.
struct CartridgeJob {
    std::string id;
    std::string province_id;
    std::string army_id;
    std::string unit_class = "militia";
    int workers = 0;
    int powder = 0;
    int lead = 0;
    int paper = 0;
    int cartridges_out = 0;
    int64_t started_hour = 0;
    int64_t complete_on_hour = 0;
    std::string status;  // underway | complete | cancelled
};

// Structured after-battle snapshot (ground held ≠ losses ≠ ammo ≠ morale).
struct BattleReport {
    std::string province_id;
    std::string winner;
    std::string province_owner_before;
    std::string province_owner_after;
    int local_support_before = 0;
    int local_support_after = 0;
    int gold_before = 0;
    int gold_after = 0;
    std::string objective_holder;
    std::map<std::string, std::string> objectives_held;
    std::map<std::string, std::map<std::string, int>> losses;
    std::map<std::string, std::map<std::string, int>> wounded;
    std::map<std::string, int> cartridges_spent;
    std::map<std::string, int> powder_spent;
    std::map<std::string, int> morale_delta;  // army_id -> delta
    std::string ownership_reason;            // held_objective | stores_forfeit | unchanged
    std::vector<std::string> change_reasons; // player-facing attributed deltas
    int defender_reward_support = 0;
    int personal_kills = 0;
    std::string retreat_to;  // loser's destination when retreated; empty otherwise
    std::string loser_fate;  // retreated | shattered | destroyed | none
    int scavenged_cartridges = 0;
    int scavenged_powder = 0;
    int scavenged_muskets = 0;
};

// One attributed treasury/stores/support delta (soft-default empty on old saves).
struct ResourceChange {
    int64_t hour_index = 0;
    std::string reason;  // e.g. "Fort, Lexington: -45 gold"
    int gold = 0;
    int powder = 0;
    int cartridges = 0;
    int muskets = 0;
    int provisions = 0;
    int local_support = 0;
    std::string province_id;
};

// Structured staff letter. Flavor text is display-only and never parsed for state.
struct CorrespondenceItem {
    std::string id;
    int64_t created_hour = 0;
    int64_t resolve_on_hour = 0;  // when reply/delivery is applied
    std::string recipient;        // committee_of_safety | town_selectmen | merchant | washington_hq | congress
    // purchase | request_assistance | requisition | capacity_investment | knox_support |
    // commander_report | source_decision | historical_notice
    std::string kind;
    std::string status;  // needs_approval | pending | approved | partial | delayed | refused | delivered | dismissed
    int powder = 0;
    int arms = 0;
    int provisions = 0;
    int gold_cost = 0;
    int committed_powder = 0;
    int committed_arms = 0;
    int committed_provisions = 0;
    int received_powder = 0;
    int received_arms = 0;
    int received_provisions = 0;
    bool fictionalized = true;
    std::string subject;
    std::string body;
    std::string reason;
    bool awaiting_choice = false;  // true until the player picks a path
    std::string source_id;         // catalog L1..L7, D8
    std::string event_id;          // stable playable/reference event id
    std::string decision_id;       // choice taken (empty until decided)
    bool decision_applied = false; // exactly-once effects
};

struct StaffState {
    std::string clerk_name = "Clerk Hale";  // fictionalized composite
    int capacity_per_day = 2;
    int letters_processed_today = 0;
    int64_t capacity_day_index = -1;  // hour_index/24 of last reset
    std::vector<CorrespondenceItem> queue;
};

// Detached commander: objectives and risk preference (Stage B friction).
struct DetachedCommander {
    std::string id = "parker";
    std::string name = "Capt. Parker";  // fictionalized composite of local command
    std::string army_id = "continental_militia";
    std::string objective = "preserve_force";  // preserve_force | contest_crossing | hold_stores
    std::string risk = "cautious";             // cautious | balanced | aggressive
    bool pending_clarification = false;
    std::string last_request;
};

struct CampaignProject {
    std::string id;  // e.g. knox_ticonderoga
    std::string status = "none";  // none | offered | underway | delivered | declined
    int64_t start_hour = 0;
    int64_t ready_hour = 0;
    int pieces = 0;
    int transport_committed = 0;
    bool fictionalized_authorization = false;
};

struct CampaignProgress {
    int battles_fought = 0;
    bool lexington_fought = false;  // opening-war gate; set after first fight (Concord in this slice)
    bool aftermath_pending = false;
    bool concord_followup_seeded = false;
    bool chapter_continental = false;  // after 14 Jun 1775
    bool chapter_washington = false;   // after 3 Jul 1775
    int capacity_level = 0;            // 0 none, 1 procurement/repair contact
    int64_t capacity_ready_hour = 0;
    std::string player_role = "massachusetts_field_commander";
    CampaignProject knox;
    DetachedCommander commander;
    std::vector<PersonnelCommitment> personnel_commitments;
    std::vector<CartridgeJob> cartridge_jobs;
    std::vector<ConstructionJob> construction_jobs;
    std::vector<RecruitJob> recruit_jobs;
    int treasury_debt = 0;     // gold owed on the province's bills of credit
    int pay_arrears_months = 0;  // consecutive monthly settlements the army went unpaid
    // The other side's war chest: the same monthly accounts, no credit. It is never shown to the
    // player as a number; unpaid months reach the player only as reports.
    int enemy_gold = 60;
    int enemy_arrears_months = 0;
    BattleReport last_battle_report;
    std::vector<std::string> resolved_event_ids;
    std::vector<std::string> received_source_ids;
    bool offensive_postponed = false;  // L7 conserve (gameplay tuning)
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
    std::map<std::string, int> supplies;         // faction -> abstract supply points (not powder/arms)
    std::map<std::string, int> relations;        // AI faction -> attitude toward player, -100..100
    std::map<std::string, int> prisoners_held;   // holding faction -> enemy prisoners held
    bool player_vassal = false;                  // pledged to the Crown (rogue army under its protection)
    std::string player_title;                    // title the Crown granted the player
    int player_gold = 500;                       // treasury the player can offer
    std::map<std::string, std::string> fiefs;  // province -> envoy name it was granted to
    // Authoritative war-material ledgers keyed by province id (all 13 seeded).
    std::map<std::string, ProvinceStores> province_stores;
    // Legacy faction bags: read for soft migration only; not the write path after migrate.
    std::map<std::string, FactionStores> stores;
    int local_support = 55;  // 0..100 civilian cooperation (not military control)
    StaffState staff;
    CampaignProgress progress;
    std::string pending_event_id;  // correspondence id that raised EventPopup
    std::map<std::string, DonorReserve> donor_reserves;
    std::vector<Shipment> shipments;
    std::vector<Treaty> treaties;
    std::vector<MemoryEntry> memory;
    PendingBattle pending_battle;
    std::vector<Command> pending_commands;
    uint64_t next_command_seq = 1;
    std::vector<std::string> log;
    std::vector<ResourceChange> resource_changes;  // attributed treasury/stores/support history
};

// Immutable catalog entry (opening-war correspondence registry). Never mutated by choices.
struct CorrespondenceChoiceDef {
    std::string id;
    std::string label;
    std::string effects_summary;  // player-facing; amounts are gameplay tuning
};

struct CorrespondenceCatalogEntry {
    std::string source_id;
    std::string event_id;
    std::string mode;  // playable | historical_reference | retrospective_exhibit
    std::string author;
    std::string recipient;
    std::string document_date;
    std::string reported_event_date;
    std::string source_type;
    std::string institutional_url;
    std::string uncertainty_notes;
    std::string evidence_summary;
    std::string text_kind = "paraphrase_from_report";
    YMDH eligible_from{};
    std::vector<std::string> faction_visibility;
    bool requires_lexington = false;
    bool requires_chapter_continental = false;
    std::vector<std::string> requires_resolved_events;
    bool activate_decisions = false;
    std::string adaptation_relay;
    std::string adaptation_dispatch;
    std::string adaptation_label = "Game adaptation—not a historical quotation.";
    std::vector<CorrespondenceChoiceDef> choices;
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
    std::map<std::string, int> tickets;  // per unit class, after stores gating
    int morale = 0;                     // campaign confidence (feeds UE pressure bias)
    int readiness = 100;
    int cartridge_budget = 0;           // cartridges available to this side from the province
    float reinforcement_scale = 1.f;    // farm/depot: shortens bot respawn (UE)
    std::string retreat_province;
};
struct BattleSetup {
    bool valid = false;
    std::string province_id;
    std::string province_name;
    std::string objective = "green";
    std::string scenario_preset = "lexington_common";  // lexington_common | concord_bridge | battle_road
    uint64_t seed = 0;
    std::string attacker_faction;
    std::string defender_faction;
    std::string player_faction;
    std::vector<std::string> terrain_tags;
    std::map<std::string, SideSetup> sides;  // keyed by faction
    int province_cartridges = 0;            // stock at fight start (authoritative cap)
    int province_powder = 0;                // raw powder at fight start (misfire waste cap)
    std::string force_notes = "sourced approximation — not a precise muster roll";
    // Contested-province development (0 when absent / old saves).
    int farm_level = 0;
    int workshop_level = 0;
    int depot_level = 0;
    int fort_level = 0;
    bool church = false;
    bool militia_muster = false;  // defender owns a Militia Muster here; UE raises its muster cap
    std::map<std::string, std::string> initial_post_holders;  // post_id -> faction
    std::vector<std::string> modifier_notes;                 // sourced lines for pre-battle UI
};
struct BattleResult {
    std::string province_id;
    std::string winner;
    std::string objective_holder;  // plurality owner of posts at the end, or ""
    std::map<std::string, std::string> objectives_held;  // post_id -> faction (omit neutral)
    std::map<std::string, std::map<std::string, int>> losses;   // faction -> class -> killed
    std::map<std::string, std::map<std::string, int>> wounded;  // faction -> class -> wounded
    std::map<std::string, int> cartridges_spent;               // faction -> spent
    std::map<std::string, int> powder_spent;                   // faction -> spent (rare)
    std::map<std::string, int> morale_delta;                   // army_id -> delta (optional input)
    std::map<std::string, int> shots_fired;                    // faction -> successful discharges
    int personal_kills = 0;  // deaths caused by the local player
    std::string retreat_province;  // loser's retreat; empty = sim chooses
    std::string province_owner_after;  // filled by apply when ownership flips
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
