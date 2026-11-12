#include "internal.hpp"

namespace pa250 {
namespace {

int ticket_total(const std::map<std::string, int>& tickets) {
    int n = 0;
    for (const auto& kv : tickets) n += std::max(0, kv.second);
    return n;
}

std::map<std::string, int> gate_tickets_by_stores(const CampaignState& state, const Army& army,
                                                  const std::string& battle_province,
                                                  int extra_cartridges = 0) {
    std::map<std::string, int> tickets = army.units;  // fit only
    const ProvinceStores& st = province_ledger(state, battle_province);

    int total = 0;
    for (const auto& kv : tickets) total += std::max(0, kv.second);
    if (total <= 0) return tickets;

    // Muskets: need roughly one serviceable musket per unit fielded.
    if (st.muskets < total && total > 0) {
        const double scale = static_cast<double>(std::max(0, st.muskets)) / static_cast<double>(total);
        for (auto& kv : tickets) {
            if (kv.first == "militia" || kv.second > 0)
                kv.second = std::max(0, static_cast<int>(kv.second * scale + 0.5));
        }
    }

    // Cartridges (not powder) cap how many militia can be fielded with ammunition.
    // Workshop development adds virtual cartridge budget for this fight only.
    auto mit = tickets.find("militia");
    if (mit != tickets.end() && mit->second > 0) {
        const int cart_cap = std::max(0, st.cartridges + std::max(0, extra_cartridges));
        if (cart_cap < mit->second) mit->second = cart_cap;
    }

    // Readiness / fatigue soft-cap (aggregate, not personal inventory).
    if (army.readiness < 50 || army.fatigue > 60) {
        for (auto& kv : tickets) {
            if (kv.first == "militia") kv.second = (kv.second * 3) / 4;
        }
    }

    // Temporary personnel commitments hold paper units out of fielding without touching
    // Army.units, equipment, readiness, or morale.
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

// Shares a clamped spend total across factions in proportion to their raw claims, so the
// after-battle report can never claim more cartridges or powder than the magazine held.
std::map<std::string, int> allocate_spend(const std::map<std::string, int>& raw, int total) {
    std::map<std::string, int> out;
    long raw_sum = 0;
    for (const auto& kv : raw) raw_sum += std::max(0, kv.second);
    for (const auto& kv : raw) {
        out[kv.first] = (total > 0 && raw_sum > 0)
                            ? static_cast<int>(static_cast<long>(std::max(0, kv.second)) * total / raw_sum)
                            : 0;
    }
    int assigned = 0;
    for (const auto& kv : out) assigned += kv.second;
    // Hand the rounding remainder back, one at a time in faction order, never past a raw claim.
    while (assigned < total) {
        bool progressed = false;
        for (auto& kv : out) {
            if (assigned >= total) break;
            if (kv.second < std::max(0, raw.at(kv.first))) {
                ++kv.second;
                ++assigned;
                progressed = true;
            }
        }
        if (!progressed) break;
    }
    return out;
}

// North Bridge is one British detachment, not the column on the road. It does not outnumber the
// provincials who actually reached the crossing. The cap is shared across the column's classes in
// proportion (largest remainder, class order breaks ties), so a mixed column sends a mixed company.
void limit_bridge_company(std::map<std::string, int>& british, int provincial_units) {
    int total = 0;
    for (const auto& kv : british) total += std::max(0, kv.second);
    int company = std::min(total, 6);
    if (provincial_units > 0 && company > provincial_units) company = provincial_units;
    if (total <= company) return;
    std::map<std::string, int> out;
    std::vector<std::pair<int, std::string>> rest;  // (-remainder, class)
    int given = 0;
    for (const auto& kv : british) {
        const int n = std::max(0, kv.second);
        out[kv.first] = n * company / total;
        given += out[kv.first];
        rest.push_back({-(n * company % total), kv.first});
    }
    std::sort(rest.begin(), rest.end());
    for (const auto& r : rest) {
        if (given >= company) break;
        if (r.first == 0) break;
        ++out[r.second];
        ++given;
    }
    british = out;
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
    b.farm_level = pit->second.farm_level;
    b.workshop_level = pit->second.workshop_level;
    b.depot_level = pit->second.depot_level;
    b.fort_level = pit->second.fort_level;
    b.church = pit->second.church;
    // Muster helps only a defender fighting on its own ground (UE reads this flag for the side).
    b.militia_muster = pit->second.militia_muster && b.defender_faction == pit->second.owner;
    const int workshop_bonus =
        std::max(0, pit->second.workshop_level) * workshop_cartridges_per_level();
    // Physical magazine only. The workshop bonus raises the owner's militia tickets;
    // it is not extra cartridges the fight can spend past what the province holds.
    const ProvinceStores& magazine = province_ledger(state, province);
    const int stored_cartridges = magazine.cartridges;
    b.province_cartridges = stored_cartridges;
    b.province_powder = magazine.powder;
    b.force_notes = "sourced approximation — not a precise muster roll";
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
    const int farm_ready = std::max(0, pit->second.farm_level) * farm_readiness_per_level();
    const int depot_ready = std::max(0, pit->second.depot_level) * depot_readiness_per_level();
    const int logistics_ready = farm_ready + depot_ready;
    const float reinforce =
        1.f - 0.05f * static_cast<float>(std::min(5, pit->second.farm_level + pit->second.depot_level));
    for (const Army* a : {&ait->second, &dit->second}) {
        SideSetup s;
        s.faction = a->faction;
        s.army_id = a->id;
        const bool owns_ground = a->faction == pit->second.owner;
        const int extra_cart = owns_ground ? workshop_bonus : 0;
        s.tickets = gate_tickets_by_stores(state, *a, province, extra_cart);
        s.morale = a->morale;
        s.readiness = a->readiness;
        // Fortifications help whoever holds the province defend it.
        if (a->faction == pit->second.owner && pit->second.fort_level > 0) {
            s.morale = detail::clamp_int(s.morale + 3 * pit->second.fort_level, 0, 100);
            s.readiness = detail::clamp_int(s.readiness + 2 * pit->second.fort_level, 0, 100);
        }
        // Meeting House: defender on its own ground only (matches the catalog text and the note).
        if (a->faction == pit->second.owner && a->faction == b.defender_faction) {
            const int church_morale = building_battle_morale(pit->second);
            if (church_morale != 0)
                s.morale = detail::clamp_int(s.morale + church_morale, 0, 100);
        }
        // Farms/depots stock the magazine and steady the force that fights here.
        if (a->faction == pit->second.owner && logistics_ready > 0) {
            s.readiness = detail::clamp_int(s.readiness + logistics_ready, 0, 100);
            s.reinforcement_scale = std::max(0.75f, reinforce);
        }
        s.cartridge_budget = stored_cartridges + extra_cart;
        s.retreat_province = choose_retreat(state, province, a->faction);
        b.sides[a->faction] = s;
    }
    if (b.scenario_preset == "concord_bridge") {
        const int prov = b.sides.count(kContinental) ? ticket_total(b.sides.at(kContinental).tickets) : 0;
        if (b.sides.count(kBritish)) limit_bridge_company(b.sides.at(kBritish).tickets, prov);
    }
    // Fort: defender who owns the ground starts holding the primary post + cover note.
    if (pit->second.fort_level > 0 && b.defender_faction == pit->second.owner) {
        const std::string post = "green";
        b.initial_post_holders[post] = b.defender_faction;
        b.modifier_notes.push_back("Fort L" + std::to_string(pit->second.fort_level) + " " +
                                   pit->second.name + ": defender holds " + post +
                                   " at start; +3 morale/+2 readiness per level; extra cover");
    }
    if (pit->second.workshop_level > 0) {
        b.modifier_notes.push_back(
            "Workshop L" + std::to_string(pit->second.workshop_level) + " " + pit->second.name +
            ": +" + std::to_string(workshop_bonus) + " cartridge budget (militia tickets)");
    }
    if (pit->second.farm_level > 0 || pit->second.depot_level > 0) {
        b.modifier_notes.push_back(
            "Farm L" + std::to_string(pit->second.farm_level) + "/Depot L" +
            std::to_string(pit->second.depot_level) + " " + pit->second.name +
            ": owner readiness +" + std::to_string(logistics_ready) + "; faster reinforcement");
    }
    if (b.defender_faction == pit->second.owner && building_battle_morale(pit->second) != 0) {
        b.modifier_notes.push_back("Meeting House " + pit->second.name + ": defender morale +" +
                                   std::to_string(building_battle_morale(pit->second)));
    }
    if (b.militia_muster) {
        b.modifier_notes.push_back("Militia Muster " + pit->second.name +
                                   ": extra defender militia companies");
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
    const int support_before = s.local_support;
    const int gold_before = s.player_gold;
    const std::string owner_before = s.provinces.at(r.province_id).owner;

    std::map<std::string, int> fielded_by_army;
    std::map<std::string, std::map<std::string, int>> gated_by_army;
    int workshop_bonus_apply = 0;
    std::string province_owner;
    {
        auto pit = in.provinces.find(r.province_id);
        if (pit != in.provinces.end()) {
            workshop_bonus_apply =
                std::max(0, pit->second.workshop_level) * workshop_cartridges_per_level();
            province_owner = pit->second.owner;
        }
    }
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = in.armies.find(aid);
        if (it == in.armies.end()) continue;
        const int extra = it->second.faction == province_owner ? workshop_bonus_apply : 0;
        gated_by_army[aid] = gate_tickets_by_stores(in, it->second, r.province_id, extra);
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

    // Killed: reduce fit. Wounded: move fit → wounded.
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
    for (const auto& fk : r.wounded) {
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = s.armies.find(aid);
            if (it == s.armies.end() || it->second.faction != fk.first) continue;
            for (const auto& ck : fk.second) {
                if (ck.second <= 0) continue;
                auto& fit = it->second.units[ck.first];
                const int move = std::min(fit, ck.second);
                fit -= move;
                it->second.wounded[ck.first] += move;
            }
        }
    }

    // Expend cartridges (and optional powder) from the battle province. No free refill.
    ProvinceStores& pst = province_ledger(s, r.province_id);
    const int cart_before = pst.cartridges;
    const int powder_before = pst.powder;
    int total_cart_spent = 0;
    int total_powder_spent = 0;
    for (const auto& kv : r.cartridges_spent) total_cart_spent += std::max(0, kv.second);
    for (const auto& kv : r.powder_spent) total_powder_spent += std::max(0, kv.second);

    // Prefer live shot counts from UE when present; else cartridges_spent; else fielded×2.
    int total_shots = 0;
    for (const auto& kv : r.shots_fired) total_shots += std::max(0, kv.second);
    if (total_shots > 0) {
        total_cart_spent = total_shots;
    } else if (total_cart_spent <= 0) {
        for (const auto& fk : fielded_by_army) {
            auto it = in.armies.find(fk.first);
            if (it == in.armies.end()) continue;
            const int spend = std::max(0, fk.second * 2);
            total_cart_spent += spend;
        }
    }
    total_cart_spent = std::min(total_cart_spent, cart_before);
    total_powder_spent = std::min(total_powder_spent, powder_before);
    pst.cartridges = cart_before - total_cart_spent;
    pst.powder = powder_before - total_powder_spent;

    // Musket loss from killed (equipment broken/captured); no free recovery.
    int muskets_lost = 0;
    for (const auto& fk : r.losses)
        for (const auto& ck : fk.second) muskets_lost += std::max(0, ck.second);
    pst.muskets = std::max(0, pst.muskets - muskets_lost);

    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = s.armies.find(aid);
        if (it == s.armies.end()) continue;
        auto loss_it = r.losses.find(it->second.faction);
        int lost = 0;
        if (loss_it != r.losses.end())
            for (const auto& ck : loss_it->second) lost += std::max(0, ck.second);
        it->second.fatigue = detail::clamp_int(it->second.fatigue + 15 + lost, 0, 100);
        it->second.readiness = detail::clamp_int(it->second.readiness - 10 - lost, 0, 100);
    }

    // Morale, personal-kill bump, retreat / shatter / destruction.
    std::string retreat = r.retreat_province;
    {
        auto pit = s.provinces.find(r.province_id);
        bool ok = pit != s.provinces.end() && detail::contains(pit->second.adjacent, retreat);
        if (ok)
            for (const auto& kv : s.armies)
                if (kv.second.province == retreat && kv.second.faction == r.winner) ok = false;
        if (!ok) retreat = choose_retreat(in, r.province_id, loser);
    }
    const int personal_morale = std::min(5, std::max(0, r.personal_kills));
    std::map<std::string, int> morale_delta;
    std::string loser_fate = "none";
    std::string retreat_to;
    int flee_fit = 0;     // remaining fit on loser when retreating (light drop)
    int shatter_fit = 0;  // remaining fit when shattered (heavy salvage)
    for (auto& kv : s.armies) {
        Army& a = kv.second;
        if (kv.first != pb.attacker_army && kv.first != pb.defender_army) continue;
        if (a.province != r.province_id) continue;
        if (a.faction == r.winner) {
            int bump = 5;
            if (a.faction == s.player_faction) bump += personal_morale;
            a.morale = detail::clamp_int(a.morale + bump, 0, 100);
        } else {
            a.morale = detail::clamp_int(a.morale - 20, 0, 100);
            if (a.faction == s.player_faction && personal_morale > 0)
                a.morale = detail::clamp_int(a.morale + personal_morale, 0, 100);
            const int fit = detail::total_units(a);
            if (fit <= 0) {
                loser_fate = "destroyed";
            } else if (a.morale <= 10) {
                loser_fate = "shattered";
                shatter_fit = fit;
            } else if (!retreat.empty()) {
                loser_fate = "retreated";
                flee_fit = fit;
                retreat_to = retreat;
                a.previous_province = a.province;
                a.province = retreat;
            } else {
                loser_fate = "shattered";
                shatter_fit = fit;
            }
        }
        auto before_it = in.armies.find(kv.first);
        const int before = before_it != in.armies.end() ? before_it->second.morale : a.morale;
        morale_delta[kv.first] = a.morale - before;
    }
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = s.armies.find(aid);
        if (it == s.armies.end()) continue;
        it->second.path.clear();
        it->second.hours_into_edge = 0;
    }

