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
    int powder_mill_level = 0;  // monthly powder into this town's magazine (needs river or falls)
    // Building ids that stood here before the ground rules (catalog requires_tags / income_requires_tags)
    // and do not meet them: they keep working and upgrading. Sorted. Set by mark_grandfathered_buildings.
    std::vector<std::string> grandfathered;
    // Discrete City View buildings (non-stackable). Soft-default false on old saves.
    bool church = false;          // Meeting House — Support
    bool militia_muster = false;  // Militia Muster — defender muster bonus
    int manpower_used = 0;        // men raised here this month (resets on the 1st)
    // Siege (GAME INVENTION): a defender beaten on its own fortified ground keeps the town until
    // the hours run out with a besieging army still there. Empty / 0 = no siege.
    std::string siege_by;            // besieging faction
    int siege_hours_remaining = 0;
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

// Men who fell, by what became of them. Every man who fell is exactly one of the three.
struct MenDown {
    int killed = 0;
    int wounded = 0;
    int prisoners = 0;
    int total() const { return killed + wounded + prisoners; }
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
    // Halted part-way along a leg, the army turns about: it is on `returning_edge`, this many march
    // hours short of the town the leg began in (`province`). It starts no new march until it is back.
    std::string returning_edge;
    int returning_hours = 0;
    // Units of each class that came from other colonies (campaign pass, lobbying): they are among
    // `units`, and begin to go home while Unity is under its floor.
    std::map<std::string, int> allied;
    // "garrison": the AI never orders it out of its base (campaign rules). Empty: a field army.
    std::string role;
    // Units of each class re-enlisted for the new year (enlistments; cleared when they expire).
    std::map<std::string, int> reenlisted;
    // Rounds in each man's cartridge box. It falls with what the army fires and what a beaten army
    // drops, and fills only in a town its side holds, from that town's magazine. -1: a full box
    // (battle_rules carried_per_man); read it through carried_rounds().
    int rounds_per_man = -1;
    // Men who have fallen but do not yet make up a whole unit, per class: a battle reports its
    // casualties in men, units leave the ranks a whole unit at a time, and what is left over waits
    // here for the next battle. Never as many as a unit; cleared when the class has no units left.
    std::map<std::string, MenDown> men_down;
    // Field guns with the army. It fields as many as it has crews to serve (served_guns); it draws
    // them from the magazine of a town its side holds, takes them in battle, and can lose them.
    int guns = 0;
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
    int guns = 0;   // field guns laid up here: shipped like other stores, drawn by an army that has crews
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
    int guns = 0;  // field guns on the wagons
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
    std::string end_reason;  // BattleResult.end_reason as applied
    std::string mode;        // the kind of fight it was (BattleSetup.mode); empty for the open fight
    std::map<std::string, std::map<std::string, int>> prisoners;  // faction -> class -> captured (clamped)
    int siege_hours = 0;                    // > 0: a siege began; the town falls after this many hours
    std::vector<std::string> map_changes;   // what changed on the map, in plain words (after-battle card)
    // Cartridge boxes, in rounds a man, by faction: at form-up, as the armies leave the field, what
    // the beaten army dropped and what the winner picked up of it.
    std::map<std::string, int> rounds_before;
    std::map<std::string, int> rounds_after;
    std::map<std::string, int> rounds_dropped;
    std::map<std::string, int> rounds_picked_up;
    // Men who fell, as the battle counted them and the sim took them (never more than the army had):
    // faction -> class -> killed / wounded / prisoners. `losses`, `wounded` and `prisoners` above are
    // the whole units that left the ranks because of them.
    std::map<std::string, std::map<std::string, MenDown>> casualties;
    // Colours taken, as applied: captor faction -> colours.
    std::map<std::string, int> colours_captured;
    // Guns that changed hands, as applied: the faction that has them now -> guns.
    std::map<std::string, int> guns_taken;
    // Generals who fell, as applied: faction -> wounded | killed | captured.
    std::map<std::string, std::string> commander_events;
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
    // Once delivered the guns are a campaign item: they lie in a town, or go where the army
    // hauling them goes.
    std::string province;
    std::string army_id;
    // The train's light pieces and their crews go to the first army that hauls it, once.
    bool field_guns_issued = false;
};

