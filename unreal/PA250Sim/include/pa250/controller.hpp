// CampaignController: plain C++ owner of CampaignState, the Campaign/Battle/Diplomacy flow state
// machine, queued orders, pending BattleSetup / DiplomacyContext, save/load and a transition trace.
// UPA250Subsystem (Unreal) is a thin wrapper around this class.
#pragma once
#include <string>
#include <vector>

#include "pa250/sim.hpp"

namespace pa250 {

enum class Flow { Campaign, Battle, Diplomacy };
const char* flow_name(Flow f);

struct TraceEntry {
    int seq = 0;
    std::string event;
    int turn = 0;
    std::string flow;
    std::string hash;    // state hash after the event
    std::string detail;
};

class CampaignController {
public:
    CampaignController() = default;

    // New campaign from scenario JSON text. Throws std::runtime_error on bad data.
    void new_campaign(const std::string& scenario_json);
    bool has_campaign() const { return started_; }

    const CampaignState& state() const { return state_; }
    Flow flow() const { return flow_; }
    std::string hash_hex() const { return state_hash_hex(state_); }

    // ---- Campaign: orders and turns (Flow::Campaign only) ----
    // Queues (or replaces) a move for an army. Returns errors; empty = queued.
    std::vector<std::string> queue_move(const std::string& army_id, const std::string& to_province);
    void clear_orders() { orders_.orders.clear(); }
    const Orders& orders() const { return orders_; }
    bool end_turn(std::string& error);

    // ---- Battle ----
    bool battle_pending() const { return state_.pending_battle.active; }
    bool begin_battle(std::string& error);                    // Campaign -> Battle
    const BattleSetup& battle_setup() const { return setup_; }
    bool finish_battle(const BattleResult& r, std::string& error);  // Battle -> Campaign

    // ---- Diplomacy ----
    bool begin_diplomacy(const std::string& faction, std::string& error);  // Campaign -> Diplomacy
    const DiplomacyContext& diplomacy_context() const { return context_; }
    // `result` may be null (Leave): returns to Campaign with no state change.
    bool finish_diplomacy(const DiplomacyResult* result, std::string& error);  // Diplomacy -> Campaign

    // ---- Persistence (Campaign flow, i.e. a turn boundary) ----
    bool save(const std::string& path, std::string& error);
    bool load(const std::string& path, std::string& error);

    // ---- Trace (autoplay / debugging) ----
    const std::vector<TraceEntry>& trace() const { return trace_; }
    std::string trace_json() const;
    void record(const std::string& event, const std::string& detail = std::string());

private:
    bool started_ = false;
    CampaignState state_;
    Flow flow_ = Flow::Campaign;
    Orders orders_;
    BattleSetup setup_;
    DiplomacyContext context_;
    std::vector<TraceEntry> trace_;
};

}  // namespace pa250
