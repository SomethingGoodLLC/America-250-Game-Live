#include "internal.hpp"
#include "nlohmann/json.hpp"

#include <cmath>

namespace pa250 {
namespace {

std::string other_end(const Edge& e, const std::string& from) { return e.a == from ? e.b : e.a; }

bool hostile(const Army& a, const Army& b) { return a.faction != b.faction; }

bool same_province_hostile(const CampaignState& s, const std::string& province, std::string& attacker,
                           std::string& defender) {
    const Army *brit = nullptr, *cont = nullptr;
    for (const auto& ak : s.armies) {
        if (ak.second.province != province) continue;
        if (ak.second.faction == kBritish && !brit) brit = &ak.second;
        if (ak.second.faction == kContinental && !cont) cont = &ak.second;
    }
    if (!brit || !cont) return false;
    if (detail::has_safe_passage(s, brit->id, province) || detail::has_safe_passage(s, cont->id, province))
        return false;
    attacker = brit->id;
    defender = cont->id;
    return true;
}

BattleSetup make_contact_setup(const CampaignState& s, const std::string& province,
                               const std::string& attacker_id, const std::string& defender_id,
                               const std::string& arrival_edge_id, int hours_since_boat) {
    BattleSetup setup = build_battle_setup(s, province);
    setup.attacker_faction = s.armies.at(attacker_id).faction;
    setup.defender_faction = s.armies.at(defender_id).faction;
    // Prefer pending battle armies.
    if (setup.sides.count(setup.attacker_faction)) setup.sides[setup.attacker_faction].army_id = attacker_id;
    if (setup.sides.count(setup.defender_faction)) setup.sides[setup.defender_faction].army_id = defender_id;

    auto pit = s.provinces.find(province);
    if (pit != s.provinces.end()) {
        for (const auto& t : pit->second.terrain) setup.terrain_tags.push_back(t);
    }
    if (!arrival_edge_id.empty()) {
        auto eit = s.edges.find(arrival_edge_id);
        if (eit != s.edges.end()) {
            if (eit->second.type == "neck") setup.terrain_tags.push_back("chokepoint");
            if (eit->second.type == "boat" && hours_since_boat <= 6) setup.terrain_tags.push_back("disordered");
        }
    }
    std::sort(setup.terrain_tags.begin(), setup.terrain_tags.end());
    setup.terrain_tags.erase(std::unique(setup.terrain_tags.begin(), setup.terrain_tags.end()),
                             setup.terrain_tags.end());
    return setup;
}

bool controls_harbor(const CampaignState& s, const std::string& faction) {
    auto fit = s.factions.find(faction);
    return fit != s.factions.end() && fit->second.controls_harbor;
}

// Walk edge ids from the army's current province. Empty dest skips the endpoint check.
bool edge_path_ok(const CampaignState& s, const Army& army, const std::vector<std::string>& edges,
                  const std::string& dest, std::string& error) {
    if (edges.empty()) {
        error = "empty_path";
        return false;
    }
    const bool harbor = controls_harbor(s, army.faction);
    std::string cur = army.province;
    for (const auto& id : edges) {
        auto eit = s.edges.find(id);
        if (eit == s.edges.end()) {
            error = "unknown_edge";
            return false;
        }
        const Edge& e = eit->second;
        if (e.a != cur && e.b != cur) {
            error = "path_broken";
            return false;
        }
        if ((e.type == "ferry" || e.type == "boat") && !harbor) {
            error = "requires_harbor_control";
            return false;
        }
        cur = e.a == cur ? e.b : e.a;
    }
    if (!dest.empty() && cur != dest) {
        error = "path_does_not_reach_dest";
        return false;
    }
    return true;
}

bool ceasefire_blocks_path(const CampaignState& s, const Army& army, const std::vector<std::string>& edges,
                           std::string& blocked_province) {
    if (!detail::has_ceasefire(s)) return false;
    std::string cur = army.province;
    for (const auto& id : edges) {
        const Edge& e = s.edges.at(id);
        cur = e.a == cur ? e.b : e.a;
        const Province& prov = s.provinces.at(cur);
        if (prov.owner != army.faction && !detail::has_safe_passage(s, army.id, cur)) {
            blocked_province = cur;
            return true;
        }
    }
    return false;
}

void apply_move_command(CampaignState& s, const CommandPayload& p) {
    auto ait = s.armies.find(p.army_id);
    if (ait == s.armies.end()) {
        detail::add_log(s, "move ignored: unknown army " + p.army_id);
        return;
    }
    Army& army = ait->second;
    std::vector<std::string> edges;
    std::string why;
    if (!p.edge_path.empty() && edge_path_ok(s, army, p.edge_path, p.dest_province, why)) {
        edges = p.edge_path;
    } else {
        const PathPlan plan = plan_path(s, p.army_id, p.dest_province);
        if (!plan.ok) {
            const std::string reason = !why.empty() ? why : plan.error;
            detail::add_log(s, "move rejected " + p.army_id + "->" + p.dest_province + " (" + reason + ")");
            return;
        }
        edges = plan.edges;
    }
    std::string blocked;
    if (ceasefire_blocks_path(s, army, edges, blocked)) {
        detail::add_log(s, "move rejected " + p.army_id + "->" + p.dest_province +
                               " (ceasefire_blocks_entry: " + blocked + ")");
        return;
    }
    army.path = std::move(edges);
    army.hours_into_edge = 0;
    detail::add_log(s, "army " + army.id + " ordered to " + p.dest_province + " via " +
                           std::to_string(army.path.size()) + " edges");
}

void apply_halt(CampaignState& s, const CommandPayload& p) {
    auto ait = s.armies.find(p.army_id);
    if (ait == s.armies.end()) return;
    ait->second.path.clear();
    ait->second.hours_into_edge = 0;
    detail::add_log(s, "army " + p.army_id + " halted");
}

void apply_one_command(CampaignState& s, const Command& c) {
    switch (c.payload.kind) {
        case CommandKind::MoveArmy:
            apply_move_command(s, c.payload);
            break;
        case CommandKind::HaltArmy:
            apply_halt(s, c.payload);
            break;
        case CommandKind::ApplyBattleResult: {
            try {
                BattleResult r = result_from_json(c.payload.result_json);
                s = apply_battle_result(s, r);
            } catch (const std::exception& e) {
                detail::add_log(s, std::string("battle result command failed: ") + e.what());
            }
            break;
        }
        case CommandKind::ApplyDiplomacy: {
            try {
                // result_json is a DiplomacyResult object
                nlohmann::json j = nlohmann::json::parse(c.payload.result_json);
                DiplomacyResult r;
                r.faction = j.at("faction").get<std::string>();
                r.accepted = j.value("accepted", false);
                auto errs = proposal_from_json(j.at("proposal").dump(), r.proposal);
                if (errs.empty()) s = apply_diplomacy(s, r);
                else
                    detail::add_log(s, "diplomacy command invalid");
            } catch (const std::exception& e) {
                detail::add_log(s, std::string("diplomacy command failed: ") + e.what());
            }
            break;
        }
    }
}

void emit_daily_ai(CampaignState& s) {
    YMDH y = to_ymdh(s.now);
    if (y.h != 0) return;
    if (s.ai_objective.empty()) return;
    for (auto& kv : s.armies) {
        Army& a = kv.second;
        if (a.faction == s.player_faction) continue;
        if (!a.path.empty()) continue;  // already marching
        PathPlan plan = plan_path(s, a.id, s.ai_objective);
        if (!plan.ok || plan.edges.empty()) continue;
        CommandPayload p;
        p.kind = CommandKind::MoveArmy;
        p.army_id = a.id;
        p.dest_province = s.ai_objective;
        p.edge_path = plan.edges;
        // Apply immediately this hour (AI decision at 00:00).
        apply_move_command(s, p);
    }
}

void monthly_supply(CampaignState& s) {
    YMDH y = to_ymdh(s.now);
    if (!(y.d == 1 && y.h == 0)) return;
    for (const auto& pk : s.provinces) {
        if (pk.second.owner.empty()) continue;
        s.supplies[pk.second.owner] += effective_supply_value(pk.second);
        // Farms feed the province magazine (development effect; 3 per farm level by default in tuning).
        if (pk.second.farm_level > 0) province_ledger(s, pk.first).provisions += pk.second.farm_level * 3;
    }
    // Treasury income: uncapped, grows with the provinces the player holds.
    int gold_gain = 0;
    for (const auto& pk : s.provinces) {
        if (pk.second.owner != s.player_faction) continue;
        const int add = town_levy_gold(pk.second);
        s.player_gold += add;
        gold_gain += add;
        if (pk.second.farm_level > 0) {
            ResourceChange ch;
            ch.reason = "Farm harvest, " + pk.second.name + ": +" +
                        std::to_string(pk.second.farm_level * 3) + " provisions";
            ch.provisions = pk.second.farm_level * 3;
            ch.province_id = pk.first;
            detail::record_resource_change(s, ch);
        }
    }
    if (gold_gain > 0) {
        ResourceChange ch;
        ch.reason = "Monthly supply accrual: +" + std::to_string(gold_gain) + " gold";
        ch.gold = gold_gain;
        detail::record_resource_change(s, ch);
    } else {
        detail::add_log(s, "monthly supply accrual");
    }
    buildings_monthly_tick(s);
}

void decay_grievance(CampaignState& s) {
    if (to_ymdh(s.now).h != 0) return;
    for (auto& fk : s.factions)
        if (fk.second.grievance > 0) fk.second.grievance = std::max(0, fk.second.grievance - 2);
}

void tick_treaties(CampaignState& s) {
    for (auto& t : s.treaties) t.hours_remaining -= 1;
    s.treaties.erase(std::remove_if(s.treaties.begin(), s.treaties.end(),
                                    [](const Treaty& t) { return t.hours_remaining <= 0; }),
                     s.treaties.end());
}

struct MoveStep {
    std::string army_id;
    std::string from;
    std::string to;
    std::string edge_id;
    bool completed_edge = false;
};

std::vector<MoveStep> advance_movement(CampaignState& s) {
    std::vector<MoveStep> steps;
    // March budget: armies only advance during the first march_hours_per_day hours of each day.
    YMDH y = to_ymdh(s.now);
    const int mph = std::max(1, s.march_hours_per_day);
    if (static_cast<int>(y.h) >= mph) return steps;  // resting

    for (auto& kv : s.armies) {
        Army& a = kv.second;
        if (a.path.empty()) continue;
        const std::string edge_id = a.path.front();
        auto eit = s.edges.find(edge_id);
        if (eit == s.edges.end()) {
            a.path.clear();
            a.hours_into_edge = 0;
            continue;
        }
        const Edge& e = eit->second;
        if (e.a != a.province && e.b != a.province) {
            a.path.clear();
            a.hours_into_edge = 0;
            detail::add_log(s, "army " + a.id + " path broken");
            continue;
        }
        // A ceasefire agreed after the order was given still bars the crossing.
        // Friendly stretches of the path continue; the army halts on the edge that would enter.
        if (a.hours_into_edge + 1 >= e.march_hours) {
            std::string blocked;
            if (ceasefire_blocks_path(s, a, {edge_id}, blocked)) {
                a.path.clear();
                a.hours_into_edge = 0;
                detail::add_log(s, "army " + a.id + " halted at " + a.province +
                                       " (ceasefire_blocks_entry: " + blocked + ")");
                continue;
            }
        }
        MoveStep st;
        st.army_id = a.id;
        st.from = a.province;
        st.edge_id = edge_id;
        a.hours_into_edge += 1;
        if (a.hours_into_edge >= e.march_hours) {
            const std::string nxt = other_end(e, a.province);
            a.previous_province = a.province;
            a.province = nxt;
            a.path.erase(a.path.begin());
            a.hours_into_edge = 0;
            st.to = nxt;
            st.completed_edge = true;
            detail::add_log(s, "army " + a.id + " enters " + nxt);
        } else {
            st.to = a.province;
        }
        steps.push_back(st);
    }
    return steps;
}

}  // namespace

CampaignState enqueue_command(const CampaignState& in, CommandPayload p) {
    CampaignState s = in;
    Command c;
    c.apply_on_hour = s.now.hour_index + 1;
    c.sequence = s.next_command_seq++;
    c.payload = std::move(p);
    s.pending_commands.push_back(std::move(c));
    std::stable_sort(s.pending_commands.begin(), s.pending_commands.end(), [](const Command& a, const Command& b) {
        if (a.apply_on_hour != b.apply_on_hour) return a.apply_on_hour < b.apply_on_hour;
        return a.sequence < b.sequence;
    });
    return s;
}

TickOutput advance_hour(const CampaignState& in) {
    TickOutput out;
    CampaignState s = in;
    if (s.pending_battle.active) {
        detail::add_log(s, "hour blocked: battle pending at " + s.pending_battle.province);
        out.state = s;
        return out;
    }

    // Advance clock first so this hour's index is "now" for due commands.
    s.now.hour_index += 1;

    // Consume commands due this hour.
    std::vector<Command> rest;
    for (const Command& c : s.pending_commands) {
        if (c.apply_on_hour == s.now.hour_index)
            apply_one_command(s, c);
        else if (c.apply_on_hour > s.now.hour_index)
            rest.push_back(c);
        // past-due dropped (should not happen)
    }
    s.pending_commands = std::move(rest);

    emit_daily_ai(s);
    decay_grievance(s);
    monthly_supply(s);
    tick_logistics(s, out.interrupts);

    // Snapshot provinces before movement for opposite-edge contact.
    std::map<std::string, std::string> province_before;
    for (const auto& ak : s.armies) province_before[ak.first] = ak.second.province;

    std::vector<MoveStep> steps = advance_movement(s);

    // Opposite-direction same-edge contact in the same hour.
    if (!detail::has_ceasefire(s)) {
        for (size_t i = 0; i < steps.size(); ++i) {
            for (size_t j = i + 1; j < steps.size(); ++j) {
                const MoveStep& a = steps[i];
                const MoveStep& b = steps[j];
                if (a.edge_id != b.edge_id) continue;
                if (!a.completed_edge || !b.completed_edge) continue;
                const Army& aa = s.armies.at(a.army_id);
                const Army& bb = s.armies.at(b.army_id);
                if (!hostile(aa, bb)) continue;
                if (a.from != b.to || a.to != b.from) continue;
                // Contact in the province the lower army_id was leaving.
                const std::string lower = a.army_id < b.army_id ? a.army_id : b.army_id;
                const std::string contact_prov = province_before.at(lower);
                // Put both in the contact province and stop both marches.
                for (const std::string& id : {a.army_id, b.army_id}) {
                    s.armies.at(id).province = contact_prov;
                    s.armies.at(id).path.clear();
                    s.armies.at(id).hours_into_edge = 0;
                }
                std::string atk = a.army_id, def = b.army_id;
                if (a.army_id > b.army_id) std::swap(atk, def);
                // Prefer british as default attacker when present.
                if (s.armies.at(atk).faction != kBritish && s.armies.at(def).faction == kBritish)
                    std::swap(atk, def);
                s.pending_battle = {true, contact_prov, atk, def};
                BattleSetup setup = make_contact_setup(s, contact_prov, atk, def, a.edge_id, 0);
                out.battles.push_back(setup);
                out.interrupts.push_back(Interrupt::ArmiesInContact);
                detail::add_log(s, "armies meet on edge " + a.edge_id + " at " + contact_prov);
            }
        }
    }

    // End-of-hour co-location contact: moving army halts.
    if (!detail::has_ceasefire(s) && !s.pending_battle.active) {
        std::vector<std::string> provinces;
        for (const auto& pk : s.provinces) provinces.push_back(pk.first);
        std::sort(provinces.begin(), provinces.end());
        for (const auto& prov : provinces) {
            std::string atk, def;
            if (!same_province_hostile(s, prov, atk, def)) continue;
            // Contact ends the march for both sides, including a defender already on a path.
            for (const auto& ak : s.armies) {
                if (ak.second.province != prov) continue;
                if (ak.second.faction != kBritish && ak.second.faction != kContinental) continue;
                s.armies.at(ak.first).path.clear();
                s.armies.at(ak.first).hours_into_edge = 0;
            }
            std::string brit_id, cont_id;
            for (const auto& ak : s.armies) {
                if (ak.second.province != prov) continue;
                if (ak.second.faction == kBritish) brit_id = ak.first;
                if (ak.second.faction == kContinental) cont_id = ak.first;
            }
            bool cont_moved = false, brit_moved = false;
            for (const auto& st : steps) {
                if (!st.completed_edge || st.to != prov) continue;
                if (st.army_id == cont_id) cont_moved = true;
                if (st.army_id == brit_id) brit_moved = true;
            }
            if (cont_moved && !brit_moved) {
                atk = cont_id;
                def = brit_id;
            } else {
                atk = brit_id;
                def = cont_id;
            }
            s.pending_battle = {true, prov, atk, def};
            std::string arrival_edge;
            int boat_hours = 99;
            for (const auto& st : steps) {
                if (st.completed_edge && st.to == prov && (st.army_id == atk || st.army_id == def)) {
                    arrival_edge = st.edge_id;
                    auto eit = s.edges.find(st.edge_id);
                    if (eit != s.edges.end() && eit->second.type == "boat") boat_hours = 0;
                }
            }
            BattleSetup setup = make_contact_setup(s, prov, atk, def, arrival_edge, boat_hours);
            out.battles.push_back(setup);
            out.interrupts.push_back(Interrupt::ArmiesInContact);
            detail::add_log(s, "armies meet at " + prov + ": battle pending");
            break;  // one contact interrupt focus; collect all sorted below
        }
    }

    // Decay after contact so a treaty with hours left still covers this hour's march.
    tick_treaties(s);

    // Deterministic sort of interrupts and battles.
    std::sort(out.interrupts.begin(), out.interrupts.end(),
              [](Interrupt a, Interrupt b) { return static_cast<uint8_t>(a) < static_cast<uint8_t>(b); });
    out.interrupts.erase(std::unique(out.interrupts.begin(), out.interrupts.end()), out.interrupts.end());
    std::sort(out.battles.begin(), out.battles.end(),
              [](const BattleSetup& a, const BattleSetup& b) { return a.province_id < b.province_id; });
    // Deduplicate battles by province. Contact takes priority over EventPopup for the same hour.
    {
        std::vector<BattleSetup> uniq;
        std::string last;
        for (const auto& b : out.battles) {
            if (b.province_id == last) continue;
            uniq.push_back(b);
            last = b.province_id;
        }
        out.battles = std::move(uniq);
        if (!out.battles.empty()) {
            out.interrupts.clear();
            out.interrupts.push_back(Interrupt::ArmiesInContact);
        }
    }

    out.state = std::move(s);
    return out;
}

std::vector<std::string> validate_order(const CampaignState& state, const Order& order) {
    using detail::err;
    std::vector<std::string> errors;
    auto ai = state.armies.find(order.army_id);
    if (ai == state.armies.end()) return {err("unknown_army", order.army_id)};
    auto pi = state.provinces.find(order.to_province);
    if (pi == state.provinces.end()) return {err("unknown_province", order.to_province)};
    const Army& army = ai->second;
    if (order.to_province == army.province) return errors;
    const Province& here = state.provinces.at(army.province);
    if (!detail::contains(here.adjacent, order.to_province))
        return {err("not_adjacent", army.province + " -> " + order.to_province)};
    // Harbor gate for ferry/boat one-step
    for (const auto& ek : state.edges) {
        const Edge& e = ek.second;
        bool connects = (e.a == army.province && e.b == order.to_province) ||
                        (e.b == army.province && e.a == order.to_province);
        if (!connects) continue;
        if ((e.type == "ferry" || e.type == "boat")) {
            auto fit = state.factions.find(army.faction);
            if (fit == state.factions.end() || !fit->second.controls_harbor)
                return {err("requires_harbor_control", e.id)};
        }
    }
    if (pi->second.owner != army.faction && detail::has_ceasefire(state) &&
        !detail::has_safe_passage(state, army.id, order.to_province))
        return {err("ceasefire_blocks_entry", order.to_province)};
    return errors;
}

}  // namespace pa250
