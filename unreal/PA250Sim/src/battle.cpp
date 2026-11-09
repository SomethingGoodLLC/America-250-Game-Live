#include "internal.hpp"

namespace pa250 {
namespace {

int ticket_total(const std::map<std::string, int>& tickets) {
    int n = 0;
    for (const auto& kv : tickets) n += std::max(0, kv.second);
    return n;
}

std::map<std::string, int> gate_tickets_by_stores(const CampaignState& state, const Army& army) {
    std::map<std::string, int> tickets = army.units;
    const auto sit = state.stores.find(army.faction);
    if (sit == state.stores.end()) return tickets;
    const FactionStores& st = sit->second;

    int total = 0;
    for (const auto& kv : tickets) total += std::max(0, kv.second);
    if (total <= 0) return tickets;

    // Arms: need roughly one serviceable arm per unit fielded.
    if (st.arms < total && total > 0) {
        const double scale = static_cast<double>(std::max(0, st.arms)) / static_cast<double>(total);
        for (auto& kv : tickets) {
            if (kv.first == "militia" || kv.second > 0)
                kv.second = std::max(0, static_cast<int>(kv.second * scale + 0.5));
        }
    }

    // Powder shortage mainly stands down militia (line keeps a minimum).
    auto mit = tickets.find("militia");
    if (mit != tickets.end() && mit->second > 0) {
        const int powder_cap = std::max(0, st.powder);
        if (powder_cap < mit->second) mit->second = powder_cap;
    }

    // Readiness / fatigue soft-cap (aggregate, not personal inventory).
    if (army.readiness < 50 || army.fatigue > 60) {
        for (auto& kv : tickets) {
            if (kv.first == "militia") kv.second = (kv.second * 3) / 4;
        }
    }

    // Temporary personnel commitments hold paper units out of fielding without touching
    // Army.units, arms, readiness, or morale.
    for (const auto& pc : state.progress.personnel_commitments) {
        if (pc.army_id != army.id || pc.count <= 0) continue;
        if (pc.release_on_hour > 0 && state.now.hour_index >= pc.release_on_hour) continue;
        auto uit = tickets.find(pc.unit_class);
        if (uit == tickets.end()) continue;
        uit->second = std::max(0, uit->second - pc.count);
    }

    // Never gate a still-existing army to a zero ticket pool: that made the next fight
    // end on the first Tick as tickets_exhausted before anyone spawned.
    if (ticket_total(tickets) <= 0 && total > 0) {
        for (const auto& kv : army.units) {
            if (kv.second <= 0) continue;
            tickets[kv.first] = 1;
            break;
        }
    }
    return tickets;
}

// North Bridge is one British company, not the column on the road. That company
// does not outnumber the provincials who actually reached the crossing.
void limit_bridge_company(std::map<std::string, int>& british, int provincial_units) {
    auto it = british.find("line_infantry");
    if (it == british.end()) return;
    int company = std::min(std::max(0, it->second), 6);
    if (provincial_units > 0 && company > provincial_units) company = provincial_units;
    it->second = company;
}

}  // namespace

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
    b.objective = "green";
    b.scenario_preset = scenario_preset_for_province(province);
    b.player_faction = state.player_faction;
    b.attacker_faction = ait->second.faction;
    b.defender_faction = dit->second.faction;
    b.seed = detail::mix64(state.seed ^ detail::mix64(static_cast<uint64_t>(state.now.hour_index)) ^
                           detail::fnv1a(province));
    for (const auto& tag : pit->second.terrain) b.terrain_tags.push_back(tag);
    // The clock pauses on contact, so the attacker's previous province is the edge just marched.
    if (!ait->second.previous_province.empty()) {
        for (const auto& ek : state.edges) {
            const Edge& e = ek.second;
            const bool connects = (e.a == ait->second.previous_province && e.b == province) ||
                                  (e.b == ait->second.previous_province && e.a == province);
            if (!connects) continue;
            if (e.type == "neck") b.terrain_tags.push_back("chokepoint");
            if (e.type == "boat") b.terrain_tags.push_back("disordered");
        }
    }
    if (b.scenario_preset == "concord_bridge") b.terrain_tags.push_back("bridge_crossing");
    if (b.scenario_preset == "battle_road") b.terrain_tags.push_back("return_march");
    std::sort(b.terrain_tags.begin(), b.terrain_tags.end());
    b.terrain_tags.erase(std::unique(b.terrain_tags.begin(), b.terrain_tags.end()), b.terrain_tags.end());
    for (const Army* a : {&ait->second, &dit->second}) {
        SideSetup s;
        s.faction = a->faction;
        s.army_id = a->id;
        s.tickets = gate_tickets_by_stores(state, *a);
        s.morale = a->morale;
        s.readiness = a->readiness;
        s.retreat_province = choose_retreat(state, province, a->faction);
        b.sides[a->faction] = s;
    }
    if (b.scenario_preset == "concord_bridge") {
        const int prov = b.sides.count(kContinental) ? ticket_total(b.sides.at(kContinental).tickets) : 0;
        if (b.sides.count(kBritish)) limit_bridge_company(b.sides.at(kBritish).tickets, prov);
    }
    return b;
}

