#include "internal.hpp"

namespace pa250 {

std::string choose_retreat(const CampaignState& state, const std::string& province,
                           const std::string& loser_faction) {
    auto pit = state.provinces.find(province);
    if (pit == state.provinces.end()) return "";
    auto enemy_in = [&](const std::string& prov) {
        for (const auto& kv : state.armies)
            if (kv.second.province == prov && kv.second.faction != loser_faction) return true;
        return false;
    };
    // Prefer where the loser came from.
    for (const auto& kv : state.armies) {
        const Army& a = kv.second;
        if (a.faction == loser_faction && a.province == province && !a.previous_province.empty() &&
            a.previous_province != province && detail::contains(pit->second.adjacent, a.previous_province) &&
            !enemy_in(a.previous_province))
            return a.previous_province;
    }
    for (const auto& n : pit->second.adjacent)
        if (state.provinces.at(n).owner == loser_faction && !enemy_in(n)) return n;
    for (const auto& n : pit->second.adjacent)
        if (!enemy_in(n)) return n;
    return "";
}

BattleSetup build_battle_setup(const CampaignState& state, const std::string& province) {
    BattleSetup b;
    const PendingBattle& pb = state.pending_battle;
    if (!pb.active || pb.province != province) return b;
    auto pit = state.provinces.find(province);
    auto ait = state.armies.find(pb.attacker_army);
    auto dit = state.armies.find(pb.defender_army);
    if (pit == state.provinces.end() || ait == state.armies.end() || dit == state.armies.end()) return b;
    b.valid = true;
    b.province_id = province;
    b.province_name = pit->second.name;
    b.player_faction = state.player_faction;
    b.attacker_faction = ait->second.faction;
    b.defender_faction = dit->second.faction;
    b.seed = detail::mix64(state.seed ^ detail::mix64(static_cast<uint64_t>(state.turn)) ^ detail::fnv1a(province));
    for (const Army* a : {&ait->second, &dit->second}) {
        SideSetup s;
        s.faction = a->faction;
        s.army_id = a->id;
        s.tickets = a->units;
        s.morale = a->morale;
        s.retreat_province = choose_retreat(state, province, a->faction);
        b.sides[a->faction] = s;
    }
    return b;
}

CampaignState apply_battle_result(const CampaignState& in, const BattleResult& r) {
    const PendingBattle pb = in.pending_battle;
    // A result that does not match the pending battle is not a command: leave state, log and hash alone.
    if (!pb.active || pb.province != r.province_id) return in;
    if (r.winner != kBritish && r.winner != kContinental) return in;
    CampaignState s = in;
    const std::string loser = detail::other_faction(r.winner);

    // Losses per unit class, clamped to what the army has.
    for (const auto& fk : r.losses) {
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = s.armies.find(aid);
            if (it == s.armies.end() || it->second.faction != fk.first) continue;
            for (const auto& ck : fk.second) {
                auto uit = it->second.units.find(ck.first);
                if (uit == it->second.units.end() || ck.second <= 0) continue;
                uit->second = std::max(0, uit->second - ck.second);
            }
        }
    }

    // Morale, retreat, destruction.
    std::string retreat = r.retreat_province;
    {
        auto pit = s.provinces.find(r.province_id);
        bool ok = pit != s.provinces.end() && detail::contains(pit->second.adjacent, retreat);
        if (ok)  // retreat must not be into an enemy army
            for (const auto& kv : s.armies)
                if (kv.second.province == retreat && kv.second.faction == r.winner) ok = false;
        if (!ok) retreat = choose_retreat(in, r.province_id, loser);
    }
    for (auto& kv : s.armies) {
        Army& a = kv.second;
        if (a.province != r.province_id) continue;
        if (a.faction == r.winner) {
            a.morale = detail::clamp_int(a.morale + 5, 0, 100);
        } else {
            a.morale = detail::clamp_int(a.morale - 20, 0, 100);
            if (detail::total_units(a) > 0 && !retreat.empty()) {
                a.previous_province = a.province;
                a.province = retreat;
            }
        }
    }
    for (auto it = s.armies.begin(); it != s.armies.end();) {
        if (detail::total_units(it->second) <= 0) {
            detail::add_log(s, "army destroyed: " + it->first);
            it = s.armies.erase(it);
        } else {
            ++it;
        }
    }

    // Ownership: only if the winner held the objective.
    if (r.objective_holder == r.winner) s.provinces.at(r.province_id).owner = r.winner;

    s.memory.push_back({s.turn, r.winner, "Battle of " + s.provinces.at(r.province_id).name + " won by " + r.winner});
    s.pending_battle = PendingBattle{};
    detail::add_log(s, "battle at " + r.province_id + " won by " + r.winner);
    return s;
}

}  // namespace pa250