    // Salvage from enemy dead + flight/shatter (credited to battle province magazine).
    int loser_killed = 0;
    {
        auto lit = r.losses.find(loser);
        if (lit != r.losses.end())
            for (const auto& ck : lit->second) loser_killed += std::max(0, ck.second);
    }
    int scav_cart = loser_killed * 1;
    int scav_powder = loser_killed / 4;
    int scav_muskets = loser_killed / 2;
    if (loser_fate == "retreated") {
        scav_cart += flee_fit / 10;
    } else if (loser_fate == "shattered") {
        scav_cart += shatter_fit * 2;
        scav_powder += shatter_fit / 2;
        scav_muskets += shatter_fit / 2;
    }
    scav_muskets = std::min(scav_muskets, muskets_lost);
    scav_cart = std::max(0, scav_cart);
    scav_powder = std::max(0, scav_powder);
    scav_muskets = std::max(0, scav_muskets);
    pst.cartridges += scav_cart;
    pst.powder += scav_powder;
    pst.muskets += scav_muskets;

    for (auto it = s.armies.begin(); it != s.armies.end();) {
        if ((it->first == pb.attacker_army || it->first == pb.defender_army) &&
            (detail::total_units(it->second) <= 0 ||
             (it->second.faction == loser &&
              (loser_fate == "shattered" || loser_fate == "destroyed")))) {
            if (loser_fate == "shattered" && it->second.faction == loser)
                detail::add_log(s, "army shattered: " + it->first);
            else
                detail::add_log(s, "army destroyed: " + it->first);
            if (it->second.faction == loser && loser_fate == "none") loser_fate = "destroyed";
            it = s.armies.erase(it);
        } else {
            ++it;
        }
    }
    if (loser_fate == "none") {
        // Winner wiped the field with no surviving loser army entry.
        bool loser_alive = false;
        for (const std::string& aid : {pb.attacker_army, pb.defender_army})
            if (s.armies.count(aid) && s.armies.at(aid).faction == loser) loser_alive = true;
        if (!loser_alive) loser_fate = "destroyed";
    }

