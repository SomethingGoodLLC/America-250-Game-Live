// PA250Sim public API. All state transitions are pure functions.
#pragma once
#include <string>
#include <vector>

#include "pa250/types.hpp"

namespace pa250 {

// ---- Calendar -----------------------------------------------------------
YMDH to_ymdh(GameTime t);
GameTime from_ymdh(YMDH v);

// ---- Scenario / persistence --------------------------------------------
std::string read_text_file(const std::string& path);                // throws std::runtime_error
bool write_text_file(const std::string& path, const std::string& text);
CampaignState load_scenario(const std::string& json_text);          // throws std::runtime_error
std::string serialize(const CampaignState& state);                  // canonical single-file JSON save
CampaignState deserialize(const std::string& json_text);            // throws std::runtime_error
uint64_t state_hash(const CampaignState& state);                    // FNV-1a 64 of serialize()
std::string state_hash_hex(const CampaignState& state);
void rebuild_adjacency(CampaignState& s);                           // from edges
std::vector<std::string> validate_scenario_graph(const CampaignState& s);

// ---- Hourly clock / commands / paths ------------------------------------
TickOutput advance_hour(const CampaignState& s);  // pure; consumes pending_commands due this hour
CampaignState enqueue_command(const CampaignState& s, CommandPayload p);  // apply_on_hour=now+1
PathPlan plan_path(const CampaignState& s, const std::string& army_id, const std::string& dest);

// Order validation (adjacency / ceasefire) for UI hints
std::vector<std::string> validate_order(const CampaignState& state, const Order& order);

// ---- Battle ------------------------------------------------------------
std::string choose_retreat(const CampaignState& state, const std::string& province,
                           const std::string& loser_faction);
std::string scenario_preset_for_province(const std::string& province_id);
BattleSetup build_battle_setup(const CampaignState& state, const std::string& province);
CampaignState apply_battle_result(const CampaignState& state, const BattleResult& result);

// ---- Logistics / correspondence / staff (pure) -------------------------
// Ensure default stores/staff exist (called from load_scenario path).
void ensure_logistics_defaults(CampaignState& s);
// Soft-migrate older schema-2 saves (faction stores → province_stores, etc.).
void migrate_campaign_state(CampaignState& s);
// Gameplay-tuning file (not historical fact). Optional; defaults apply if unloaded.
void load_logistics_tuning(const std::string& json_text);
bool logistics_tuning_loaded();
int workshop_cartridges_per_level();
int farm_readiness_per_level();
int depot_readiness_per_level();

// Province ledger helpers.
ProvinceStores& province_ledger(CampaignState& s, const std::string& province_id);
const ProvinceStores& province_ledger(const CampaignState& s, const std::string& province_id);
// Computed rollup of province ledgers owned by faction (display only; not a write path).
FactionStores rollup_faction_stores(const CampaignState& s, const std::string& faction);
std::string default_depot_province(const CampaignState& s, const std::string& faction);

// Purchase: debit gold, create in-transit shipment; destination unchanged until delivery.
// Transfer: debit source immediately; credit dest only on delivery.
CampaignState approve_purchase(const CampaignState& state, const std::string& dest_province,
                               int powder, int cartridges, int muskets, int bayonets, int provisions,
                               int lead, int paper, std::string& error);
CampaignState approve_transfer(const CampaignState& state, const std::string& source_province,
                               const std::string& dest_province, int powder, int cartridges, int muskets,
                               int bayonets, int provisions, int lead, int paper, std::string& error);
// Gold a purchase order would cost (the price approve_purchase charges). Never mutates state.
int purchase_cost(int powder, int cartridges, int muskets, int bayonets, int provisions, int lead,
                  int paper);
// Province development (EU4-style tracks + City View discrete buildings).
int province_development(const Province& p);  // 1 + farm+workshop+depot+fort
int effective_supply_value(const Province& p);  // supply_value + farm_level
int development_cost(const std::string& track, int current_level);  // gold, -1 = unknown track
int development_max_level();
// Legacy name: now the same timed start as start_construction (gold on start, level on completion).
CampaignState develop_province(const CampaignState& state, const std::string& province_id,
                               const std::string& track, std::string& error);
// Player-facing preview strings (costs/effects) for HUD tooltips — never mutate state.
std::string development_preview(const CampaignState& state, const std::string& province_id,
                                const std::string& track);
std::string aftermath_choice_preview(const CampaignState& state, const std::string& choice);

// City View building catalog / sizes (optional JSON; defaults apply if unloaded).
void load_buildings_catalog(const std::string& json_text);
void load_province_sizes(const std::string& json_text);
bool buildings_catalog_loaded();
bool province_sizes_loaded();
int province_slot_count(const std::string& province_id);
int province_slots_occupied(const CampaignState& state, const std::string& province_id);
int building_level(const Province& p, const std::string& building_id);
bool building_present(const Province& p, const std::string& building_id);
int building_cost(const std::string& building_id, int current_level);  // -1 unknown
int building_build_hours(const std::string& building_id);
std::string building_preview(const CampaignState& state, const std::string& province_id,
                             const std::string& building_id);
// Start a timed build or upgrade. Gold charged on start. error codes match develop_province plus
// no_free_slot / already_present / already_building / unknown_building.
CampaignState start_construction(const CampaignState& state, const std::string& province_id,
                                 const std::string& building_id, std::string& error);
CampaignState cancel_construction(const CampaignState& state, const std::string& province_id,
                                  const std::string& building_id, std::string& error);
CampaignState demolish_building(const CampaignState& state, const std::string& province_id,
                                const std::string& building_id, std::string& error);
void tick_construction_jobs(CampaignState& s);
void buildings_monthly_tick(CampaignState& s);  // upkeep + church Support drip
int building_battle_morale(const Province& p);  // Meeting House morale for battle setup
// Raising troops (GAME INVENTION). A town the player holds musters one company at a time: gold and
// stores are charged on start, the men arrive after the catalog's days, drawn from the town's
// monthly manpower. Militia need a Militia Muster in the town; regulars need a Fort and muskets
// from the town magazine. A company belongs to a faction (the other side's companies are
// unknown_unit); rifle companies wait for the Continental chapter (not_yet). Unit classes in play:
// militia, line_infantry, rifleman, officer, dragoon (Continental); line_infantry, grenadier, light_infantry,
// officer, dragoon (British). Dragoons are a GAME INVENTION for 1775 (they ride, then fight on foot). error codes: unknown_unit / unknown_province / not_your_province / not_yet /
// battle_pending / already_mustering / needs_building / no_manpower / insufficient_gold /
// insufficient_muskets / insufficient_provisions / no_muster.
int province_manpower_cap(const CampaignState& state, const std::string& province_id);
std::string recruitment_preview(const CampaignState& state, const std::string& province_id,
                                const std::string& unit_id);
CampaignState start_recruitment(const CampaignState& state, const std::string& province_id,
                                const std::string& unit_id, std::string& error);
CampaignState cancel_recruitment(const CampaignState& state, const std::string& province_id,
                                 std::string& error);
void tick_recruitment_jobs(CampaignState& s);
// A company the player raised that stands idle in a town with another of the player's armies is
// absorbed by it (a battle is set up between one army per side). Scripted armies are never absorbed.
void merge_idle_player_armies(CampaignState& s);
// Treasury (GAME INVENTION, after the bills of credit Massachusetts issued in 1775). On the 1st of
// each month the province settles: town levies, depot trade and workshop output come in; army
// pay, building upkeep and interest on debt go out. A shortfall is borrowed automatically up to
// the credit limit; past it the army goes unpaid (morale and Support fall).
int town_levy_gold(const Province& p);                    // monthly gold from one town
int treasury_credit_limit(const CampaignState& state);    // most the province may owe
int monthly_net_gold(const CampaignState& state);         // projected income - expenses
std::string treasury_json(const CampaignState& state);    // balance, debt, every line of the budget
// Borrow / repay one loan step. error: battle_pending / credit_exhausted / no_debt / insufficient_gold.
CampaignState take_loan(const CampaignState& state, std::string& error);
CampaignState repay_loan(const CampaignState& state, std::string& error);
void settle_monthly_budget(CampaignState& s);  // called from buildings_monthly_tick
// The other side keeps the same accounts (levies, depot trade, workshop muskets, army pay, building
// upkeep) from its own war chest, plus a subsidy and provisions by sea while it controls the
// harbour and holds its headquarters town. It cannot borrow: an empty chest means an unpaid army
// (morale falls). With gold to spare it raises one company a month from the catalog, armed and fed
// from its own magazine. Called from buildings_monthly_tick after the player's settlement.
std::string enemy_faction(const CampaignState& state);
int enemy_monthly_net_gold(const CampaignState& state);
void settle_enemy_budget(CampaignState& s);
// Read-only City View snapshot (slots, options, totals) for UE HUD.
std::string city_view_json(const CampaignState& state, const std::string& province_id);

// Start a cartridge job: reserves powder/lead/paper and commits workers until complete_on_hour.
CampaignState start_cartridge_job(const CampaignState& state, const std::string& province_id,
                                  const std::string& army_id, int workers, int powder, int lead, int paper,
                                  std::string& error);
void tick_cartridge_jobs(CampaignState& s);

// Player chooses among purchase | request_assistance | requisition | capacity_investment | knox_support | dismiss
// | source_decision choice ids | acknowledge for historical_notice.
// letter_id empty → use the pending aftermath choice letter.
CampaignState choose_logistics(const CampaignState& state, const std::string& letter_id,
                               const std::string& choice);
// Clarify / counterorder / escalate a detached commander's request.
CampaignState resolve_commander_request(const CampaignState& state, const std::string& action);
// Tick correspondence deliveries and dated chapter/project gates; may add EventPopup.
void tick_logistics(CampaignState& s, std::vector<Interrupt>& interrupts);

// Opening-war correspondence catalog (immutable). Load once from data JSON text.
void load_correspondence_catalog(const std::string& json_text);
bool correspondence_catalog_loaded();
const CorrespondenceCatalogEntry* find_correspondence_entry(const std::string& source_id);
std::vector<CorrespondenceCatalogEntry> correspondence_catalog_entries();
void ensure_donor_reserve_defaults(CampaignState& s);
void seed_opening_war_correspondence(CampaignState& s, std::vector<Interrupt>* interrupts);
void release_due_personnel_commitments(CampaignState& s);
void deliver_due_shipments(CampaignState& s, std::vector<Interrupt>* interrupts);
CampaignState apply_source_decision(const CampaignState& state, const std::string& letter_id,
                                    const std::string& choice);

// ---- Diplomacy ---------------------------------------------------------
DiplomacyContext build_diplomacy_context(const CampaignState& state, const std::string& faction);
// Returns error strings of the form "code: detail"; empty means valid. spoken_line is never read.
std::vector<std::string> validate_proposal(const DiplomacyContext& context,
                                           const DiplomacyProposal& proposal);
CampaignState apply_diplomacy(const CampaignState& state, const DiplomacyResult& result);
// Talks ended without agreement: the envoy likes the player less and may snub them.
CampaignState register_diplomacy_failure(const CampaignState& state, const std::string& faction);

// ---- JSON bridges (strings in, strings out; nlohmann stays out of this header) ----
std::string context_to_json(const DiplomacyContext& context);
DiplomacyContext context_from_json(const std::string& json_text);   // throws
// Parses a proposal; structural problems come back as "code: detail" errors.
std::vector<std::string> proposal_from_json(const std::string& json_text, DiplomacyProposal& out);
std::string proposal_to_json(const DiplomacyProposal& proposal);
std::string setup_to_json(const BattleSetup& setup);
BattleSetup setup_from_json(const std::string& json_text);          // throws
std::string result_to_json(const BattleResult& result);
BattleResult result_from_json(const std::string& json_text);        // throws
std::string report_to_json(const BattleReport& report);

}  // namespace pa250
