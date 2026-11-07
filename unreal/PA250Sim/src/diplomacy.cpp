#include "internal.hpp"

namespace pa250 {

DiplomacyContext build_diplomacy_context(const CampaignState& s, const std::string& faction) {
    DiplomacyContext c;
    c.faction = faction;
    c.counterpart = detail::other_faction(faction);
    c.turn = s.turn;
    auto rit = s.relations.find(faction);
    c.relation = rit == s.relations.end() ? 0 : rit->second;
    auto fit = s.factions.find(faction);
    if (fit != s.factions.end()) {
        c.allowed_clause_types = fit->second.allowed_clause_types;
        c.max_ceasefire_turns = fit->second.max_ceasefire_turns;
        c.max_safe_passage_turns = fit->second.max_safe_passage_turns;
        c.protected_provinces = fit->second.protected_provinces;
    }
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

void check_turns(std::vector<std::string>& e, const char* type, int turns, int max_turns) {
    if (turns < 1) e.push_back(err(std::string(type) + "_turns_invalid", "turns must be >= 1"));
    else if (turns > max_turns)
        e.push_back(err(std::string(type) + "_too_long", std::to_string(turns) + " > " + std::to_string(max_turns)));
}

void validate_clause(const DiplomacyContext& c, const Clause& cl, std::vector<std::string>& e) {
    static const std::vector<std::string> kKnown = {"ceasefire", "prisoner_exchange", "safe_passage",
                                                    "cede_province"};
    if (!detail::contains(kKnown, cl.type)) {
        e.push_back(err("unknown_clause_type", cl.type));
        return;
    }
    if (!detail::contains(c.allowed_clause_types, cl.type)) {
        e.push_back(err("clause_type_not_allowed", cl.type));
        return;
    }
    if (cl.type == "ceasefire") {
        check_turns(e, "ceasefire", cl.turns, c.max_ceasefire_turns);
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
        check_turns(e, "safe_passage", cl.turns, c.max_safe_passage_turns);
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
    if (cl.type == "ceasefire") return "ceasefire for " + std::to_string(cl.turns) + " turns";
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
        return "safe passage for " + cl.army_id + " through " + cl.province_id + " for " + std::to_string(cl.turns) + " turns";
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
    if (!r.accepted || r.proposal.clauses.empty()) return s;  // rejected or empty: no-op
    // Only an AI envoy's terms can be applied; the player never acts as the envoy.
    if (r.faction == in.player_faction || !in.factions.count(r.faction)) return s;
    // Re-validate from authoritative state; all-or-nothing.
    DiplomacyContext ctx = build_diplomacy_context(in, r.faction);
    auto errors = validate_proposal(ctx, r.proposal);
    if (!errors.empty()) return s;  // invalid result: strict no-op (state, log and hash untouched)
    std::string summary;
    for (const auto& cl : r.proposal.clauses) {
        summary += (summary.empty() ? "" : "; ") + summarize(cl);
        if (cl.type == "ceasefire") {
            bool merged = false;
            for (auto& t : s.treaties)
                if (t.type == "ceasefire") {
                    t.turns_remaining = std::max(t.turns_remaining, cl.turns);
                    merged = true;
                }
            if (!merged) s.treaties.push_back({"ceasefire", cl.turns, "", ""});
        } else if (cl.type == "safe_passage") {
            s.treaties.push_back({"safe_passage", cl.turns, cl.army_id, cl.province_id});
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
    int& rel = s.relations[r.faction];
    rel = detail::clamp_int(rel + 3 * static_cast<int>(r.proposal.clauses.size()), -100, 100);
    s.memory.push_back({s.turn, r.faction, "Agreed: " + summary});
    return s;
}

}  // namespace pa250
