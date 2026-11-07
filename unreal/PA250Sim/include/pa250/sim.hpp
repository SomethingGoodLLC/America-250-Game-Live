// PA250Sim public API. All state transitions are pure functions.
#pragma once
#include <string>
#include <vector>

#include "pa250/types.hpp"

namespace pa250 {

// ---- Scenario / persistence --------------------------------------------
std::string read_text_file(const std::string& path);                // throws std::runtime_error
bool write_text_file(const std::string& path, const std::string& text);
CampaignState load_scenario(const std::string& json_text);          // throws std::runtime_error
std::string serialize(const CampaignState& state);                  // canonical single-file JSON save
CampaignState deserialize(const std::string& json_text);            // throws std::runtime_error
uint64_t state_hash(const CampaignState& state);                    // FNV-1a 64 of serialize()
std::string state_hash_hex(const CampaignState& state);

// ---- Turn resolution ----------------------------------------------------
std::vector<std::string> validate_order(const CampaignState& state, const Order& order);
CampaignState resolve_turn(const CampaignState& state, const Orders& orders);

// ---- Battle ------------------------------------------------------------
std::string choose_retreat(const CampaignState& state, const std::string& province,
                           const std::string& loser_faction);
BattleSetup build_battle_setup(const CampaignState& state, const std::string& province);
CampaignState apply_battle_result(const CampaignState& state, const BattleResult& result);

// ---- Diplomacy ---------------------------------------------------------
DiplomacyContext build_diplomacy_context(const CampaignState& state, const std::string& faction);
// Returns error strings of the form "code: detail"; empty means valid. spoken_line is never read.
std::vector<std::string> validate_proposal(const DiplomacyContext& context,
                                           const DiplomacyProposal& proposal);
CampaignState apply_diplomacy(const CampaignState& state, const DiplomacyResult& result);

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