CampaignState apply_battle_result(const CampaignState& in, const BattleResult& r) {
    const PendingBattle pb = in.pending_battle;
    // A result that does not match the pending battle is not a command: leave state, log and hash alone.
    if (!pb.active || pb.province != r.province_id) return in;
    if (r.winner != kBritish && r.winner != kContinental) return in;
    CampaignState s = in;
    ensure_logistics_defaults(s);
    const std::string loser = detail::other_faction(r.winner);

    // The force actually fielded is the stores-gated ticket pool from before the battle,
    // not the paper roster: units stood down for want of powder/arms expend nothing.
    std::map<std::string, int> fielded_by_army;
    std::map<std::string, std::map<std::string, int>> gated_by_army;
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = in.armies.find(aid);
        if (it == in.armies.end()) continue;
        gated_by_army[aid] = gate_tickets_by_stores(in, it->second);
    }
    if (scenario_preset_for_province(r.province_id) == "concord_bridge") {
        int prov = 0;
        std::string brit_id;
        for (const auto& gk : gated_by_army) {
            auto it = in.armies.find(gk.first);
            if (it == in.armies.end()) continue;
            if (it->second.faction == kContinental) prov += ticket_total(gk.second);
            if (it->second.faction == kBritish) brit_id = gk.first;
        }
        if (!brit_id.empty()) limit_bridge_company(gated_by_army.at(brit_id), prov);
    }
    for (const auto& gk : gated_by_army) fielded_by_army[gk.first] = ticket_total(gk.second);

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

    // Expend powder/arms for units that fought; recover a few arms on a costly win.
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = s.armies.find(aid);
        if (it == s.armies.end()) continue;
        auto loss_it = r.losses.find(it->second.faction);
        int lost = 0;
        if (loss_it != r.losses.end())
            for (const auto& ck : loss_it->second) lost += std::max(0, ck.second);
        const int engaged = fielded_by_army.count(aid) ? fielded_by_army.at(aid) : 0;
        FactionStores& st = s.stores[it->second.faction];
        const int powder_use = engaged > 0 ? std::min(st.powder, engaged / 2) : 0;
        const int arms_lost = std::min(st.arms, lost);
        st.powder = std::max(0, st.powder - powder_use);
        st.arms = std::max(0, st.arms - arms_lost);
        it->second.fatigue = detail::clamp_int(it->second.fatigue + 15 + lost, 0, 100);
        it->second.readiness = detail::clamp_int(it->second.readiness - 10 - lost, 0, 100);
    }
    if (r.winner == s.player_faction) {
        // Recover a few serviceable arms from the field (not allegiance).
        auto& st = s.stores[s.player_faction];
        st.arms += 1;
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
        // Only the two armies whose tickets were included fought this battle.
        if (kv.first != pb.attacker_army && kv.first != pb.defender_army) continue;
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
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = s.armies.find(aid);
        if (it == s.armies.end()) continue;
        it->second.path.clear();
        it->second.hours_into_edge = 0;
    }
    for (auto it = s.armies.begin(); it != s.armies.end();) {
        if ((it->first == pb.attacker_army || it->first == pb.defender_army) &&
            detail::total_units(it->second) <= 0) {
            detail::add_log(s, "army destroyed: " + it->first);
            it = s.armies.erase(it);
        } else {
            ++it;
        }
    }

    // Ownership: the winner takes the ground when they held the objective.
    // Escalating Parker's company (hold_stores) adds a second way to lose it: if his
    // army is beaten, the town is forfeit even when the winner never held the post.
    // Protected provinces (Boston) stay outside that risk.
    bool stores_forfeit = false;
    const DetachedCommander& cmd = s.progress.commander;
    if (cmd.objective == "hold_stores" && r.winner != s.player_faction && r.objective_holder != r.winner &&
        (pb.attacker_army == cmd.army_id || pb.defender_army == cmd.army_id)) {
        bool protected_ground = false;
        for (const auto& fk : s.factions)
            if (detail::contains(fk.second.protected_provinces, r.province_id)) protected_ground = true;
        stores_forfeit = !protected_ground;
    }
    if (r.objective_holder == r.winner || stores_forfeit) {
        s.provinces.at(r.province_id).owner = r.winner;
        if (stores_forfeit)
            detail::add_log(s, "stores exposed: " + r.province_id + " forfeited when the field was lost");
    }

    s.memory.push_back(
        {s.now.hour_index, r.winner, "Battle of " + s.provinces.at(r.province_id).name + " won by " + r.winner});
    s.pending_battle = PendingBattle{};
    detail::add_log(s, "battle at " + r.province_id + " won by " + r.winner);

    ++s.progress.battles_fought;
    if (r.province_id == "lexington" || s.progress.battles_fought == 1) {
        s.progress.lexington_fought = true;
        s.progress.aftermath_pending = true;
    }
    return s;
}

}  // namespace pa250