// One mission of a faction's chain (definition: data/missions_boston_1775.json).
struct MissionState {
    std::string status = "locked";  // locked | active | complete | failed
    int64_t activated_hour = 0;
    int64_t deadline_hour = 0;  // 0 = none
    int64_t resolved_hour = 0;
};

// A mission of the player's that has ended, as it was applied. The campaign map shows each one
// once, as a card: what happened and what it paid (or cost).
struct MissionResult {
    std::string mission_id;
    std::string title;
    std::string outcome;       // complete | failed
    int64_t hour_index = 0;
    int gold = 0;              // as applied (a purse never goes below nothing)
    int local_support = 0;     // as applied, after the 0..100 clamp
    int army_morale = 0;       // as the mission file gives it; every army of the side takes it
    int army_readiness = 0;
    int british_attitude = 0;  // as applied
    std::string stores_province;
    int powder = 0, cartridges = 0, muskets = 0, provisions = 0;
    bool start_evacuation = false;
    std::string summary;       // the mission file's own line
};

// How the campaign ended. Once decided the clock no longer advances.
struct CampaignOutcome {
    bool decided = false;
    std::string winner;  // british | continental
    // boston_taken | evacuation | hq_lost | support_collapsed | no_field_army | vassalage | window_score
    std::string reason;
    int64_t hour_index = 0;
    std::vector<std::string> summary;  // plain-word lines for the end screen
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
    // ---- Campaign arc (campaign rules + missions; all soft-default on old saves) ----
    std::map<std::string, MissionState> missions;  // mission id -> state
    std::vector<MissionResult> mission_results;    // the player's missions as they ended, oldest first (saved only when any)
    CampaignOutcome outcome;
    int hq_lost_hours = 0;       // consecutive hours the Patriot headquarters town has been British
    int support_low_hours = 0;   // consecutive hours Support has been under the floor
    int evacuation_hours_remaining = 0;  // > 0: the British are preparing to sail
    bool evacuated = false;
    bool enlistment_warned = false;
    bool enlistments_expired = false;
    std::map<std::string, int> battles_won;  // faction -> battles won
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
    // A mission's reward or failure effect. result_json = {"mission_id":..., "outcome":"complete"|"failed"}.
    MissionEffect = 5,
    // A campaign order (protocol/schemas/campaign_orders.yaml): result_json is the order object.
    CampaignOrder = 6,
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

enum class Interrupt : uint8_t { None = 0, ArmiesInContact, DiplomacyRequest, EventPopup, CampaignOver, Decision };

// ---- Campaign pass: people, lobbying, decisions (all soft-default; saved only when used) ----
// A battle factor at a named town for a set time (a letter, a decision or an agent put it there).
// It shows on the pre-battle card with its source.
struct TownFactor {
    std::string province;
    std::string faction;    // the side whose number it moves
    std::string label;      // morale | readiness | fortification | reinforcement | losses | magazine_saved | alarm
    int value = 0;
    std::string source;     // player-facing: the letter, decision or person
    int64_t until_hour = 0; // 0: until used
    bool one_battle = false;  // spent by the next battle there
};
// What a person is doing now. A person holds one role at a time.
struct PersonState {
    std::string role;       // "" (at hand) | general | delegate | organizer | envoy | agent
    std::string army_id;    // general
    std::string province;   // organizer
    std::string bloc;       // delegate
    int uses_left = -1;     // agent: -1 = as the data gives
    std::string status;     // "" | wounded | killed | captured | away | recalled | exposed
    int64_t away_until_hour = 0;  // wounded / away: back at hand at this hour
    bool unlocked = false;  // a measure made him available
    bool envoy_counted = false;  // the envoy's one-time Attitude was given
};
struct MeasureState {
    std::string status;     // "" | sent | passed | failed | lapsed
    int64_t vote_hour = 0;  // when the vote falls (set when it opens or a dispatch is sent)
    int64_t answer_hour = 0;  // dispatch: when the answer reaches Boston
    int64_t retry_hour = 0;   // failed: it may be put again from this hour
    int yes = 0;            // blocs that voted for it
    bool voted_yes = false; // dispatch in transit: how London voted
    int times = 0;          // times passed (repeatable requests)
};
struct DelayedEffect {
    int64_t due_hour = 0;
    std::string faction;    // the side it belongs to
    std::string source;     // measure or decision title
    std::string text;       // what is told when it lands
    std::string effects_json;  // the effect set, as in the data file
    std::string condition;  // "" | boston_not_taken
};
struct MonthlyGrant {
    std::string source;
    std::string faction;
    std::string province;
    int powder = 0, muskets = 0, provisions = 0;
};
struct AiLure {
    std::string province;
    std::string faction;    // the side drawn there
    int64_t until_hour = 0;
};
struct CampaignPass {
    bool seeded = false;
    std::map<std::string, PersonState> persons;
    std::map<std::string, int> bloc_lean;          // bloc id -> lean (-100..100)
    std::map<std::string, MeasureState> measures;  // measure id -> state
    int favors = 0;
    int ministry_confidence = 0;
    std::string pending_decision;                  // a decision waiting for the player
    std::map<std::string, std::string> decisions;  // decision id -> option taken
    std::vector<DelayedEffect> delayed;
    std::vector<TownFactor> town_factors;
    std::vector<MonthlyGrant> grants;
    std::vector<AiLure> lures;
    std::map<std::string, int64_t> scouted_until;  // "<viewer faction>:<army id>" -> hour
    std::map<std::string, int64_t> boats_until;    // army id -> hour
    std::map<std::string, int> upkeep_share_pct;   // faction -> share of army pay its body carries
    int price_rise_pct = 0;
    std::vector<std::string> flags;
    std::vector<std::string> counted_missions;     // missions already paid in favors
    int unity_desertion_day = -1;                  // last day units left for low Unity
    bool any() const {
        return seeded || !persons.empty() || !bloc_lean.empty() || !measures.empty() || favors != 0 ||
               ministry_confidence != 0 || !pending_decision.empty() || !decisions.empty() || !delayed.empty() ||
               !town_factors.empty() || !grants.empty() || !lures.empty() || !scouted_until.empty() ||
               !boats_until.empty() || !upkeep_share_pct.empty() || price_rise_pct != 0 || !flags.empty() ||
               !counted_missions.empty();
    }
};

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
    CampaignPass pass;  // people, lobbying, decisions (campaign pass)
};

