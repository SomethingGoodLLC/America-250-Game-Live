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
    e.hour_index = state_.now.hour_index;
    e.flow = flow_name(flow_);
    e.hash = started_ ? state_hash_hex(state_) : "";
    e.detail = detail;
    trace_.push_back(e);
}

std::string CampaignController::trace_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : trace_)
        arr.push_back({{"seq", e.seq},
                       {"event", e.event},
                       {"hour_index", e.hour_index},
                       {"flow", e.flow},
                       {"hash", e.hash},
                       {"detail", e.detail}});
    return arr.dump(1);
}

void CampaignController::new_campaign(const std::string& scenario_json) {
    state_ = load_scenario(scenario_json);
    ensure_logistics_defaults(state_);
    started_ = true;
    flow_ = Flow::Campaign;
    setup_ = BattleSetup{};
    context_ = DiplomacyContext{};
    record("new_campaign", state_.scenario);
}

std::vector<std::string> CampaignController::enqueue_move(const std::string& army_id,
                                                          const std::string& to_province) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    auto ait = state_.armies.find(army_id);
    if (ait != state_.armies.end() && ait->second.faction != state_.player_faction)
        return {detail::err("not_player_army", army_id)};
    PathPlan plan = plan_path(state_, army_id, to_province);
    if (!plan.ok) return {detail::err(plan.error.empty() ? "no_route" : plan.error)};
    CommandPayload p;
    p.kind = CommandKind::MoveArmy;
    p.army_id = army_id;
    p.dest_province = to_province;
    p.edge_path = plan.edges;
    state_ = enqueue_command(state_, p);
    return {};
}

std::vector<std::string> CampaignController::enqueue_halt(const std::string& army_id) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    auto ait = state_.armies.find(army_id);
    if (ait == state_.armies.end()) return {detail::err("unknown_army", army_id)};
    if (ait->second.faction != state_.player_faction) return {detail::err("not_player_army", army_id)};
    CommandPayload p;
    p.kind = CommandKind::HaltArmy;
    p.army_id = army_id;
    state_ = enqueue_command(state_, p);
    return {};
}

TickOutput CampaignController::advance_one_hour() {
    TickOutput out;
    if (!started_ || flow_ != Flow::Campaign) {
        out.state = state_;
        return out;
    }
    out = advance_hour(state_);
    state_ = out.state;
    if (!out.interrupts.empty()) {
        std::string d;
        for (auto i : out.interrupts) d += std::to_string(static_cast<int>(i)) + ",";
        record("interrupt", d);
    } else {
        record("advance_hour");
    }
    return out;
}

TickOutput CampaignController::advance_hours(int n, bool stop_on_interrupt) {
    TickOutput last;
    last.state = state_;
    for (int i = 0; i < n; ++i) {
        last = advance_one_hour();
        if (stop_on_interrupt && !last.interrupts.empty()) break;
        if (state_.pending_battle.active) break;
    }
    return last;
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
    // Apply at this hour boundary. advance_hour will not consume commands while a battle is
    // pending, so the result cannot wait for a later tick.
    state_ = apply_battle_result(state_, r);
    {
        std::vector<Interrupt> briefings;
        tick_logistics(state_, briefings);  // aftermath letter / EventPopup without waiting an hour
    }
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
        state_ = apply_diplomacy(state_, *result);
        detail = state_hash(state_) == before ? "no_change" : "applied";
    }
    context_ = DiplomacyContext{};
    flow_ = Flow::Campaign;
    record("finish_diplomacy", detail);
    return true;
}

bool CampaignController::apply_envoy_act(const DiplomacyResult& result, std::string& error) {
    if (flow_ != Flow::Diplomacy || result.faction != context_.faction) {
        error = "no matching diplomacy in progress";
        return false;
    }
    state_ = apply_diplomacy(state_, result);
    context_ = build_diplomacy_context(state_, context_.faction);
    record("envoy_act", result.proposal.clauses.empty() ? "" : result.proposal.clauses[0].type);
    return true;
}

bool CampaignController::register_failure(std::string& error) {
    if (flow_ != Flow::Diplomacy) {
        error = "only in diplomacy flow";
        return false;
    }
    state_ = register_diplomacy_failure(state_, context_.faction);
    context_ = build_diplomacy_context(state_, context_.faction);
    record("diplomacy_failure", context_.mood);
    return true;
}

bool CampaignController::save(const std::string& path, std::string& error) {
    if (!started_ || flow_ != Flow::Campaign) {
        error = "save only in campaign flow at an hour boundary";
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
        ensure_logistics_defaults(state_);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    started_ = true;
    flow_ = Flow::Campaign;
    setup_ = BattleSetup{};
    context_ = DiplomacyContext{};
    record("load", path);
    return true;
}

std::vector<std::string> CampaignController::choose_logistics_action(const std::string& letter_id,
                                                                    const std::string& choice) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    state_ = choose_logistics(state_, letter_id, choice);
    record("logistics", choice);
    return {};
}

std::vector<std::string> CampaignController::resolve_commander_action(const std::string& action) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    state_ = resolve_commander_request(state_, action);
    record("commander", action);
    return {};
}

bool CampaignController::has_staff_event() const {
    if (!state_.pending_event_id.empty()) return true;
    for (const auto& c : state_.staff.queue)
        if (c.awaiting_choice) return true;
    return false;
}

std::vector<std::string> CampaignController::approve_purchase_order(
    const std::string& dest_province, int powder, int cartridges, int muskets, int bayonets,
    int provisions, int lead, int paper) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    std::string error;
    CampaignState next =
        approve_purchase(state_, dest_province, powder, cartridges, muskets, bayonets, provisions,
                         lead, paper, error);
    if (!error.empty()) return {detail::err(error)};
    state_ = next;
    record("purchase", dest_province);
    return {};
}

std::vector<std::string> CampaignController::approve_transfer_order(
    const std::string& source_province, const std::string& dest_province, int powder, int cartridges,
    int muskets, int bayonets, int provisions, int lead, int paper) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    std::string error;
    CampaignState next =
        approve_transfer(state_, source_province, dest_province, powder, cartridges, muskets,
                         bayonets, provisions, lead, paper, error);
    if (!error.empty()) return {detail::err(error)};
    state_ = next;
    record("transfer", source_province + "->" + dest_province);
    return {};
}

std::vector<std::string> CampaignController::develop_province_action(const std::string& province_id,
                                                                     const std::string& track) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    std::string error;
    CampaignState next = develop_province(state_, province_id, track, error);
    if (!error.empty()) return {detail::err(error)};
    state_ = next;
    record("develop", province_id + ":" + track);
    return {};
}

std::vector<std::string> CampaignController::start_cartridge_craft(const std::string& province_id,
                                                                  const std::string& army_id,
                                                                  int workers, int powder, int lead,
                                                                  int paper) {
    if (!started_ || flow_ != Flow::Campaign) return {detail::err("not_in_campaign")};
    if (state_.pending_battle.active) return {detail::err("battle_pending")};
    std::string error;
    CampaignState next =
        start_cartridge_job(state_, province_id, army_id, workers, powder, lead, paper, error);
    if (!error.empty()) return {detail::err(error)};
    state_ = next;
    record("cartridge_job", province_id);
    return {};
}

}  // namespace pa250
