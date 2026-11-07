#include "internal.hpp"

namespace pa250 {

std::vector<std::string> validate_order(const CampaignState& state, const Order& order) {
    using detail::err;
    std::vector<std::string> errors;
    auto ai = state.armies.find(order.army_id);
    if (ai == state.armies.end()) return {err("unknown_army", order.army_id)};
    auto pi = state.provinces.find(order.to_province);
    if (pi == state.provinces.end()) return {err("unknown_province", order.to_province)};
    const Army& army = ai->second;
    if (order.to_province == army.province) return errors;  // hold
    const Province& here = state.provinces.at(army.province);
    if (!detail::contains(here.adjacent, order.to_province))
        return {err("not_adjacent", army.province + " -> " + order.to_province)};
    if (pi->second.owner != army.faction && detail::has_ceasefire(state) &&
        !detail::has_safe_passage(state, army.id, order.to_province))
        return {err("ceasefire_blocks_entry", order.to_province)};
    return errors;
}

namespace {

Order ai_order(const CampaignState& s, const Army& army) {
    Order hold{army.id, army.province};
    if (s.ai_objective.empty()) return hold;
    auto path = detail::shortest_path(s, army.province, s.ai_objective);
    if (path.size() < 2) return hold;
    Order step{army.id, path[1]};
    return validate_order(s, step).empty() ? step : hold;
}

}  // namespace

CampaignState resolve_turn(const CampaignState& in, const Orders& orders) {
    CampaignState s = in;
    if (s.pending_battle.active) {
        detail::add_log(s, "turn blocked: battle pending at " + s.pending_battle.province);
        return s;
    }

    // 1. Collect legal orders (against start-of-turn state), then fill British AI.
    std::map<std::string, std::string> dest;  // army id -> destination
    for (const auto& o : orders.orders) {
        auto errs = validate_order(in, o);
        if (!errs.empty()) {
            detail::add_log(s, "order rejected " + o.army_id + "->" + o.to_province + " (" + errs[0] + ")");
            continue;
        }
        if (dest.count(o.army_id)) {
            detail::add_log(s, "duplicate order ignored for " + o.army_id);
            continue;
        }
        dest[o.army_id] = o.to_province;
    }
    for (const auto& kv : in.armies) {
        const Army& a = kv.second;
        if (a.faction != in.player_faction && !dest.count(a.id)) dest[a.id] = ai_order(in, a).to_province;
    }

    // 1b. Hostile armies swapping provinces along one road meet on it instead of passing through
    // each other. The army marching into enemy-owned ground attacks (tie: lower army id); the
    // defender holds its province, so step 3 records the battle there.
    if (!detail::has_ceasefire(in)) {
        std::set<std::string> intercepted;
        for (const auto& ak : in.armies) {
            const Army& a = ak.second;
            auto da = dest.find(a.id);
            if (da == dest.end() || da->second == a.province || intercepted.count(a.id)) continue;
            for (const auto& bk : in.armies) {
                const Army& b = bk.second;
                if (b.id <= a.id || b.faction == a.faction || intercepted.count(b.id)) continue;
                auto db = dest.find(b.id);
                if (db == dest.end() || db->second != a.province || da->second != b.province) continue;
                if (detail::has_safe_passage(in, a.id, b.province) || detail::has_safe_passage(in, b.id, a.province))
                    continue;
                const bool a_hostile = in.provinces.at(b.province).owner != a.faction;
                const bool b_hostile = in.provinces.at(a.province).owner != b.faction;
                const Army& defender = (b_hostile && !a_hostile) ? a : b;
                dest[defender.id] = defender.province;
                intercepted.insert(a.id);
                intercepted.insert(b.id);
                detail::add_log(s, "armies meet on the road " + a.province + " - " + b.province);
                break;
            }
        }
    }

    // 2. Move simultaneously.
    std::set<std::string> moved;
    for (const auto& kv : dest) {
        Army& a = s.armies.at(kv.first);
        if (kv.second != a.province) {
            a.previous_province = a.province;
            a.province = kv.second;
            moved.insert(a.id);
        }
    }

    // 3. Detect meetings. The resolver never fights; it only records a pending battle.
    if (!detail::has_ceasefire(s)) {
        for (const auto& pk : s.provinces) {
            std::vector<const Army*> here;
            for (const auto& ak : s.armies)
                if (ak.second.province == pk.first) here.push_back(&ak.second);
            const Army *brit = nullptr, *cont = nullptr;
            for (const Army* a : here) {
                if (a->faction == kBritish && !brit) brit = a;
                if (a->faction == kContinental && !cont) cont = a;
            }
            if (!brit || !cont) continue;
            if (detail::has_safe_passage(s, brit->id, pk.first) || detail::has_safe_passage(s, cont->id, pk.first))
                continue;
            const Army* attacker = brit;
            if (moved.count(cont->id) && !moved.count(brit->id)) attacker = cont;
            const Army* defender = attacker == brit ? cont : brit;
            s.pending_battle = {true, pk.first, attacker->id, defender->id};
            detail::add_log(s, "armies meet at " + pk.first + ": battle pending");
            break;
        }
    }

    // 4. Supplies from owned provinces.
    for (const auto& pk : s.provinces) s.supplies[pk.second.owner] += pk.second.supply_value;

    // 5. Treaty tick, then advance the turn.
    for (auto& t : s.treaties) t.turns_remaining -= 1;
    s.treaties.erase(std::remove_if(s.treaties.begin(), s.treaties.end(),
                                    [](const Treaty& t) { return t.turns_remaining <= 0; }),
                     s.treaties.end());
    s.turn += 1;
    return s;
}

}  // namespace pa250