    // Ownership: the winner takes the ground when they held the objective.
    bool stores_forfeit = false;
    const DetachedCommander& cmd = s.progress.commander;
    if (cmd.objective == "hold_stores" && r.winner != s.player_faction && r.objective_holder != r.winner &&
        (pb.attacker_army == cmd.army_id || pb.defender_army == cmd.army_id)) {
        bool protected_ground = false;
        for (const auto& fk : s.factions)
            if (detail::contains(fk.second.protected_provinces, r.province_id)) protected_ground = true;
        stores_forfeit = !protected_ground;
    }
    std::string ownership_reason = "unchanged";
    if (r.objective_holder == r.winner || stores_forfeit) {
        s.provinces.at(r.province_id).owner = r.winner;
        ownership_reason = stores_forfeit ? "stores_forfeit" : "held_objective";
        if (stores_forfeit)
            detail::add_log(s, "stores exposed: " + r.province_id + " forfeited when the field was lost");
    }
    // Capture itself never changes local_support; defender win reward may raise it below.
    if (s.local_support != support_before) s.local_support = support_before;

    // Player defender who wins the field earns a visible Support reward.
    int defender_reward = 0;
    {
        auto dit = in.armies.find(pb.defender_army);
        if (dit != in.armies.end() && dit->second.faction == r.winner &&
            dit->second.faction == s.player_faction) {
            defender_reward = 5;
            s.local_support = detail::clamp_int(s.local_support + defender_reward, 0, 100);
            ResourceChange ch;
            ch.reason = "Defended " + s.provinces.at(r.province_id).name + ": +5 Support";
            ch.local_support = defender_reward;
            ch.province_id = r.province_id;
            detail::record_resource_change(s, ch);
        }
    }