// Immutable catalog entry (opening-war correspondence registry). Never mutated by choices.
// Data-driven consequences of a catalog choice (GAMEPLAY TUNING, read from the catalog JSON).
struct CorrespondenceChoiceEffects {
    int gold = 0;              // negative = cost; the choice is refused if the treasury is short
    int local_support = 0;
    int british_attitude = 0;
    int provisions = 0;        // to the player's depot; with delivery_hours > 0 it arrives later
    int powder = 0;
    int delivery_hours = 0;
    int army_morale = 0;       // every army of the player's faction
    int army_readiness = 0;
    int clerk_load = 0;        // desk slots used today
    int favors = 0;            // favors owed to the player in his Congress (campaign pass)
    // A battle factor at a named town for a set number of days, shown on the pre-battle card with
    // the letter as its source. label: morale | readiness | fortification | reinforcement.
    std::string factor_province;
    std::string factor_label;
    int factor_value = 0;
    int factor_days = 0;
    bool any() const {
        return gold || local_support || british_attitude || provisions || powder || army_morale ||
               army_readiness || clerk_load || favors || (factor_value != 0 && !factor_province.empty());
    }
};

// One answer to the detached commander's letter (catalog "commander_letter"): what it does to his
// army now, and the factors it leaves at the town for the next battle there.
struct CommanderLetterFactor {
    std::string label;  // morale | readiness | withdraw_early | stand_fast | magazine_saved
    int value = 0;
};
struct CommanderLetterChoice {
    std::string id;
    std::vector<std::string> aliases;
    std::string label;
    std::string effects_summary;
    CorrespondenceChoiceEffects effects;
    std::vector<CommanderLetterFactor> factors;
};

