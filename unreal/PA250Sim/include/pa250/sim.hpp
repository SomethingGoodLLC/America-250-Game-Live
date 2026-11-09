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

}  // namespace pa250
