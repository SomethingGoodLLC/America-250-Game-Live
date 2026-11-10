#include "internal.hpp"

namespace pa250 {

DiplomacyContext build_diplomacy_context(const CampaignState& s, const std::string& faction) {
    DiplomacyContext c;
    c.faction = faction;
    c.counterpart = detail::other_faction(faction);
    c.hour_index = s.now.hour_index;
    auto rit = s.relations.find(faction);
    c.relation = rit == s.relations.end() ? 0 : rit->second;
    auto fit = s.factions.find(faction);
    if (fit != s.factions.end()) {
        c.allowed_clause_types = fit->second.allowed_clause_types;
        c.max_ceasefire_days = fit->second.max_ceasefire_days;
        c.max_safe_passage_days = fit->second.max_safe_passage_days;
        c.protected_provinces = fit->second.protected_provinces;
    }
    c.titles = {{"colonel", 20}, {"baron", 35}, {"mayor_of_boston", 45}, {"governor_of_massachusetts", 60}};
    if (fit != s.factions.end()) {
        c.envoy_loyalty = fit->second.envoy_loyalty;
        c.envoy_defected = fit->second.envoy_defected;
    }
    c.player_gold = s.player_gold;
    c.player_vassal = s.player_vassal;
    if (fit != s.factions.end()) {
        c.grievance = fit->second.grievance;
        c.snubbed_hours_left = static_cast<int>(std::max<int64_t>(0, fit->second.snub_until_hour - s.now.hour_index));
        c.mood = c.grievance >= 60 ? "angry" : c.grievance >= 30 ? "insulted" : c.grievance >= 10 ? "irritated" : "calm";
    }
    for (const auto& kv : s.armies) {
        c.army_provinces[kv.first] = kv.second.province;
        int64_t total = 0;
        for (const auto& u : kv.second.units) total += std::max(0, u.second);
        c.army_strengths[kv.first] = total;
    }
    for (const auto& kv : s.provinces) c.adjacency[kv.first] = kv.second.adjacent;
    // Cost of a conscience: loyalty, eased by goodwill and hardened by ill will.
    c.defect_price = std::max(10, c.envoy_loyalty - c.relation / 5);
    for (const auto& kv : s.provinces) c.provinces[kv.first] = kv.second.owner;
    for (const auto& kv : s.armies) c.armies[kv.first] = kv.second.faction;
    c.prisoners_held = s.prisoners_held;
    c.treaties = s.treaties;
    size_t start = s.memory.size() > 5 ? s.memory.size() - 5 : 0;
    for (size_t i = start; i < s.memory.size(); ++i) c.memory.push_back(s.memory[i].text);
    return c;
}

namespace {

using detail::err;

void check_days(std::vector<std::string>& e, const char* type, int days, int max_days) {
    if (days < 1) e.push_back(err(std::string(type) + "_days_invalid", "days must be >= 1"));
    else if (days > max_days)
        e.push_back(err(std::string(type) + "_too_long", std::to_string(days) + " > " + std::to_string(max_days)));
}

void validate_clause(const DiplomacyContext& c, const Clause& cl, std::vector<std::string>& e) {
    static const std::vector<std::string> kKnown = {"ceasefire", "prisoner_exchange", "safe_passage",
                                                    "cede_province", "defect", "surrender", "vassalage",
                                                    "take_offense"};
    if (!detail::contains(kKnown, cl.type)) {
        e.push_back(err("unknown_clause_type", cl.type));
        return;
    }
    if (!detail::contains(c.allowed_clause_types, cl.type)) {
        e.push_back(err("clause_type_not_allowed", cl.type));
        return;
    }
    if (c.envoy_defected) {
        e.push_back(err("envoy_already_defected", cl.type));
        return;
    }
    if (c.snubbed_hours_left > 0) {
        e.push_back(err("envoy_refuses_to_negotiate", std::to_string(c.snubbed_hours_left) + "h"));
        return;
    }
    if (cl.type == "ceasefire") {
        check_days(e, "ceasefire", cl.days, c.max_ceasefire_days);
    } else if (cl.type == "prisoner_exchange") {
        if (!cl.has_counts) {
            e.push_back(err("prisoner_counts_missing"));
            return;
        }
        int total = 0;
        for (const auto& kv : cl.counts) {
            if (kv.first != kBritish && kv.first != kContinental) {
                e.push_back(err("unknown_faction", kv.first));
                continue;
            }
            if (kv.second < 0) e.push_back(err("prisoner_count_negative", kv.first));
            auto hit = c.prisoners_held.find(kv.first);
            int held = hit == c.prisoners_held.end() ? 0 : hit->second;
            if (kv.second > held)
                e.push_back(err("prisoners_exceed_held", kv.first + " " + std::to_string(kv.second) + " > " +
                                                             std::to_string(held)));
            total += std::max(0, kv.second);
        }
        if (total == 0) e.push_back(err("prisoner_exchange_empty"));
    } else if (cl.type == "safe_passage") {
        auto ait = c.armies.find(cl.army_id);
        if (ait == c.armies.end()) e.push_back(err("unknown_army", cl.army_id));
        else if (ait->second != c.counterpart) e.push_back(err("army_not_counterpart", cl.army_id));
        auto pit = c.provinces.find(cl.province_id);
        if (pit == c.provinces.end()) e.push_back(err("unknown_province", cl.province_id));
        else if (pit->second != c.faction) e.push_back(err("province_not_owned_by_envoy", cl.province_id));
        check_days(e, "safe_passage", cl.days, c.max_safe_passage_days);
    } else if (cl.type == "take_offense") {
        static const std::vector<std::string> kReasons = {"disrespect", "insolence", "threat", "deceit"};
        if (!detail::contains(kReasons, cl.reason)) e.push_back(err("unknown_offense_reason", cl.reason));
        check_days(e, "offense", cl.days, 7);
    } else if (cl.type == "vassalage") {
        // The Crown's offer to the player. Gold is not capped; the player alone decides whether to accept.
        if (c.player_vassal) e.push_back(err("already_vassal"));
        if (c.relation < -60) e.push_back(err("envoy_too_hostile", std::to_string(c.relation)));
        if (!cl.title.empty() && !c.titles.count(cl.title)) e.push_back(err("unknown_title", cl.title));
        if (cl.gold < 0) e.push_back(err("vassal_gold_invalid", "gold must be >= 0"));
        std::set<std::string> seen_v;
        for (const auto& p : cl.provinces) {
            auto pit = c.provinces.find(p);
            if (pit == c.provinces.end()) e.push_back(err("unknown_province", p));
            else if (pit->second != c.faction) e.push_back(err("province_not_owned_by_envoy", p));
            else if (detail::contains(c.protected_provinces, p)) e.push_back(err("red_line_protected_province", p));
            else if (!seen_v.insert(p).second) e.push_back(err("duplicate_province", p));
        }
    } else if (cl.type == "surrender") {
        // Honors of war (Fort Necessity, 1754): a cornered army may march out unharmed and paroled.
        auto ait = c.armies.find(cl.army_id);
        if (ait == c.armies.end()) {
            e.push_back(err("unknown_army", cl.army_id));
            return;
        }
        if (ait->second != c.faction) {
            e.push_back(err("army_not_envoys", cl.army_id));
            return;
        }
        check_days(e, "parole", cl.days, std::min(30, c.max_ceasefire_days));
        const std::string at = detail::map_get(c.army_provinces, cl.army_id);
        if (detail::contains(c.protected_provinces, at)) e.push_back(err("red_line_protected_province", at));
        const auto adj_it = c.adjacency.find(at);
        std::vector<std::string> near = {at};
        if (adj_it != c.adjacency.end()) near.insert(near.end(), adj_it->second.begin(), adj_it->second.end());
        int64_t foe = 0;
        for (const auto& kv : c.armies)
            if (kv.second == c.counterpart && detail::contains(near, detail::map_get(c.army_provinces, kv.first)))
                foe += detail::map_get(c.army_strengths, kv.first);
        const int64_t own = detail::map_get(c.army_strengths, cl.army_id);
        if (foe * 2 < own * 3) e.push_back(err("not_cornered", std::to_string(foe) + " vs " + std::to_string(own)));
        bool can_retreat = false;
        if (adj_it != c.adjacency.end())
            for (const auto& n : adj_it->second) {
                auto oit = c.provinces.find(n);
                if (oit != c.provinces.end() && oit->second == c.faction) can_retreat = true;
            }
        if (!can_retreat) e.push_back(err("no_retreat", at));
    } else if (cl.type == "defect") {
        int value = 0;
        if (!cl.title.empty()) {
            auto tit = c.titles.find(cl.title);
            if (tit == c.titles.end()) e.push_back(err("unknown_title", cl.title));
            else value += tit->second;
        }
        if (cl.gold < 0) e.push_back(err("defect_gold_invalid", "gold must be >= 0"));
        else if (cl.gold > c.player_gold)
            e.push_back(err("gold_exceeds_treasury", std::to_string(cl.gold) + " > " + std::to_string(c.player_gold)));
        else value += cl.gold / 10;  // linear and uncapped: only the treasury limits what you can pay
        std::set<std::string> seen_p;
        for (const auto& p : cl.provinces) {
            auto pit = c.provinces.find(p);
            if (pit == c.provinces.end()) e.push_back(err("unknown_province", p));
            else if (pit->second != c.counterpart) e.push_back(err("province_not_owned_by_player", p));
            else if (!seen_p.insert(p).second) e.push_back(err("duplicate_province", p));
            else value += 15;
        }
        if (value < c.defect_price)
            e.push_back(err("offer_insufficient", std::to_string(value) + " < " + std::to_string(c.defect_price)));
    } else if (cl.type == "cede_province") {
        auto pit = c.provinces.find(cl.province_id);
        if (pit == c.provinces.end()) {
            e.push_back(err("unknown_province", cl.province_id));
        } else {
            if (pit->second != c.faction) e.push_back(err("province_not_owned_by_envoy", cl.province_id));
            if (detail::contains(c.protected_provinces, cl.province_id))
                e.push_back(err("red_line_protected_province", cl.province_id));
        }
    }
}

}  // namespace

std::vector<std::string> validate_proposal(const DiplomacyContext& c, const DiplomacyProposal& p) {
    std::vector<std::string> e;
    if (p.clauses.size() > 1) {
        for (const auto& cl : p.clauses)
            if (cl.type == "defect" || cl.type == "vassalage" || cl.type == "take_offense") {
                e.push_back(err(cl.type + "_must_be_alone"));
                break;
            }
    }
    if (p.clauses.size() > 4) e.push_back(err("too_many_clauses", std::to_string(p.clauses.size())));
    std::set<std::string> seen;
    for (const auto& cl : p.clauses) {
        if (!seen.insert(cl.type).second) e.push_back(err("duplicate_clause_type", cl.type));
        validate_clause(c, cl, e);
    }
    return e;
}

namespace {

std::string summarize(const Clause& cl) {
    if (cl.type == "ceasefire") return "ceasefire for " + std::to_string(cl.days) + " days";
    if (cl.type == "prisoner_exchange") {
        std::string s = "prisoner exchange (";
        bool first = true;
        for (const auto& kv : cl.counts) {
            s += (first ? "" : ", ") + kv.first + " " + std::to_string(kv.second);
            first = false;
        }
        return s + ")";
    }
    if (cl.type == "safe_passage")
        return "safe passage for " + cl.army_id + " through " + cl.province_id + " for " +
               std::to_string(cl.days) + " days";
    if (cl.type == "take_offense") return "the envoy takes offense (" + cl.reason + ")";
    if (cl.type == "vassalage") return "you pledge yourself a vassal of the Crown (title " + cl.title + ", " + std::to_string(cl.gold) + " gold)";
    if (cl.type == "surrender")
        return cl.army_id + " surrenders with the honors of war (paroled " + std::to_string(cl.days) + " days)";
    if (cl.type == "defect") return "the envoy defects (title " + cl.title + ", " + std::to_string(cl.gold) + " gold)";
    return "cession of " + cl.province_id;
}

void add_troops(CampaignState& s, const std::string& faction, const std::string& cls, int n) {
    if (n <= 0) return;
    for (auto& kv : s.armies)
        if (kv.second.faction == faction) {
            kv.second.units[cls] += n;
            return;
        }
}

}  // namespace

CampaignState apply_diplomacy(const CampaignState& in, const DiplomacyResult& r) {
    CampaignState s = in;
    if (!r.accepted || r.proposal.clauses.empty()) return s;
    if (r.faction == in.player_faction || !in.factions.count(r.faction)) return s;
    DiplomacyContext ctx = build_diplomacy_context(in, r.faction);
    auto errors = validate_proposal(ctx, r.proposal);
    if (!errors.empty()) return s;
    std::string summary;
    for (const auto& cl : r.proposal.clauses) {
        summary += (summary.empty() ? "" : "; ") + summarize(cl);
        if (cl.type == "ceasefire") {
            const int hours = cl.days * 24;
            bool merged = false;
            for (auto& t : s.treaties)
                if (t.type == "ceasefire") {
                    t.hours_remaining = std::max(t.hours_remaining, hours);
                    merged = true;
                }
            if (!merged) s.treaties.push_back({"ceasefire", hours, "", ""});
        } else if (cl.type == "safe_passage") {
            s.treaties.push_back({"safe_passage", cl.days * 24, cl.army_id, cl.province_id});
        } else if (cl.type == "take_offense") {
            FactionInfo& fi = s.factions.at(r.faction);
            const int w = cl.reason == "deceit" ? 50 : cl.reason == "threat" ? 40 : 30;
            fi.grievance = detail::clamp_int(fi.grievance + w, 0, 100);
            fi.snub_until_hour = s.now.hour_index + static_cast<int64_t>(cl.days) * 24;
            int& rel = s.relations[r.faction];
            rel = detail::clamp_int(rel - 10, -100, 100);
            s.memory.push_back({s.now.hour_index, r.faction,
                                fi.envoy_name + " takes offense (" + cl.reason + ") and will not treat for " +
                                    std::to_string(cl.days) + " days."});
        } else if (cl.type == "vassalage") {
            s.player_vassal = true;
            s.player_title = cl.title;
            s.player_gold += cl.gold;
            if (cl.gold != 0) {
                ResourceChange ch;
                ch.reason = "Vassalage: +" + std::to_string(cl.gold) + " gold";
                ch.gold = cl.gold;
                detail::record_resource_change(s, ch);
            }
            for (const auto& p : cl.provinces) s.provinces.at(p).owner = ctx.counterpart;
            s.treaties.push_back({"vassalage", 24 * 365 * 100, "", ""});  // the Crown's peace; ends only by treachery
            s.relations[r.faction] = std::max(s.relations[r.faction], 60);
        } else if (cl.type == "surrender") {
            Army& army = s.armies.at(cl.army_id);
            const std::string from = army.province;
            // Retreat to an envoy-owned neighbour, preferring a protected one (the army falls back on its base).
            std::string dest;
            for (const auto& n : s.provinces.at(from).adjacent) {
                if (s.provinces.at(n).owner != r.faction) continue;
                if (dest.empty() || detail::contains(ctx.protected_provinces, n)) dest = n;
            }
            if (s.provinces.at(from).owner == r.faction) s.provinces.at(from).owner = ctx.counterpart;
            army.previous_province = from;
            army.province = dest;
            army.path.clear();
            army.hours_into_edge = 0;  // unharmed: units and morale untouched
            if (s.pending_battle.active &&
                (s.pending_battle.attacker_army == cl.army_id || s.pending_battle.defender_army == cl.army_id))
                s.pending_battle = {};
            const int hours = cl.days * 24;  // the parole is a ceasefire both sides observe
            bool merged = false;
            for (auto& t : s.treaties)
                if (t.type == "ceasefire") {
                    t.hours_remaining = std::max(t.hours_remaining, hours);
                    merged = true;
                }
            if (!merged) s.treaties.push_back({"ceasefire", hours, "", ""});
            s.memory.push_back({s.now.hour_index, r.faction,
                                army.name + " marched out of " + s.provinces.at(from).name + " with the honors of war."});
        } else if (cl.type == "defect") {
            FactionInfo& fi = s.factions.at(r.faction);
            fi.envoy_defected = true;
            fi.envoy_title_granted = cl.title;
            s.player_gold -= cl.gold;
            if (cl.gold != 0) {
                ResourceChange ch;
                ch.reason = "Defect: -" + std::to_string(cl.gold) + " gold";
                ch.gold = -cl.gold;
                detail::record_resource_change(s, ch);
            }
            for (const auto& p : cl.provinces) s.fiefs[p] = fi.envoy_name;
            // He brings his command over; as Mayor of Boston he opens the gates.
            for (auto& kv : s.armies)
                if (kv.second.faction == r.faction) {
                    kv.second.faction = ctx.counterpart;
                    kv.second.path.clear();
                    break;
                }
            if (cl.title == "mayor_of_boston" && s.provinces.count("boston"))
                s.provinces.at("boston").owner = ctx.counterpart;
            // The Crown does not forgive: attitude collapses and a reprisal force lands at Boston harbour.
            s.relations[r.faction] = -100;
            if (!s.armies.count("british_reprisal") && s.provinces.count("boston")) {
                Army rp;
                rp.id = "british_reprisal";
                rp.name = "Reprisal Column";
                rp.faction = r.faction;
                rp.province = "boston";
                rp.previous_province = "boston";
                rp.units = {{"line_infantry", 40}};
                rp.morale = 85;
                s.armies[rp.id] = rp;
                s.memory.push_back({s.now.hour_index, r.faction,
                                    fi.envoy_name + " has betrayed the Crown. A reprisal column lands at Boston."});
            }
        } else if (cl.type == "cede_province") {
            s.provinces.at(cl.province_id).owner = ctx.counterpart;
        } else if (cl.type == "prisoner_exchange") {
            int from_brit = cl.counts.count(kBritish) ? cl.counts.at(kBritish) : 0;
            int from_cont = cl.counts.count(kContinental) ? cl.counts.at(kContinental) : 0;
            s.prisoners_held[kBritish] -= from_brit;
            s.prisoners_held[kContinental] -= from_cont;
            add_troops(s, kContinental, "militia", from_brit);
            add_troops(s, kBritish, "line_infantry", from_cont);
        }
    }
    bool offended = false;
    for (const auto& cl : r.proposal.clauses) offended = offended || cl.type == "take_offense";
    if (!s.factions.at(r.faction).envoy_defected && !offended) {  // a betrayal leaves the Crown's attitude at -100
        int& rel = s.relations[r.faction];
        rel = detail::clamp_int(rel + 3 * static_cast<int>(r.proposal.clauses.size()), -100, 100);
    }
    s.memory.push_back({s.now.hour_index, r.faction, "Agreed: " + summary});
    return s;
}

CampaignState register_diplomacy_failure(const CampaignState& in, const std::string& faction) {
    CampaignState s = in;
    auto fit = s.factions.find(faction);
    if (fit == s.factions.end() || faction == in.player_faction || fit->second.envoy_defected) return s;
    FactionInfo& fi = fit->second;
    fi.grievance = detail::clamp_int(fi.grievance + 8, 0, 100);
    int& rel = s.relations[faction];
    rel = detail::clamp_int(rel - 2, -100, 100);
    // Talks that keep failing end in a snub: Washington would not treat for days over a slight.
    if (fi.grievance >= 30 && fi.snub_until_hour <= s.now.hour_index)
        fi.snub_until_hour = s.now.hour_index + (fi.grievance >= 60 ? 7 : 3) * 24;
    s.memory.push_back({s.now.hour_index, faction, "Talks with the player broke down without agreement."});
    return s;
}

}  // namespace pa250
