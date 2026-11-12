// CampaignController: plain C++ owner of CampaignState, flow state machine, save/load and trace.
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
    int64_t hour_index = 0;
    std::string flow;
    std::string hash;
    std::string detail;
};

class CampaignController {
public:
    CampaignController() = default;

    void new_campaign(const std::string& scenario_json);
    bool has_campaign() const { return started_; }

    const CampaignState& state() const { return state_; }
    Flow flow() const { return flow_; }
    std::string hash_hex() const { return state_hash_hex(state_); }

    // Campaign: enqueue moves and advance hours
    std::vector<std::string> enqueue_move(const std::string& army_id, const std::string& to_province);
    std::vector<std::string> enqueue_halt(const std::string& army_id);
    TickOutput advance_one_hour();
    // Advance up to n hours or until an interrupt; returns last TickOutput.
    TickOutput advance_hours(int n, bool stop_on_interrupt = true);

    bool battle_pending() const { return state_.pending_battle.active; }
    bool begin_battle(std::string& error);
    const BattleSetup& battle_setup() const { return setup_; }
    bool finish_battle(const BattleResult& r, std::string& error);

    bool begin_diplomacy(const std::string& faction, std::string& error);
    const DiplomacyContext& diplomacy_context() const { return context_; }
    bool finish_diplomacy(const DiplomacyResult* result, std::string& error);
    // Apply an envoy act (e.g. taking offense) without leaving the room.
    bool apply_envoy_act(const DiplomacyResult& result, std::string& error);
    // The player talked and left without agreement.
    bool register_failure(std::string& error);

    bool save(const std::string& path, std::string& error);
    bool load(const std::string& path, std::string& error);

    // Logistics / staff: purchase | request_assistance | requisition | capacity_investment | knox_support | dismiss
    std::vector<std::string> choose_logistics_action(const std::string& letter_id, const std::string& choice);
    std::vector<std::string> resolve_commander_action(const std::string& action);
    bool has_staff_event() const;

    // Province supply pipeline / cartridge craft (commit ≠ receive for shipments).
    std::vector<std::string> approve_purchase_order(const std::string& dest_province, int powder,
                                                    int cartridges, int muskets, int bayonets,
                                                    int provisions, int lead, int paper);
    std::vector<std::string> approve_transfer_order(const std::string& source_province,
                                                    const std::string& dest_province, int powder,
                                                    int cartridges, int muskets, int bayonets,
                                                    int provisions, int lead, int paper);
    std::vector<std::string> develop_province_action(const std::string& province_id,
                                                     const std::string& track);
    std::vector<std::string> start_construction_action(const std::string& province_id,
                                                       const std::string& building_id);
    std::vector<std::string> cancel_construction_action(const std::string& province_id,
                                                        const std::string& building_id);
    std::vector<std::string> demolish_building_action(const std::string& province_id,
                                                     const std::string& building_id);
    std::vector<std::string> start_recruitment_action(const std::string& province_id,
                                                      const std::string& unit_id);
    std::vector<std::string> cancel_recruitment_action(const std::string& province_id);
    std::vector<std::string> take_loan_action();
    std::vector<std::string> repay_loan_action();
    std::vector<std::string> start_cartridge_craft(const std::string& province_id,
                                                   const std::string& army_id, int workers, int powder,
                                                   int lead, int paper);

    const std::vector<TraceEntry>& trace() const { return trace_; }
    std::string trace_json() const;
    void record(const std::string& event, const std::string& detail = std::string());

private:
    bool started_ = false;
    CampaignState state_;
    Flow flow_ = Flow::Campaign;
    BattleSetup setup_;
    DiplomacyContext context_;
    std::vector<TraceEntry> trace_;
};

}  // namespace pa250
