#include <filesystem>
#include <stdexcept>

#include "internal.hpp"
#include "nlohmann/json.hpp"
#include "pa250/controller.hpp"

namespace pa250 {

const char* flow_name(Flow f) {
    switch (f) {
        case Flow::Campaign: return "campaign";
        case Flow::Battle: return "battle";
        case Flow::Diplomacy: return "diplomacy";
    }
    return "?";
}

void CampaignController::record(const std::string& event, const std::string& detail) {
    TraceEntry e;
    e.seq = static_cast<int>(trace_.size());
    e.event = event;
    e.turn = state_.turn;
    e.flow = flow_name(flow_);
    e.hash = started_ ? state_hash_hex(state_) : "";
    e.detail = detail;
    trace_.push_back(e);
}

std::string CampaignController::trace_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : trace_)
        arr.push_back({{"seq", e.seq}, {"event", e.event}, {"turn", e.turn}, {"flow", e.flow},
                       {"hash", e.hash}, {"detail", e.detail}});
    return arr.dump(1);
}

void CampaignController::new_campaign(const std::string& scenario_json) {
    state_ = load_scenario(scenario_json);
    started_ = true;
    flow_ = Flow::Campaign;
    orders_ = Orders{};
    setup_ = BattleSetup{};
    context_ = DiplomacyContext{};
    record("new_campaign", state_.scenario);
}

std::vector<std::string> CampaignController::queue_move(const std::string& army_id, const std::string& to) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    auto ait = state_.armies.find(army_id);
    if (ait != state_.armies.end() && ait->second.faction != state_.player_faction)
        return {detail::err("not_player_army", army_id)};
    Order o{army_id, to};
    auto errs = validate_order(state_, o);
    if (!errs.empty()) return errs;
    for (auto& existing : orders_.orders)
        if (existing.army_id == army_id) {
            existing = o;
            return {};
        }
    orders_.orders.push_back(o);
    return {};
}

bool CampaignController::end_turn(std::string& error) {
    if (!started_ || flow_ != Flow::Campaign) {
        error = "end_turn only in campaign flow";
        return false;
    }
    if (state_.pending_battle.active) {
        error = "resolve the pending battle first";
        return false;
    }
    state_ = resolve_turn(state_, orders_);
    orders_.orders.clear();
    record("end_turn", state_.pending_battle.active ? "battle_pending:" + state_.pending_battle.province : "");
    return true;
}

bool CampaignController::begin_battle(std::string& error) {
    if (!started_ || flow_ != Flow::Campaign) {
        error = "begin_battle only from campaign flow";
        return false;
    }
    if (!state_.pending_battle.active) {
        error = "no pending battle";
        return false;
    }
    setup_ = build_battle_setup(state_, state_.pending_battle.province);
    if (!setup_.valid) {
        error = "could not build battle setup";
        return false;
    }
    flow_ = Flow::Battle;
    record("begin_battle", setup_.province_id);
    return true;
}

bool CampaignController::finish_battle(const BattleResult& r, std::string& error) {
    if (flow_ != Flow::Battle) {
        error = "finish_battle only in battle flow";
        return false;
    }
    if (r.province_id != setup_.province_id || (r.winner != kBritish && r.winner != kContinental)) {
        error = "battle result does not match the pending battle";
        return false;
    }
    state_ = apply_battle_result(state_, r);
    setup_ = BattleSetup{};
    flow_ = Flow::Campaign;
    record("finish_battle", "winner:" + r.winner);
    return true;
}

bool CampaignController::begin_diplomacy(const std::string& faction, std::string& error) {
    if (!started_ || flow_ != Flow::Campaign) {
        error = "diplomacy only from campaign flow";
        return false;
    }
    if (state_.pending_battle.active) {
        error = "resolve the pending battle first";
        return false;
    }
    if (!state_.factions.count(faction) || faction == state_.player_faction) {
        error = "unknown or non-envoy faction";
        return false;
    }
    context_ = build_diplomacy_context(state_, faction);
    flow_ = Flow::Diplomacy;
    record("begin_diplomacy", faction);
    return true;
}

bool CampaignController::finish_diplomacy(const DiplomacyResult* result, std::string& error) {
    if (flow_ != Flow::Diplomacy) {
        error = "finish_diplomacy only in diplomacy flow";
        return false;
    }
    std::string detail = "left";
    if (result) {
        if (result->faction != context_.faction) {
            error = "result is for a different envoy";
            return false;
        }
        const uint64_t before = state_hash(state_);
        state_ = apply_diplomacy(state_, *result);  // sim re-validates; invalid results are a no-op
        detail = state_hash(state_) == before ? "no_change" : "applied";
    }
    context_ = DiplomacyContext{};
    flow_ = Flow::Campaign;
    record("finish_diplomacy", detail);
    return true;
}

bool CampaignController::save(const std::string& path, std::string& error) {
    if (!started_ || flow_ != Flow::Campaign) {
        error = "save only at a turn boundary in campaign flow";
        return false;
    }
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (!write_text_file(path, serialize(state_))) {
        error = "cannot write " + path;
        return false;
    }
    record("save", path);
    return true;
}

bool CampaignController::load(const std::string& path, std::string& error) {
    try {
        CampaignState loaded = deserialize(read_text_file(path));
        state_ = loaded;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    started_ = true;
    flow_ = Flow::Campaign;
    orders_ = Orders{};
    setup_ = BattleSetup{};
    context_ = DiplomacyContext{};
    record("load", path);
    return true;
}

}  // namespace pa250