    if (total_cart_spent > 0 || total_powder_spent > 0 || muskets_lost > 0) {
        ResourceChange ch;
        ch.reason = "Battle ammo at " + r.province_id + ": -" + std::to_string(total_cart_spent) +
                    " cartridges, -" + std::to_string(total_powder_spent) + " powder, -" +
                    std::to_string(muskets_lost) + " muskets";
        ch.cartridges = -total_cart_spent;
        ch.powder = -total_powder_spent;
        ch.muskets = -muskets_lost;
        ch.province_id = r.province_id;
        detail::record_resource_change(s, ch);
    }
    if (scav_cart > 0 || scav_powder > 0 || scav_muskets > 0) {
        ResourceChange ch;
        ch.reason = "Scavenged the field at " + r.province_id + ": +" + std::to_string(scav_cart) +
                    " cartridges, +" + std::to_string(scav_powder) + " powder, +" +
                    std::to_string(scav_muskets) + " muskets";
        ch.cartridges = scav_cart;
        ch.powder = scav_powder;
        ch.muskets = scav_muskets;
        ch.province_id = r.province_id;
        detail::record_resource_change(s, ch);
    }

    std::string ownership_line;
    if (ownership_reason == "held_objective") {
        ownership_line = "Held the posts: " + s.provinces.at(r.province_id).name + " now " + r.winner;
        detail::add_log(s, ownership_line);
    } else if (ownership_reason == "stores_forfeit") {
        ownership_line = "Stores exposed after field lost: " + s.provinces.at(r.province_id).name +
                         " now " + r.winner;
    } else {
        ownership_line = "Ownership unchanged at " + s.provinces.at(r.province_id).name;
    }