struct CorrespondenceChoiceDef {
    std::string id;
    std::string label;
    std::string effects_summary;  // player-facing; amounts are gameplay tuning
    CorrespondenceChoiceEffects effects;
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

// What the battle needs to know of a general (persons data rules.commander).
struct CommanderRules {
    float aura_radius_m = 30.f;        // his officer's aura
    float home_post_radius_m = 25.f;   // how far from his station he lets himself be drawn
    float fall_pressure = 0.6f;        // the morale check on his side when he falls...
    float fall_pressure_seconds = 10.f;  // ...fading over this long
    int fate_wounded = 60, fate_killed = 25, fate_captured = 15;  // what becomes of him, in 100
};

// ---- Campaign arc definitions (data files; GAMEPLAY TUNING) ---------------
// data/campaign_rules_boston_1775.json. The campaign arc (occupation, missions, enlistments, the
// scored AI, win and loss) runs only while these rules are loaded.
struct CampaignRules {
    YMDH window_start{1775, 4, 19, 0};
    YMDH window_end{1776, 3, 31, 23};
    std::string patriot_hq = "cambridge";
    std::string british_base = "boston";
    std::string heights = "dorchester";
    int hq_held_days = 14;
    int support_floor = 20;
    int support_floor_days = 30;
    int score_per_town = 10;
    int score_support_weight = 1;
    std::string score_tie_goes_to = "british";
    int evacuation_hours = 240;
    YMDH enlist_warning{1775, 12, 1, 0};
    YMDH enlist_expiry{1776, 1, 1, 0};
    int enlist_gold_per_company = 15;
    std::vector<std::string> enlist_exempt;
    int siege_train_gold = 80;
    int siege_train_haul_days = 40;
    int knox_field_guns = 2;    // light pieces of the train, given with their crews to the army that first hauls it
    int knox_crew_units = 2;
    int occupation_hour = 0;
    int ai_garrison_units = 20;
    int ai_garrison_max = 32;
    int ai_min_field = 6;
    int ai_retreat_morale = 30;
    int ai_rest_morale = 3;
    int ai_rest_cap = 80;
    // What the AI raises with its spare gold: class -> the share of its units (percent) it wants of
    // that class. It buys the class furthest short of its share; empty: the cheapest company.
    std::map<std::string, int> ai_recruit_targets;
    // A column detached from the garrison takes this share of its guns, with their crews.
    int ai_detachment_gun_share_pct = 50;
    int v_cartridge = 1, v_powder = 4, v_musket = 3, v_provision = 1;
    int v_hq = 60, v_heights = 30, v_heights_fortified = 200000, v_weak = 20, v_forage = 15, v_mission = 100000;
    int ar_fort_pct = 50, ar_luck_pct = 15, ar_loser_pct = 30, ar_winner_pct = 10, ar_broke_pct = 150;
};

// A mission predicate (closed set, protocol/schemas/missions.yaml), read from the mission's side.
struct MissionPredicate {
    std::string type;
    std::string province;
    std::vector<std::string> provinces;
    std::string faction;  // owner_is: default the mission's side
    int amount = 0;
    int rounds = 0;
    std::string project;
    std::string status;
    int min_fort_level = 0;
    YMDH date{};
    std::vector<MissionPredicate> of;  // all
};
struct MissionEffects {
    int gold = 0;
    int local_support = 0;
    int british_attitude = 0;
    int army_morale = 0;
    int army_readiness = 0;
    std::string stores_province;
    int powder = 0, cartridges = 0, muskets = 0, provisions = 0;
    bool start_evacuation = false;
    std::string summary;
};
struct MissionDef {
    std::string id;
    std::string faction;
    std::string title;
    std::string flavor;
    std::string goal;
    std::string prerequisite;
    bool prerequisite_must_succeed = false;
    bool has_available_from = false;
    YMDH available_from{};
    std::vector<std::string> target_provinces;
    MissionPredicate complete;
    bool has_fail = false;
    MissionPredicate fail;
    int deadline_days = 0;
    bool has_deadline = false;
    YMDH deadline{};
    MissionEffects reward;
    MissionEffects failure;
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
// GAMEPLAY TUNING read from data/battle_rules.json (defaults apply when the file is not loaded).
struct BattleRules {
    int bot_budget = 40;            // soldiers on the field, both sides together
    int field_min = 10;             // per side
    int field_max = 30;             // per side
    float fps_bar = 45.f;           // the frame rate the battle must hold (battle_rules.json field.frame_rate_bar)
    int fps_bar_bots = 60;          // ...with this many bots, 3 samples of 3, before bot_budget may rise
    int soldiers_per_unit = 4;      // soldiers on the field per campaign unit
    int rounds_per_cartridge = 20;  // one magazine cartridge = this many rounds
    int rounds_min = 3;
    int rounds_max = 24;
    int rounds_carried = 12;        // a full cartridge box on the march
    int wounded_pct_of_fallen = 60; // of the men shot or bayoneted down, this share are wounded, the rest killed
    // Battle modes (battle_rules.json "modes").
    std::vector<std::string> mode_landing_edge_types = {"boat", "ferry"};  // arriving by one of these...
    std::string mode_landing_town_tag = "harbor";                          // ...into a town with this tag is a landing
    int mode_assault_min_fort = 1;                                         // a defender behind works of this level or more is assaulted
    std::vector<std::string> mode_conquest_presets = {"concord_bridge", "battle_road"};  // scripted fights keep the open fight
    float assault_seconds_per_post = 120.f;    // the clock gains this when the attacker carries a post
    int assault_garrison_share_pct = 67;       // share of the defender's squads that stand at the post under attack
    int landing_boats = 4;                     // most boats in the first wave
    int landing_men_per_boat = 8;
    float landing_boat_speed_cm = 260.f;       // rowing speed
    float landing_row_out_m = 110.f;           // how far off the landing place the boats start
    float landing_disembark_seconds = 2.f;     // from the keel touching to the men being free to move
    float landing_player_wait_seconds = 20.f;  // the boats wait this long for the player to take his place
    float landing_boat_spacing_cm = 900.f;     // boats land this far apart when the level marks no places
    int guns_crew_units_per_gun = 1;            // units of the crew class that serve one gun
    std::string guns_crew_class = "artilleryman";
    int colours_support_per_capture = 3;  // Support the player gains for colours taken, and loses for his own
    std::string colours_class = "ensign"; // the class whose units each carry one colour
    int defeat_drop_pct = 25;       // share of its boxes a beaten army leaves on the field
    int rout_drop_pct = 50;         // ...when it routed or broke (end_reason routed, army_broke)
    int siege_hours_per_fort_level = 72;
    float army_broke_fraction = 0.6f;
    float army_broke_seconds = 15.f;
    float army_broke_min_elapsed_seconds = 120.f;
    int army_broke_min_squads = 3;
    int army_broke_morale_penalty = 25;
    float posts_swept_seconds = 20.f;
    float posts_swept_min_elapsed_seconds = 60.f;
    float resupply_seconds = 4.f;
    int resupply_held_boxes = 1;
    // The field gun in battle (battle_rules.json "cannon"). Read by the Unreal battle.
    int cannon_crew_men = 4;                   // men who serve one gun
    int cannon_min_crew = 2;                   // fewer cannot work it or move it
    float cannon_sponge_load_seconds = 6.f;    // the drill with a full trained crew: sponge and load,
    float cannon_ram_seconds = 4.f;            // ram home,
    float cannon_prime_lay_seconds = 5.f;      // prime and lay
    float cannon_untrained_scale = 1.6f;       // the drill is this much slower for men who are not gunners
    float cannon_traverse_deg_per_second = 8.f;
    float cannon_round_range_m = 350.f;
    float cannon_round_speed_m = 160.f;        // metres a second (slow enough to be seen)
    float cannon_round_damage = 400.f;
    float cannon_round_spread_degrees = 0.5f;  // half-angle of the cone a laid round shot scatters in
    int cannon_round_pierce = 3;               // men one round shot goes through
    float cannon_knockdown_radius_cm = 220.f;  // men this close to its path are knocked off their feet
    float cannon_knockdown_seconds = 1.2f;
    float cannon_works_damage = 160.f;         // what one round shot batters off a field work
    float cannon_pressure = 0.5f;              // the morale check on a squad under cannon fire...
    float cannon_pressure_seconds = 8.f;       // ...fading over this long
    float cannon_canister_range_m = 70.f;      // inside this the crew fires canister
    int cannon_canister_balls = 20;
    float cannon_canister_cone_degrees = 6.f;
    float cannon_push_speed_cm = 70.f;         // a crew pushes its gun this fast
    float cannon_follow_gap_m = 80.f;          // a gun this far behind its side's nearest squad is pushed up
    float cannon_capture_radius_cm = 500.f;    // enemies this close with no crew by the gun overrun it...
    float cannon_capture_seconds = 4.f;        // ...after this long
    float cannon_crew_radius_cm = 450.f;       // a man this close to a gun of his side is serving it
    float cannon_length_cm = 260.f;            // the piece from trail to muzzle, for fitting the mesh
    // Cover and field works (battle_rules.json "cover", "works"). Read by the Unreal battle.
    float cover_snap_cm = 160.f;        // a man this close behind a wall, fence or work is "at" it
    float works_breastwork_length_cm = 420.f;
    float works_breastwork_height_cm = 120.f;
    float works_breastwork_thickness_cm = 90.f;
    float works_breastwork_health = 300.f;
    float works_redoubt_side_cm = 900.f;
    float works_redoubt_height_cm = 135.f;
    float works_redoubt_health = 900.f;
    float works_cover_built_share = 0.6f;   // a work this far built stops a ball
    float works_man_radius_cm = 2200.f;     // defenders of a post man the works within this distance of it
    float works_slot_ring_cm = 1400.f;      // without marked slots: works stand this far out from the post
    int works_breastwork_slots = 6;         // marked or made-up places for breastworks at a post
    std::vector<int> works_breastworks_by_level = {0, 2, 4, 4};  // standing at the start, by fortification_level
    std::vector<int> works_redoubts_by_level = {0, 0, 0, 1};
    int remnant_men = 3;            // no reserve and this few standing: the remnant quits the field
    int remnant_mark_men = 5;       // ...and from this few down they are marked on the enemy player's HUD
    float remnant_seconds = 15.f;
    // Feel (battle_rules.json "smoke", "sound", "duel"; read only by the Unreal battle).
    float smoke_sight_block_opacity = 0.7f;   // powder smoke this thick on a line of sight hides the man beyond it
    float smoke_sight_radius_scale = 0.4f;    // share of a puff's width that counts as thick
    float sound_speed_m_per_second = 343.f;   // a shot is heard this long after it is seen
    float sound_no_delay_within_m = 20.f;     // no delay worth hearing inside this
    float duel_shove_range_cm = 170.f;
    float duel_shove_recover_seconds = 0.5f;  // the shover's own recovery
    float duel_shove_stagger_seconds = 0.7f;  // the man shoved cannot move
    float duel_shove_open_seconds = 0.9f;     // ...and cannot guard or parry
    float duel_shove_push_cm = 140.f;         // how far he is driven back
    float duel_shove_cooldown_seconds = 1.6f;
    float cannon_knockdown_push_cm = 320.f;   // speed a near miss throws a man off his feet at
    float cannon_dirt_burst_scale = 3.f;      // a round shot's dirt against a musket ball's
};
// Why a side's number is what it is (pre-battle card). Rows with the same label belong together:
// the first is the base value, later ones are signed changes to it.
struct SideFactor {
    std::string label;   // men | field_strength | rounds_per_man | morale | readiness | fortification | ...
    int value = 0;
    std::string source;  // the building or condition that produced the value
};
struct FieldSplit {
    int a = 0;
    int b = 0;
};
struct SideSetup {
    std::string faction;
    std::string army_id;
    std::map<std::string, int> tickets;  // per unit class, after stores gating
    int morale = 0;                     // campaign confidence (feeds UE pressure bias)
    int readiness = 100;
    int cartridge_budget = 0;           // cartridges this side may spend beyond its boxes, from magazine_province
    float reinforcement_scale = 1.f;    // farm/depot: shortens bot respawn (UE)
    std::string retreat_province;
    int field_strength = 0;             // soldiers of this side on the field at one time
    int rounds_per_man = 0;             // rounds in each soldier's box at the start
    int fortification_level = 0;        // fort this side fights behind (defender on its own ground)
    std::string magazine_province;      // the town it holds and fights in; empty = only what it carries
    int guns = 0;                       // field guns this side brings: those its army has crews to serve
    std::vector<SideFactor> factors;
    // The general who commands this side (campaign pass). Empty id: a detachment under its own captain.
    std::string commander_id;
    std::string commander_name;
    std::vector<std::string> commander_traits;
    float commander_aura_radius_m = 0.f;  // his officer's aura, larger than a company officer's
    bool alarmed = false;                 // an agent raised the alarm: every company is under arms at the start
    // What the other side knows of this side before the fight: its true numbers ("exact") or a
    // rough count ("estimate": men_low..men_high). The pre-battle card shows the enemy column from this.
    std::string intel = "exact";
    int intel_men_low = 0;
    int intel_men_high = 0;
    std::string intel_source;
};
struct BattleSetup {
    bool valid = false;
    std::string province_id;
    std::string province_name;
    std::string objective = "green";
    std::string scenario_preset = "lexington_common";  // lexington_common | concord_bridge | battle_road
    std::string mode = "conquest";                     // conquest | assault | landing (choose_battle_mode)
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
    std::vector<std::string> modifier_notes;                 // legacy free-text lines; see sides.*.factors
    int soldiers_per_unit = 4;                               // battle_rules.json; men = units x this
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
    // tickets_exhausted | time_limit | force_preserved | routed | army_broke | posts_swept
    std::string end_reason = "tickets_exhausted";
    std::map<std::string, std::map<std::string, int>> prisoners;  // faction -> class -> captured
    // Men, not units: faction -> class -> killed / wounded / prisoners. When it has any entry the
    // unit fields above (losses, wounded, prisoners) are ignored.
    std::map<std::string, std::map<std::string, MenDown>> casualties;
    // Colours taken on the field: the faction that took them -> how many. Never more than the
    // other side's army had ensign units to carry.
    std::map<std::string, int> colours_captured;
    // Guns overrun and taken: the faction that took them -> how many. Only the side that wins the
    // field keeps what it took; never more than the other side brought.
    std::map<std::string, int> guns_taken;
    // What became of a side's general on the field: faction -> wounded | killed | captured.
    std::map<std::string, std::string> commander_events;
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