    BattleReport report;
    report.province_id = r.province_id;
    report.winner = r.winner;
    report.province_owner_before = owner_before;
    report.province_owner_after = s.provinces.at(r.province_id).owner;
    report.local_support_before = support_before;
    report.local_support_after = s.local_support;
    report.gold_before = gold_before;
    report.gold_after = s.player_gold;
    report.objective_holder = r.objective_holder;
    report.objectives_held = r.objectives_held;
    report.losses = r.losses;
    report.wounded = r.wounded;
    report.ownership_reason = ownership_reason;
    report.defender_reward_support = defender_reward;
    report.personal_kills = std::max(0, r.personal_kills);
    report.retreat_to = retreat_to;
    report.loser_fate = loser_fate;
    report.scavenged_cartridges = scav_cart;
    report.scavenged_powder = scav_powder;
    report.scavenged_muskets = scav_muskets;
    if (defender_reward > 0)
        report.change_reasons.push_back("Defended " + s.provinces.at(r.province_id).name + ": +5 Support");
    report.change_reasons.push_back(ownership_line);
    if (total_cart_spent || total_powder_spent || muskets_lost) {
        report.change_reasons.push_back("Battle ammo at " + r.province_id + ": -" +
                                        std::to_string(total_cart_spent) + " cart, -" +
                                        std::to_string(total_powder_spent) + " powder, -" +
                                        std::to_string(muskets_lost) + " muskets");
    }
    if (scav_cart || scav_powder || scav_muskets) {
        report.change_reasons.push_back("Scavenged the field: +" + std::to_string(scav_cart) +
                                        " cart, +" + std::to_string(scav_powder) + " powder, +" +
                                        std::to_string(scav_muskets) + " muskets");
    }
    if (personal_morale > 0)
        report.change_reasons.push_back("Your marksmanship: +" + std::to_string(personal_morale) +
                                        " Morale (" + std::to_string(report.personal_kills) + " kills)");
    if (loser_fate == "retreated" && !retreat_to.empty()) {
        const std::string dest =
            s.provinces.count(retreat_to) ? s.provinces.at(retreat_to).name : retreat_to;
        report.change_reasons.push_back("Loser retreated to " + dest);
    } else if (loser_fate == "shattered") {
        report.change_reasons.push_back("Loser army shattered and dispersed");
    } else if (loser_fate == "destroyed") {
        report.change_reasons.push_back("Loser army destroyed");
    }
    report.change_reasons.push_back("Gold unchanged by battle (" + std::to_string(gold_before) + ")");
    // Split the actual (stock-clamped) spend across sides for the report: raw claims come
    // from shots, then cartridges_spent, else the fielded share, scaled to what was deducted.
    int fielded_all = 0;
    for (const auto& fk : fielded_by_army) fielded_all += fk.second;
    std::map<std::string, int> raw_cart, raw_powder;
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = in.armies.find(aid);
        if (it == in.armies.end()) continue;
        const int engaged = fielded_by_army.count(aid) ? fielded_by_army.at(aid) : 0;
        int cart = 0;
        if (r.shots_fired.count(it->second.faction))
            cart = std::max(0, r.shots_fired.at(it->second.faction));
        else if (r.cartridges_spent.count(it->second.faction))
            cart = std::max(0, r.cartridges_spent.at(it->second.faction));
        else if (fielded_all > 0)
            cart = total_cart_spent * engaged / fielded_all;
        raw_cart[it->second.faction] = cart;
        raw_powder[it->second.faction] =
            r.powder_spent.count(it->second.faction) ? std::max(0, r.powder_spent.at(it->second.faction)) : 0;
    }
    report.cartridges_spent = allocate_spend(raw_cart, total_cart_spent);
    report.powder_spent = allocate_spend(raw_powder, total_powder_spent);
    report.morale_delta = morale_delta;
    s.progress.last_battle_report = report;

    s.memory.push_back(
        {s.now.hour_index, r.winner, "Battle of " + s.provinces.at(r.province_id).name + " won by " + r.winner});
    s.pending_battle = PendingBattle{};
    detail::add_log(s, "battle at " + r.province_id + " won by " + r.winner);

    ++s.progress.battles_fought;
    // Opening-war gate: first battle (Concord in this slice) unlocks aftermath correspondence.
    if (r.province_id == "concord" || r.province_id == "lexington" || s.progress.battles_fought == 1) {
        s.progress.lexington_fought = true;
        s.progress.aftermath_pending = true;
    }
    return s;
}

}  // namespace pa250
