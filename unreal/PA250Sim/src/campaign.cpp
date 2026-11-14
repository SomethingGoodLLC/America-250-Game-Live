// Campaign arc: win and loss, missions, enlistments, occupation, the scored British AI and the
// sim-only battle resolver. Plain C++; state changes only through the hourly tick and commands.
// GAMEPLAY TUNING lives in data/campaign_rules_boston_1775.json and data/missions_boston_1775.json.
#include "internal.hpp"
#include "nlohmann/json.hpp"

namespace pa250 {
namespace {

CampaignRules g_campaign_rules;
bool g_campaign_rules_loaded = false;
std::vector<MissionDef> g_missions;
bool g_missions_loaded = false;

YMDH cmp_ymdh(const nlohmann::json& j, YMDH fallback) {
    if (!j.is_object()) return fallback;
    YMDH v;
    v.y = j.value("y", static_cast<int>(fallback.y));
    v.m = static_cast<uint8_t>(j.value("m", static_cast<int>(fallback.m)));
    v.d = static_cast<uint8_t>(j.value("d", static_cast<int>(fallback.d)));
    v.h = static_cast<uint8_t>(j.value("h", 0));
    return v;
}

int64_t cmp_hour(YMDH v) { return from_ymdh(v).hour_index; }

std::string cmp_date_words(YMDH v) {
    static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
    const int m = std::max(1, std::min(12, static_cast<int>(v.m)));
    return std::to_string(static_cast<int>(v.d)) + " " + months[m - 1] + " " + std::to_string(v.y);
}

std::string cmp_town(const CampaignState& s, const std::string& id) {
    auto it = s.provinces.find(id);
    return it == s.provinces.end() ? id : it->second.name;
}

const MissionDef* cmp_find_mission(const std::string& id) {
    for (const MissionDef& m : g_missions)
        if (m.id == id) return &m;
    return nullptr;
}

bool cmp_known_predicate(const std::string& t) {
    for (const char* k : {"magazine_emptied", "enemy_army_in", "hold_all", "owner_any", "owner_is",
                          "provisions_at_most", "provisions_at_least", "rounds_per_man_at_least",
                          "project_status", "item_at_fortified", "date_reached", "all"})
        if (t == k) return true;
    return false;
}

// Closed set: any key outside it is an error, so a typo cannot silently become "always true".
bool cmp_parse_predicate(const nlohmann::json& j, MissionPredicate& out, std::vector<std::string>& errors,
                         const std::string& where) {
    if (!j.is_object() || !j.contains("type") || !j.at("type").is_string()) {
        errors.push_back(detail::err("malformed_predicate", where));
        return false;
    }
    out = MissionPredicate{};
    out.type = j.at("type").get<std::string>();
    if (!cmp_known_predicate(out.type)) {
        errors.push_back(detail::err("unknown_predicate", where + " " + out.type));
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        if (k == "type") continue;
        if (k == "province" && it.value().is_string()) out.province = it.value().get<std::string>();
        else if (k == "provinces" && it.value().is_array()) out.provinces = it.value().get<std::vector<std::string>>();
        else if (k == "faction" && it.value().is_string()) out.faction = it.value().get<std::string>();
        else if (k == "amount" && it.value().is_number_integer()) out.amount = it.value().get<int>();
        else if (k == "rounds" && it.value().is_number_integer()) out.rounds = it.value().get<int>();
        else if (k == "project" && it.value().is_string()) out.project = it.value().get<std::string>();
        else if (k == "status" && it.value().is_string()) out.status = it.value().get<std::string>();
        else if (k == "min_fort_level" && it.value().is_number_integer()) out.min_fort_level = it.value().get<int>();
        else if (k == "date") out.date = cmp_ymdh(it.value(), YMDH{});
        else if (k == "of" && it.value().is_array()) {
            for (const auto& sub : it.value()) {
                MissionPredicate p;
                if (cmp_parse_predicate(sub, p, errors, where)) out.of.push_back(p);
            }
        } else {
            errors.push_back(detail::err("unexpected_predicate_field", where + " " + k));
            return false;
        }
    }
    return true;
}

bool cmp_parse_effects(const nlohmann::json& j, MissionEffects& out, std::vector<std::string>& errors,
                       const std::string& where) {
    out = MissionEffects{};
    if (!j.is_object()) {
        errors.push_back(detail::err("malformed_effects", where));
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        if (k == "gold") out.gold = it.value().get<int>();
        else if (k == "local_support") out.local_support = it.value().get<int>();
        else if (k == "british_attitude") out.british_attitude = it.value().get<int>();
        else if (k == "army_morale") out.army_morale = it.value().get<int>();
        else if (k == "army_readiness") out.army_readiness = it.value().get<int>();
        else if (k == "start_evacuation") out.start_evacuation = it.value().get<bool>();
        else if (k == "summary") out.summary = it.value().get<std::string>();
        else if (k == "stores" && it.value().is_object()) {
            const auto& st = it.value();
            out.stores_province = st.value("province", "");
            out.powder = st.value("powder", 0);
            out.cartridges = st.value("cartridges", 0);
            out.muskets = st.value("muskets", 0);
            out.provisions = st.value("provisions", 0);
        } else {
            errors.push_back(detail::err("unknown_effect", where + " " + k));
            return false;
        }
    }
    return true;
}

bool cmp_army_present(const CampaignState& s, const std::string& province, const std::string& faction) {
    for (const auto& ak : s.armies)
        if (ak.second.province == province && ak.second.faction == faction && detail::total_units(ak.second) > 0)
            return true;
    return false;
}

int cmp_units_in(const CampaignState& s, const std::string& province, const std::string& faction) {
    int n = 0;
    for (const auto& ak : s.armies)
        if (ak.second.province == province && ak.second.faction == faction) n += std::max(0, detail::total_units(ak.second));
    return n;
}

// Rounds a man an army of `faction` would take the field with in a town it holds (BattleSetup rule).
int cmp_rounds_at(const CampaignState& s, const std::string& province, const std::string& faction) {
    auto pit = s.provinces.find(province);
    if (pit == s.provinces.end() || pit->second.owner != faction) return 0;
    const int men = cmp_units_in(s, province, faction) * battle_rules().soldiers_per_unit;
    if (men <= 0) return 0;
    // As an army forming up there would find it: cartridges, powder made up, the workshop's share.
    const int cartridges = magazine_cartridges(s, province) +
                           std::max(0, pit->second.workshop_level) * workshop_cartridges_per_level();
    return rounds_per_man_for(cartridges, men);
}

std::string cmp_project_words(const std::string& status) {
    if (status == "offered") return "offered, not yet sent for";
    if (status == "underway") return "on the road";
    if (status == "delivered") return "delivered";
    if (status == "declined") return "not sent for";
    return "not yet sent for";
}

void cmp_note(CampaignState& s, const std::string& reason, const std::string& province = std::string(), int gold = 0,
              int support = 0) {
    ResourceChange ch;
    ch.reason = reason;
    ch.province_id = province;
    ch.gold = gold;
    ch.local_support = support;
    detail::record_resource_change(s, ch);
}

// ---- Hourly rules --------------------------------------------------------

void cmp_tick_projects(CampaignState& s) {
    CampaignProject& k = s.progress.knox;
    if (k.status == "underway" && k.ready_hour > 0 && s.now.hour_index >= k.ready_hour) {
        k.status = "delivered";
        if (k.pieces <= 0) k.pieces = 59;
    }
    if (k.status != "delivered") return;
    if (k.province.empty()) {
        k.province = campaign_rules().patriot_hq;
        cmp_note(s, "The siege guns have come in at " + cmp_town(s, k.province) + ": an army there can haul them", k.province);
    }
    if (k.army_id.empty()) return;
    auto ait = s.armies.find(k.army_id);
    if (ait == s.armies.end() || detail::total_units(ait->second) <= 0) {
        k.army_id.clear();  // the hauling army is gone: the guns lie where it left them
        return;
    }
    k.province = ait->second.province;
}

// An army standing alone in an enemy town at the occupation hour takes it; a fort is besieged.
void cmp_tick_occupation(CampaignState& s) {
    if (static_cast<int>(to_ymdh(s.now).h) != campaign_rules().occupation_hour) return;
    if (detail::has_ceasefire(s)) return;
    for (auto& pk : s.provinces) {
        Province& p = pk.second;
        bool brit = false, cont = false, passage = false;
        for (const auto& ak : s.armies) {
            if (ak.second.province != pk.first || detail::total_units(ak.second) <= 0) continue;
            (ak.second.faction == kBritish ? brit : cont) = true;
            if (detail::has_safe_passage(s, ak.first, pk.first)) passage = true;
        }
        if (brit == cont || passage) continue;  // nobody, or both (a battle), or a guest
        const std::string occupier = brit ? kBritish : kContinental;
        if (p.owner == occupier) continue;
        if (p.fort_level > 0) {
            if (p.siege_by == occupier) continue;
            p.siege_by = occupier;
            p.siege_hours_remaining = battle_rules().siege_hours_per_fort_level * p.fort_level;
            cmp_note(s,
                     p.name + " is besieged: Fort L" + std::to_string(p.fort_level) + " holds for " +
                         std::to_string(p.siege_hours_remaining) + " hours unless relieved",
                     pk.first);
            continue;
        }
        const std::string before = p.owner;
        p.owner = occupier;
        p.siege_by.clear();
        p.siege_hours_remaining = 0;
        cmp_note(s, p.name + " occupied without a fight: now " + detail::faction_word(occupier) + " (was " + detail::faction_word(before) + ")", pk.first);
    }
}

void cmp_tick_enlistments(CampaignState& s) {
    const CampaignRules& R = campaign_rules();
    CampaignProgress& P = s.progress;
    if (!P.enlistment_warned && s.now.hour_index >= cmp_hour(R.enlist_warning)) {
        P.enlistment_warned = true;
        cmp_note(s, "Enlistments run out with the year: re-enlist your companies (" +
                        std::to_string(R.enlist_gold_per_company) + " gold each) or they go home on " +
                        cmp_date_words(R.enlist_expiry));
    }
    if (P.enlistments_expired || s.now.hour_index < cmp_hour(R.enlist_expiry)) return;
    P.enlistments_expired = true;
    int gone = 0;
    for (auto it = s.armies.begin(); it != s.armies.end();) {
        Army& a = it->second;
        if (a.faction == kContinental) {
            for (auto& u : a.units) {
                if (detail::contains(R.enlist_exempt, u.first)) continue;
                const int keep = std::min(std::max(0, u.second), std::max(0, detail::map_get(a.reenlisted, u.first)));
                gone += std::max(0, u.second) - keep;
                u.second = keep;
            }
            a.reenlisted.clear();
            if (detail::total_units(a) <= 0) {
                detail::add_log(s, "army disbanded, enlistments expired: " + it->first);
                if (P.knox.army_id == it->first) P.knox.army_id.clear();
                it = s.armies.erase(it);
                continue;
            }
        }
        ++it;
    }
    cmp_note(s, gone > 0 ? "Enlistments expired: " + std::to_string(gone) + " units went home"
                         : "Enlistments expired: every company had re-enlisted");
}

void cmp_enqueue_mission(CampaignState& s, const std::string& id, const char* outcome) {
    CommandPayload p;
    p.kind = CommandKind::MissionEffect;
    p.result_json = nlohmann::json{{"mission_id", id}, {"outcome", outcome}}.dump();
    s = enqueue_command(s, p);
}

void cmp_tick_missions(CampaignState& s) {
    if (!g_missions_loaded) return;
    for (const MissionDef& m : g_missions) {
        MissionState& st = s.progress.missions[m.id];
        if (st.status == "locked") {
            bool ready = true;
            if (!m.prerequisite.empty()) {
                const std::string before = s.progress.missions[m.prerequisite].status;
                ready = m.prerequisite_must_succeed ? before == "complete" : (before == "complete" || before == "failed");
            }
            if (ready && m.has_available_from && s.now.hour_index < cmp_hour(m.available_from)) ready = false;
            if (!ready) continue;
            st.status = "active";
            st.activated_hour = s.now.hour_index;
            st.deadline_hour = m.has_deadline ? cmp_hour(m.deadline)
                                              : (m.deadline_days > 0 ? s.now.hour_index + 24LL * m.deadline_days : 0);
            detail::add_log(s, "mission open (" + m.faction + "): " + m.title);
            continue;  // judged from the next hour, so a mission is never opened and closed in one tick
        }
        if (st.status != "active") continue;
        if (mission_predicate(s, m.faction, m.complete)) {
            st.status = "complete";
            st.resolved_hour = s.now.hour_index;
            cmp_enqueue_mission(s, m.id, "complete");
        } else if ((m.has_fail && mission_predicate(s, m.faction, m.fail)) ||
                   (st.deadline_hour > 0 && s.now.hour_index >= st.deadline_hour)) {
            st.status = "failed";
            st.resolved_hour = s.now.hour_index;
            cmp_enqueue_mission(s, m.id, "failed");
        }
    }
}

// With the guns on the fortified heights the British sail when the clock runs out; retaking the
// heights stops it and reopens the mission that started it.
void cmp_tick_evacuation(CampaignState& s) {
    CampaignProgress& P = s.progress;
    if (P.evacuation_hours_remaining <= 0 || P.evacuated) return;
    const CampaignRules& R = campaign_rules();
    auto hit = s.provinces.find(R.heights);
    auto bit = s.provinces.find(R.british_base);
    if (hit == s.provinces.end() || bit == s.provinces.end()) return;
    const std::string besieger = detail::other_faction(bit->second.owner);
    if (hit->second.owner != besieger) {
        P.evacuation_hours_remaining = 0;
        for (const MissionDef& m : g_missions)
            if (m.reward.start_evacuation && P.missions[m.id].status == "complete") P.missions[m.id].status = "active";
        cmp_note(s, "The British hold " + hit->second.name + " again: they stay in " + bit->second.name, R.heights);
        return;
    }
    if (--P.evacuation_hours_remaining > 0) return;
    const std::string leaving = bit->second.owner;
    P.evacuated = true;
    for (auto it = s.armies.begin(); it != s.armies.end();) {
        if (it->second.faction == leaving && it->second.province == R.british_base) it = s.armies.erase(it);
        else ++it;
    }
    bit->second.owner = besieger;
    bit->second.siege_by.clear();
    bit->second.siege_hours_remaining = 0;
    cmp_note(s, "The British evacuate " + bit->second.name + ": the fleet sails and the town is " + detail::faction_word(besieger), R.british_base);
    s.memory.push_back({s.now.hour_index, besieger, "The British evacuated " + bit->second.name});
}

void cmp_tick_outcome(CampaignState& s, std::vector<Interrupt>& interrupts) {
    CampaignProgress& P = s.progress;
    if (P.outcome.decided) return;
    const CampaignRules& R = campaign_rules();
    auto hq = s.provinces.find(R.patriot_hq);
    P.hq_lost_hours = (hq != s.provinces.end() && hq->second.owner == kBritish) ? P.hq_lost_hours + 1 : 0;
    P.support_low_hours = s.local_support < R.support_floor ? P.support_low_hours + 1 : 0;
    const CampaignOutcome o = evaluate_outcome(s);
    if (!o.decided) return;
    P.outcome = o;
    detail::add_log(s, "campaign over: " + o.winner + " (" + o.reason + ")");
    s.memory.push_back({s.now.hour_index, o.winner, "The campaign ended: " + o.winner + " (" + o.reason + ")"});
    interrupts.push_back(Interrupt::CampaignOver);
}

}  // namespace

// ---- Loading ---------------------------------------------------------------

void load_campaign_rules(const std::string& json_text) {
    using nlohmann::json;
    try {
        const json j = json::parse(json_text);
        CampaignRules r;
        if (j.contains("window")) {
            r.window_start = cmp_ymdh(j.at("window").value("start", json::object()), r.window_start);
            r.window_end = cmp_ymdh(j.at("window").value("end", json::object()), r.window_end);
            if (!j.at("window").at("end").contains("h")) r.window_end.h = 23;
        }
        if (j.contains("towns")) {
            r.patriot_hq = j.at("towns").value("patriot_hq", r.patriot_hq);
            r.british_base = j.at("towns").value("british_base", r.british_base);
            r.heights = j.at("towns").value("heights", r.heights);
        }
        if (j.contains("victory")) {
            const json& v = j.at("victory");
            r.hq_held_days = v.value("hq_held_days", r.hq_held_days);
            r.support_floor = v.value("support_floor", r.support_floor);
            r.support_floor_days = v.value("support_floor_days", r.support_floor_days);
            r.score_per_town = v.value("score_per_town", r.score_per_town);
            r.score_support_weight = v.value("score_support_weight", r.score_support_weight);
            r.score_tie_goes_to = v.value("score_tie_goes_to", r.score_tie_goes_to);
        }
        if (j.contains("evacuation")) r.evacuation_hours = j.at("evacuation").value("countdown_hours", r.evacuation_hours);
        if (j.contains("enlistment")) {
            const json& e = j.at("enlistment");
            r.enlist_warning = cmp_ymdh(e.value("warning", json::object()), r.enlist_warning);
            r.enlist_expiry = cmp_ymdh(e.value("expiry", json::object()), r.enlist_expiry);
            r.enlist_gold_per_company = e.value("gold_per_company", r.enlist_gold_per_company);
            r.enlist_exempt = e.value("exempt_classes", r.enlist_exempt);
        }
        if (j.contains("siege_train")) {
            r.siege_train_gold = j.at("siege_train").value("gold_cost", r.siege_train_gold);
            r.siege_train_haul_days = j.at("siege_train").value("haul_days", r.siege_train_haul_days);
            r.knox_field_guns = j.at("siege_train").value("field_guns", r.knox_field_guns);
            r.knox_crew_units = j.at("siege_train").value("crew_units", r.knox_crew_units);
        }
        if (j.contains("occupation")) r.occupation_hour = j.at("occupation").value("hour_of_day", r.occupation_hour);
        if (j.contains("british_ai")) {
            const json& a = j.at("british_ai");
            r.ai_garrison_units = a.value("garrison_units", r.ai_garrison_units);
            r.ai_garrison_max = a.value("garrison_max_units", r.ai_garrison_max);
            r.ai_min_field = a.value("min_field_units", r.ai_min_field);
            r.ai_retreat_morale = a.value("retreat_morale", r.ai_retreat_morale);
            r.ai_rest_morale = a.value("rest_morale_per_day", r.ai_rest_morale);
            r.ai_rest_cap = a.value("rest_morale_cap", r.ai_rest_cap);
            r.ai_recruit_targets = a.value("recruit_targets", r.ai_recruit_targets);
            r.ai_detachment_gun_share_pct = detail::clamp_int(a.value("detachment_gun_share_pct", r.ai_detachment_gun_share_pct), 0, 100);
            if (a.contains("value")) {
                const json& v = a.at("value");
                r.v_cartridge = v.value("per_cartridge", r.v_cartridge);
                r.v_powder = v.value("per_powder", r.v_powder);
                r.v_musket = v.value("per_musket", r.v_musket);
                r.v_provision = v.value("per_provision", r.v_provision);
                r.v_hq = v.value("patriot_hq", r.v_hq);
                r.v_heights = v.value("heights", r.v_heights);
                r.v_heights_fortified = v.value("heights_fortified", r.v_heights_fortified);
                r.v_weak = v.value("weak_town", r.v_weak);
                r.v_forage = v.value("forage_farmland", r.v_forage);
                r.v_mission = v.value("mission_target", r.v_mission);
            }
        }
        if (j.contains("auto_resolve")) {
            const json& a = j.at("auto_resolve");
            r.ar_fort_pct = a.value("fort_bonus_pct_per_level", r.ar_fort_pct);
            r.ar_luck_pct = a.value("luck_pct", r.ar_luck_pct);
            r.ar_loser_pct = a.value("loser_loss_pct", r.ar_loser_pct);
            r.ar_winner_pct = a.value("winner_loss_pct", r.ar_winner_pct);
            r.ar_broke_pct = a.value("broke_ratio_pct", r.ar_broke_pct);
        }
        g_campaign_rules = r;
        g_campaign_rules_loaded = true;
    } catch (...) {
        g_campaign_rules = CampaignRules{};
        g_campaign_rules_loaded = false;
    }
}

void unload_campaign_rules() {
    g_campaign_rules = CampaignRules{};
    g_campaign_rules_loaded = false;
}
bool campaign_rules_loaded() { return g_campaign_rules_loaded; }
const CampaignRules& campaign_rules() { return g_campaign_rules; }

std::vector<std::string> load_missions(const std::string& json_text) {
    using nlohmann::json;
    std::vector<std::string> errors;
    std::vector<MissionDef> defs;
    try {
        const json j = json::parse(json_text);
        for (const auto& mj : j.at("missions")) {
            MissionDef m;
            m.id = mj.at("id").get<std::string>();
            m.faction = mj.at("faction").get<std::string>();
            if (m.faction != kBritish && m.faction != kContinental) errors.push_back(detail::err("unknown_faction", m.id));
            m.title = mj.value("title", m.id);
            m.flavor = mj.value("flavor", "");
            m.goal = mj.value("goal", "");
            m.prerequisite = mj.value("prerequisite", "");
            m.prerequisite_must_succeed = mj.value("prerequisite_must_succeed", false);
            if (mj.contains("available_from")) {
                m.has_available_from = true;
                m.available_from = cmp_ymdh(mj.at("available_from"), YMDH{});
            }
            m.target_provinces = mj.value("target_provinces", std::vector<std::string>{});
            cmp_parse_predicate(mj.at("complete"), m.complete, errors, m.id + ".complete");
            if (mj.contains("fail")) m.has_fail = cmp_parse_predicate(mj.at("fail"), m.fail, errors, m.id + ".fail");
            m.deadline_days = mj.value("deadline_days", 0);
            if (mj.contains("deadline")) {
                m.has_deadline = true;
                m.deadline = cmp_ymdh(mj.at("deadline"), YMDH{});
            }
            if (mj.contains("reward")) cmp_parse_effects(mj.at("reward"), m.reward, errors, m.id + ".reward");
            if (mj.contains("failure")) cmp_parse_effects(mj.at("failure"), m.failure, errors, m.id + ".failure");
            for (const MissionDef& other : defs)
                if (other.id == m.id) errors.push_back(detail::err("duplicate_mission", m.id));
            defs.push_back(m);
        }
        for (const MissionDef& m : defs) {
            if (m.prerequisite.empty()) continue;
            bool found = false;
            for (const MissionDef& other : defs) found = found || other.id == m.prerequisite;
            if (!found) errors.push_back(detail::err("unknown_prerequisite", m.id + " -> " + m.prerequisite));
        }
    } catch (const std::exception& e) {
        errors.push_back(detail::err("missions_parse_error", e.what()));
    }
    if (!errors.empty()) return errors;  // all or nothing: a half-read chain is worse than none
    g_missions = defs;
    g_missions_loaded = true;
    return errors;
}

void unload_missions() {
    g_missions.clear();
    g_missions_loaded = false;
}
bool missions_loaded() { return g_missions_loaded; }
const std::vector<MissionDef>& mission_defs() { return g_missions; }

// ---- Predicates ------------------------------------------------------------

bool mission_predicate(const CampaignState& s, const std::string& faction, const MissionPredicate& p) {
    const std::string enemy = detail::other_faction(faction);
    auto owner = [&s](const std::string& id) {
        auto it = s.provinces.find(id);
        return it == s.provinces.end() ? std::string() : it->second.owner;
    };
    if (p.type == "magazine_emptied") {
        const ProvinceStores& st = province_ledger(s, p.province);
        return st.powder <= 0 && st.cartridges <= 0 && st.muskets <= 0;
    }
    if (p.type == "enemy_army_in") return cmp_army_present(s, p.province, enemy);
    if (p.type == "hold_all") {
        for (const auto& id : p.provinces)
            if (owner(id) != faction) return false;
        return !p.provinces.empty();
    }
    if (p.type == "owner_any") {
        for (const auto& id : p.provinces)
            if (owner(id) == faction) return true;
        return false;
    }
    if (p.type == "owner_is") return owner(p.province) == (p.faction.empty() ? faction : p.faction);
    if (p.type == "provisions_at_most") return province_ledger(s, p.province).provisions <= p.amount;
    if (p.type == "provisions_at_least") return province_ledger(s, p.province).provisions >= p.amount;
    if (p.type == "rounds_per_man_at_least") return cmp_rounds_at(s, p.province, faction) >= p.rounds;
    if (p.type == "project_status") return p.project == "knox" && s.progress.knox.status == p.status;
    if (p.type == "item_at_fortified") {
        auto it = s.provinces.find(p.province);
        return p.project == "knox" && s.progress.knox.status == "delivered" && s.progress.knox.province == p.province &&
               it != s.provinces.end() && it->second.owner == faction && it->second.fort_level >= p.min_fort_level;
    }
    if (p.type == "date_reached") return s.now.hour_index >= cmp_hour(p.date);
    if (p.type == "all") {
        for (const auto& sub : p.of)
            if (!mission_predicate(s, faction, sub)) return false;
        return !p.of.empty();
    }
    return false;
}

std::string mission_progress_text(const CampaignState& s, const std::string& faction, const MissionPredicate& p) {
    if (p.type == "magazine_emptied") {
        const ProvinceStores& st = province_ledger(s, p.province);
        if (st.powder <= 0 && st.cartridges <= 0 && st.muskets <= 0) return cmp_town(s, p.province) + " magazine is empty";
        return cmp_town(s, p.province) + " magazine still holds " + std::to_string(std::max(0, st.cartridges)) +
               " cartridges, " + std::to_string(std::max(0, st.powder)) + " powder, " +
               std::to_string(std::max(0, st.muskets)) + " muskets";
    }
    if (p.type == "enemy_army_in")
        return cmp_army_present(s, p.province, detail::other_faction(faction)) ? "The enemy is in " + cmp_town(s, p.province)
                                                                               : "No enemy in " + cmp_town(s, p.province);
    if (p.type == "hold_all" || p.type == "owner_any") {
        int held = 0;
        std::string missing;
        for (const auto& id : p.provinces) {
            auto it = s.provinces.find(id);
            if (it != s.provinces.end() && it->second.owner == faction) ++held;
            else missing += (missing.empty() ? "" : ", ") + cmp_town(s, id);
        }
        std::string out = std::to_string(held) + " of " + std::to_string(p.provinces.size()) + " towns held";
        if (p.type == "hold_all" && !missing.empty()) out += " (not " + missing + ")";
        return out;
    }
    if (p.type == "owner_is") {
        auto it = s.provinces.find(p.province);
        return cmp_town(s, p.province) + " is " + (it == s.provinces.end() ? std::string("unknown") : detail::faction_word(it->second.owner));
    }
    if (p.type == "provisions_at_most" || p.type == "provisions_at_least")
        return cmp_town(s, p.province) + " provisions: " + std::to_string(province_ledger(s, p.province).provisions) +
               (p.type == "provisions_at_most" ? " (carry them off to " : " (need ") + std::to_string(p.amount) + ")";
    if (p.type == "rounds_per_man_at_least") {
        if (cmp_units_in(s, p.province, faction) <= 0) return "No army at " + cmp_town(s, p.province);
        return cmp_town(s, p.province) + ": " + std::to_string(cmp_rounds_at(s, p.province, faction)) + " of " +
               std::to_string(p.rounds) + " rounds a man";
    }
    if (p.type == "project_status") return "The siege guns: " + cmp_project_words(s.progress.knox.status);
    if (p.type == "item_at_fortified") {
        const CampaignProject& k = s.progress.knox;
        auto it = s.provinces.find(p.province);
        const int fort = it == s.provinces.end() ? 0 : it->second.fort_level;
        std::string guns = k.status != "delivered" ? "The guns are " + cmp_project_words(k.status)
                                                   : "The guns are at " + cmp_town(s, k.province) +
                                                         (k.army_id.empty() ? " (no army hauling them)" : " (on the march)");
        return guns + "; " + cmp_town(s, p.province) + " fort level " + std::to_string(fort) + " of " +
               std::to_string(p.min_fort_level);
    }
    if (p.type == "date_reached")
        return s.now.hour_index >= cmp_hour(p.date) ? "It is past " + cmp_date_words(p.date) : "Until " + cmp_date_words(p.date);
    if (p.type == "all") {
        std::string out;
        for (const auto& sub : p.of) out += (out.empty() ? "" : "; ") + mission_progress_text(s, faction, sub);
        return out;
    }
    return "";
}

// ---- Commands and player actions -------------------------------------------

CampaignState apply_mission_effect(const CampaignState& in, const std::string& mission_id, const std::string& outcome) {
    const MissionDef* m = cmp_find_mission(mission_id);
    if (!m || (outcome != "complete" && outcome != "failed")) return in;
    CampaignState s = in;
    const MissionEffects& e = outcome == "complete" ? m->reward : m->failure;
    const bool players = m->faction == s.player_faction;
    if (e.gold != 0) {
        if (players) s.player_gold = std::max(0, s.player_gold + e.gold);
        else s.progress.enemy_gold = std::max(0, s.progress.enemy_gold + e.gold);
    }
    const int support_before = s.local_support;
    s.local_support = detail::clamp_int(s.local_support + e.local_support, 0, 100);
    if (e.british_attitude != 0 && s.relations.count(kBritish))
        s.relations[kBritish] = detail::clamp_int(s.relations[kBritish] + e.british_attitude, -100, 100);
    for (auto& ak : s.armies) {
        if (ak.second.faction != m->faction) continue;
        ak.second.morale = detail::clamp_int(ak.second.morale + e.army_morale, 0, 100);
        ak.second.readiness = detail::clamp_int(ak.second.readiness + e.army_readiness, 0, 100);
    }
    if (!e.stores_province.empty() && s.provinces.count(e.stores_province)) {
        ProvinceStores& st = province_ledger(s, e.stores_province);
        st.powder = std::max(0, st.powder + e.powder);
        st.cartridges = std::max(0, st.cartridges + e.cartridges);
        st.muskets = std::max(0, st.muskets + e.muskets);
        st.provisions = std::max(0, st.provisions + e.provisions);
    }
    if (e.start_evacuation && !s.progress.evacuated && s.progress.evacuation_hours_remaining <= 0)
        s.progress.evacuation_hours_remaining = std::max(1, campaign_rules().evacuation_hours);
    ResourceChange ch;
    ch.reason = std::string(players ? "Mission " : "The enemy's mission ") + (outcome == "complete" ? "done: " : "failed: ") +
                m->title + (e.summary.empty() ? "" : " - " + e.summary);
    ch.gold = players ? e.gold : 0;
    ch.local_support = s.local_support - support_before;
    detail::record_resource_change(s, ch);
    s.memory.push_back({s.now.hour_index, m->faction, "Mission " + std::string(outcome == "complete" ? "done" : "failed") + ": " + m->title});
    return s;
}

namespace {
int company_size_or_default(const std::string& faction, const std::string& unit_class) {
    const int n = company_size(faction, unit_class);
    return n > 0 ? n : 4;
}
}  // namespace

int reenlist_cost(const CampaignState& s, const std::string& army_id, const std::string& unit_class) {
    auto ait = s.armies.find(army_id);
    if (ait == s.armies.end()) return 0;
    const int units = std::max(0, detail::map_get(ait->second.units, unit_class)) -
                      std::max(0, detail::map_get(ait->second.reenlisted, unit_class));
    if (units <= 0) return 0;
    const int size = company_size_or_default(ait->second.faction, unit_class);
    return ((units + size - 1) / size) * campaign_rules().enlist_gold_per_company;
}

CampaignState reenlist_companies(const CampaignState& in, const std::string& army_id, const std::string& unit_class,
                                 std::string& error) {
    error.clear();
    const CampaignRules& R = campaign_rules();
    auto ait = in.armies.find(army_id);
    if (ait == in.armies.end()) error = "unknown_army";
    else if (ait->second.faction != in.player_faction || ait->second.faction != kContinental) error = "not_your_army";
    else if (!ait->second.units.count(unit_class)) error = "unknown_class";
    else if (detail::contains(R.enlist_exempt, unit_class)) error = "exempt_class";
    else if (in.pending_battle.active) error = "battle_pending";
    else if (in.now.hour_index < cmp_hour(R.enlist_warning)) error = "not_yet";
    else if (in.progress.enlistments_expired || in.now.hour_index >= cmp_hour(R.enlist_expiry)) error = "too_late";
    if (!error.empty()) return in;
    const int cost = reenlist_cost(in, army_id, unit_class);
    const int units = std::max(0, ait->second.units.at(unit_class));
    if (units <= std::max(0, detail::map_get(ait->second.reenlisted, unit_class))) {
        error = "nothing_to_reenlist";
        return in;
    }
    if (in.player_gold < cost) {
        error = "insufficient_gold";
        return in;
    }
    CampaignState s = in;
    s.player_gold -= cost;
    s.armies.at(army_id).reenlisted[unit_class] = units;
    ResourceChange ch;
    ch.reason = "Re-enlisted " + std::to_string(units) + " units of " + detail::class_words(unit_class) + ", " + ait->second.name + ": -" +
                std::to_string(cost) + " gold";
    ch.gold = -cost;
    detail::record_resource_change(s, ch);
    return s;
}

CampaignState send_for_siege_guns(const CampaignState& in, std::string& error) {
    error.clear();
    const CampaignRules& R = campaign_rules();
    const CampaignProject& k = in.progress.knox;
    bool open = false;
    for (const MissionDef& m : g_missions)
        if (m.faction == in.player_faction && m.complete.type == "project_status" && m.complete.project == "knox" &&
            detail::map_get(in.progress.missions, m.id).status == "active")
            open = true;
    if (!open) error = "not_available";
    else if (k.status == "underway") error = "already_underway";
    else if (k.status == "delivered") error = "already_delivered";
    else if (in.player_gold < R.siege_train_gold) error = "insufficient_gold";
    if (!error.empty()) return in;
    CampaignState s = in;
    s.player_gold -= R.siege_train_gold;
    s.progress.knox.status = "underway";
    s.progress.knox.start_hour = s.now.hour_index;
    s.progress.knox.ready_hour = s.now.hour_index + 24LL * R.siege_train_haul_days;
    s.progress.knox.transport_committed = R.siege_train_gold;
    // A letter still asking the same question is answered by this.
    for (auto& letter : s.staff.queue) {
        if (letter.kind != "knox_support" || !letter.awaiting_choice) continue;
        letter.awaiting_choice = false;
        letter.status = "dismissed";
        letter.reason = "The guns were sent for directly.";
        if (s.pending_event_id == letter.id) s.pending_event_id.clear();
    }
    ResourceChange ch;
    ch.reason = "Sent for the siege guns: -" + std::to_string(R.siege_train_gold) + " gold, " +
                std::to_string(R.siege_train_haul_days) + " days on the road";
    ch.gold = -R.siege_train_gold;
    detail::record_resource_change(s, ch);
    return s;
}

CampaignState attach_siege_guns(const CampaignState& in, const std::string& army_id, std::string& error) {
    error.clear();
    const CampaignProject& k = in.progress.knox;
    if (k.status != "delivered" || k.province.empty()) {
        error = "no_guns";
        return in;
    }
    if (army_id.empty()) {
        CampaignState s = in;
        s.progress.knox.army_id.clear();
        detail::add_log(s, "siege guns left at " + k.province);
        return s;
    }
    auto ait = in.armies.find(army_id);
    if (ait == in.armies.end()) error = "unknown_army";
    else if (ait->second.faction != in.player_faction) error = "not_your_army";
    else if (ait->second.province != k.province) error = "not_here";
    if (!error.empty()) return in;
    CampaignState s = in;
    s.progress.knox.army_id = army_id;
    detail::add_log(s, "siege guns hauled by " + army_id);
    // The train's light pieces and the men who serve them join the first army that hauls it. The
    // heavy guns stay the train: they are for the heights, not for the field.
    const CampaignRules& R = campaign_rules();
    if (!s.progress.knox.field_guns_issued && (R.knox_field_guns > 0 || R.knox_crew_units > 0)) {
        s.progress.knox.field_guns_issued = true;
        Army& hauler = s.armies.at(army_id);
        hauler.guns += std::max(0, R.knox_field_guns);
        if (R.knox_crew_units > 0) hauler.units[battle_rules().guns_crew_class] += R.knox_crew_units;
        ResourceChange ch;
        ch.reason = "The train's light pieces join " + hauler.name + ": +" + std::to_string(std::max(0, R.knox_field_guns)) +
                    " field guns with " + std::to_string(std::max(0, R.knox_crew_units)) + " crews";
        ch.province_id = hauler.province;
        detail::record_resource_change(s, ch);
    }
    return s;
}

// ---- Win and loss ------------------------------------------------------------

WindowScore window_score_snapshot(const CampaignState& s) {
    WindowScore w;
    const CampaignRules& R = campaign_rules();
    for (const auto& pk : s.provinces) {
        if (pk.second.owner == kContinental) ++w.patriot_towns;
        if (pk.second.owner == kBritish) ++w.british_towns;
    }
    w.patriot_points = w.patriot_towns * R.score_per_town + s.local_support * R.score_support_weight;
    w.british_points = w.british_towns * R.score_per_town + (100 - s.local_support) * R.score_support_weight;
    const int64_t left = cmp_hour(R.window_end) - s.now.hour_index;
    w.hours_remaining = left > 0 ? left : 0;
    return w;
}

CampaignOutcome evaluate_outcome(const CampaignState& s) {
    CampaignOutcome o;
    if (s.progress.outcome.decided) return s.progress.outcome;
    const CampaignRules& R = campaign_rules();
    const WindowScore score = window_score_snapshot(s);
    auto decide = [&](const char* winner, const char* reason) {
        o.decided = true;
        o.winner = winner;
        o.reason = reason;
        o.hour_index = s.now.hour_index;
    };
    auto base = s.provinces.find(R.british_base);
    bool patriot_army = false;
    for (const auto& ak : s.armies)
        if (ak.second.faction == kContinental && detail::total_units(ak.second) > 0) patriot_army = true;
    bool mustering = false;
    for (const auto& job : s.progress.recruit_jobs) mustering = mustering || job.men > 0;
    if (s.player_vassal) decide(kBritish, "vassalage");
    else if (base != s.provinces.end() && base->second.owner == kContinental)
        decide(kContinental, s.progress.evacuated ? "evacuation" : "boston_taken");
    else if (s.progress.hq_lost_hours >= 24 * R.hq_held_days) decide(kBritish, "hq_lost");
    else if (s.progress.support_low_hours >= 24 * R.support_floor_days) decide(kBritish, "support_collapsed");
    else if (!patriot_army && !(s.player_faction == kContinental && mustering)) decide(kBritish, "no_field_army");
    else if (s.now.hour_index >= cmp_hour(R.window_end)) {
        decide(score.patriot_points > score.british_points ? kContinental
               : score.british_points > score.patriot_points ? kBritish
               : R.score_tie_goes_to.c_str(),
               "window_score");
        o.summary.push_back("Scored at the end of the window: Patriots " + std::to_string(score.patriot_points) + ", British " +
                            std::to_string(score.british_points) + " (towns x " + std::to_string(R.score_per_town) + " + Support)");
    }
    if (!o.decided) return o;
    const YMDH now = to_ymdh(s.now);
    const int64_t days = std::max<int64_t>(0, (s.now.hour_index - cmp_hour(R.window_start)) / 24);
    std::string why;
    if (o.reason == "evacuation") why = "The British evacuated " + cmp_town(s, R.british_base) + " under the guns on the heights.";
    else if (o.reason == "boston_taken") why = cmp_town(s, R.british_base) + " is in Patriot hands.";
    else if (o.reason == "hq_lost")
        why = "The British held " + cmp_town(s, R.patriot_hq) + ", the army's headquarters, for " +
              std::to_string(R.hq_held_days) + " days.";
    else if (o.reason == "support_collapsed")
        why = "Support stayed under " + std::to_string(R.support_floor) + " for " + std::to_string(R.support_floor_days) +
              " days: the country gave up the cause.";
    else if (o.reason == "no_field_army") why = "No Patriot army is left in the field.";
    else if (o.reason == "vassalage") why = "You took the Crown's peace and its title: the rebellion is over.";
    else why = "The campaign window closed with neither side beaten.";
    o.summary.insert(o.summary.begin(), why);
    o.summary.push_back("Ended " + cmp_date_words(now) + ", day " + std::to_string(days + 1) + " of the campaign");
    o.summary.push_back("Towns held: Patriots " + std::to_string(score.patriot_towns) + ", British " + std::to_string(score.british_towns));
    o.summary.push_back("Support " + std::to_string(s.local_support));
    o.summary.push_back("Battles fought " + std::to_string(s.progress.battles_fought) + ": Patriots won " +
                        std::to_string(detail::map_get(s.progress.battles_won, std::string(kContinental))) + ", British won " +
                        std::to_string(detail::map_get(s.progress.battles_won, std::string(kBritish))));
    int done = 0, total = 0;
    for (const MissionDef& m : g_missions) {
        if (m.faction != s.player_faction) continue;
        ++total;
        if (detail::map_get(s.progress.missions, m.id).status == "complete") ++done;
    }
    if (total > 0) o.summary.push_back("Missions done " + std::to_string(done) + " of " + std::to_string(total));
    return o;
}

void tick_campaign(CampaignState& s, std::vector<Interrupt>& interrupts) {
    if (!g_campaign_rules_loaded || s.progress.outcome.decided) return;
    cmp_tick_projects(s);
    cmp_tick_occupation(s);
    cmp_tick_enlistments(s);
    cmp_tick_missions(s);
    cmp_tick_evacuation(s);
    cmp_tick_outcome(s, interrupts);
}

// ---- HUD snapshots ---------------------------------------------------------

std::string missions_json(const CampaignState& s) {
    using nlohmann::json;
    json out = json::object();
    json list = json::array();
    std::string current;
    for (const MissionDef& m : g_missions) {
        const MissionState st = detail::map_get(s.progress.missions, m.id);
        const std::string status = st.status.empty() ? "locked" : st.status;
        json row{{"id", m.id},
                 {"faction", m.faction},
                 {"title", m.title},
                 {"flavor", m.flavor},
                 {"goal", m.goal},
                 {"status", status},
                 {"target_provinces", m.target_provinces},
                 {"deadline_hour", st.deadline_hour},
                 {"hours_left", st.deadline_hour > 0 ? std::max<int64_t>(0, st.deadline_hour - s.now.hour_index) : int64_t{-1}},
                 {"progress", status == "active" ? mission_progress_text(s, m.faction, m.complete) : std::string()},
                 {"reward", m.reward.summary},
                 {"failure", m.failure.summary}};
        if (m.has_available_from) row["available_from"] = cmp_date_words(m.available_from);
        if (m.faction == s.player_faction && status == "active" && current.empty()) current = m.id;
        list.push_back(row);
    }
    out["missions"] = list;
    out["current"] = current;
    const CampaignRules& R = campaign_rules();
    out["evacuation_hours_remaining"] = s.progress.evacuation_hours_remaining;
    const CampaignProject& k = s.progress.knox;
    bool can_send = false;
    for (const MissionDef& m : g_missions)
        if (m.faction == s.player_faction && m.complete.type == "project_status" &&
            detail::map_get(s.progress.missions, m.id).status == "active" && k.status != "underway" && k.status != "delivered")
            can_send = true;
    out["siege_guns"] = {{"status", k.status},
                         {"province", k.province},
                         {"army_id", k.army_id},
                         {"ready_hour", k.ready_hour},
                         {"can_send_for", can_send},
                         {"gold_cost", R.siege_train_gold}};
    int owed = 0;
    for (const auto& ak : s.armies) {
        if (ak.second.faction != kContinental || ak.second.faction != s.player_faction) continue;
        for (const auto& u : ak.second.units)
            if (!detail::contains(R.enlist_exempt, u.first)) owed += reenlist_cost(s, ak.first, u.first);
    }
    out["enlistment"] = {{"warned", s.progress.enlistment_warned},
                         {"expired", s.progress.enlistments_expired},
                         {"expiry", cmp_date_words(R.enlist_expiry)},
                         {"hours_left", std::max<int64_t>(0, cmp_hour(R.enlist_expiry) - s.now.hour_index)},
                         {"gold_to_keep_everyone", owed}};
    out["hq_lost_days"] = s.progress.hq_lost_hours / 24;
    out["hq_held_days_to_lose"] = R.hq_held_days;
    out["support_low_days"] = s.progress.support_low_hours / 24;
    return out.dump();
}

std::string outcome_json(const CampaignState& s) {
    const CampaignOutcome& o = s.progress.outcome;
    return nlohmann::json{{"decided", o.decided},
                          {"winner", o.winner},
                          {"reason", o.reason},
                          {"hour_index", o.hour_index},
                          {"player_won", o.decided && o.winner == s.player_faction},
                          {"summary", o.summary}}
        .dump();
}

// ---- The scored AI -----------------------------------------------------------

std::string choose_ai_target(const CampaignState& s, const std::string& army_id) {
    auto ait = s.armies.find(army_id);
    if (ait == s.armies.end()) return "";
    const Army& army = ait->second;
    const CampaignRules& R = campaign_rules();
    const std::string enemy = detail::other_faction(army.faction);
    std::set<std::string> wanted;  // towns the side's open missions name
    for (const MissionDef& m : g_missions) {
        if (m.faction != army.faction || detail::map_get(s.progress.missions, m.id).status != "active") continue;
        for (const auto& id : m.target_provinces) wanted.insert(id);
    }
    std::string best;
    long best_score = -1;
    for (const auto& pk : s.provinces) {  // std::map: town id order, so ties go to the lower id
        const Province& p = pk.second;
        if (p.owner == army.faction) continue;
        int hours = 0;
        if (pk.first != army.province) {
            const PathPlan plan = plan_path(s, army_id, pk.first);
            if (!plan.ok) continue;
            hours = plan.total_march_hours;
        }
        const ProvinceStores& st = province_ledger(s, pk.first);
        long value = static_cast<long>(std::max(0, st.cartridges)) * R.v_cartridge + static_cast<long>(std::max(0, st.powder)) * R.v_powder +
                     static_cast<long>(std::max(0, st.muskets)) * R.v_musket + static_cast<long>(std::max(0, st.provisions)) * R.v_provision;
        if (pk.first == R.patriot_hq) value += R.v_hq;
        if (pk.first == R.heights) {
            const bool guns = s.progress.knox.status == "delivered" && s.progress.knox.province == pk.first;
            value += (p.fort_level > 0 || guns) ? R.v_heights_fortified : R.v_heights;
        }
        const int defenders = cmp_units_in(s, pk.first, enemy);
        if (defenders <= 0) value += R.v_weak;
        if (detail::contains(p.terrain, "farmland")) value += R.v_forage;
        if (wanted.count(pk.first)) value += R.v_mission;
        const long score = value * 1000L / (static_cast<long>(std::max(1, hours)) * (1 + defenders));
        if (score > best_score) {
            best_score = score;
            best = pk.first;
        }
    }
    return best;
}

void campaign_daily_ai(CampaignState& s) {
    const CampaignRules& R = campaign_rules();
    const std::string ai = enemy_faction(s);
    const std::string base = ai == kBritish ? R.british_base : R.patriot_hq;
    auto base_it = s.provinces.find(base);
    const bool holds_base = base_it != s.provinces.end() && base_it->second.owner == ai;
    auto order = [&s](const std::string& army_id, const std::string& dest) {
        const PathPlan plan = plan_path(s, army_id, dest);
        if (!plan.ok || plan.edges.empty()) return;
        CommandPayload p;
        p.kind = CommandKind::MoveArmy;
        p.army_id = army_id;
        p.dest_province = dest;
        p.edge_path = plan.edges;
        s = enqueue_command(s, p);
    };
    // The garrison: never ordered out of the base. Without one, the first army standing there
    // becomes it; grown past its ceiling, the surplus marches out as a new field army.
    if (holds_base) {
        std::string garrison;
        for (const auto& ak : s.armies)
            if (ak.second.faction == ai && ak.second.role == "garrison" && ak.second.province == base && garrison.empty())
                garrison = ak.first;
        if (garrison.empty()) {
            for (auto& ak : s.armies) {
                if (ak.second.faction != ai || ak.second.province != base || !ak.second.path.empty()) continue;
                ak.second.role = "garrison";
                garrison = ak.first;
                detail::add_log(s, ak.first + " takes up the garrison of " + base_it->second.name);
                break;
            }
        }
        if (!garrison.empty() && detail::total_units(s.armies.at(garrison)) > R.ai_garrison_max) {
            Army& g = s.armies.at(garrison);
            int surplus = detail::total_units(g) - R.ai_garrison_units;
            Army field;
            field.id = ai + "_field_" + std::to_string(s.next_command_seq++);
            field.name = ai == kBritish ? "British Column" : "Continental Column";
            field.faction = ai;
            field.province = base;
            field.previous_province = base;
            field.morale = g.morale;
            field.readiness = g.readiness;
            field.rounds_per_man = g.rounds_per_man;
            // Largest class first, so the column is the garrison's line rather than its officers.
            while (surplus > 0) {
                std::string biggest;
                for (const auto& u : g.units) {
                    if (u.first == battle_rules().guns_crew_class) continue;  // gunners go only with their guns (below)
                    if (u.second > 0 && (biggest.empty() || u.second > g.units.at(biggest))) biggest = u.first;
                }
                if (biggest.empty()) break;
                --g.units[biggest];
                ++field.units[biggest];
                --surplus;
            }
            if (detail::total_units(field) >= R.ai_min_field) {
                // The column takes its share of the garrison's guns, and the crews that serve them
                // (on top of the surplus: gunners go where their guns go).
                const BattleRules& BR = battle_rules();
                const int share = std::max(0, g.guns) * R.ai_detachment_gun_share_pct / 100;
                const int crews = std::min(share * BR.guns_crew_units_per_gun, std::max(0, detail::map_get(g.units, BR.guns_crew_class)));
                const int guns_out = std::min(share, crews / std::max(1, BR.guns_crew_units_per_gun));
                if (guns_out > 0) {
                    g.guns -= guns_out;
                    field.guns += guns_out;
                    g.units[BR.guns_crew_class] -= guns_out * BR.guns_crew_units_per_gun;
                    field.units[BR.guns_crew_class] += guns_out * BR.guns_crew_units_per_gun;
                }
                detail::add_log(s, field.id + " marches out of " + base_it->second.name + " with " +
                                       std::to_string(detail::total_units(field)) + " units and " +
                                       std::to_string(field.guns) + " guns");
                s.armies[field.id] = field;
            } else {
                for (const auto& u : field.units) g.units[u.first] += u.second;  // too few to send: they stay
            }
        }
    }
    if (detail::has_ceasefire(s)) return;  // nobody marches on the enemy under a truce
    bool objective_set = false;
    std::vector<std::string> ids;
    for (const auto& ak : s.armies)
        if (ak.second.faction == ai && ak.second.role != "garrison") ids.push_back(ak.first);
    for (const std::string& id : ids) {
        Army& a = s.armies.at(id);
        if (!a.path.empty()) continue;
        auto here = s.provinces.find(a.province);
        if (here == s.provinces.end()) continue;
        if (here->second.siege_by == a.faction) continue;  // it stays at its own siege
        if (here->second.owner == a.faction && a.morale < R.ai_rest_cap)
            a.morale = std::min(R.ai_rest_cap, a.morale + R.ai_rest_morale);
        if (a.morale < R.ai_retreat_morale || detail::total_units(a) < R.ai_min_field) {
            if (holds_base && a.province != base) order(id, base);
            continue;
        }
        const std::string target = choose_ai_target(s, id);
        if (target.empty()) continue;
        if (!objective_set) {
            s.ai_objective = target;
            objective_set = true;
        }
        if (target != s.armies.at(id).province) order(id, target);
    }
}

// ---- Sim-only battle resolver -----------------------------------------------

BattleResult auto_resolve_battle(const CampaignState& state, const BattleSetup& setup) {
    const CampaignRules& R = campaign_rules();
    const BattleRules& B = battle_rules();
    BattleResult r;
    r.province_id = setup.province_id;
    auto power = [&](const std::string& faction) -> long {
        auto it = setup.sides.find(faction);
        if (it == setup.sides.end()) return 0;
        const SideSetup& side = it->second;
        // Army against army: the whole strength (the first `men` row), not only the men fielded.
        long men = 0;
        for (const auto& f : side.factors)
            if (f.label == "men") {
                men = f.value;
                break;
            }
        const long rounds = std::min(side.rounds_per_man, B.rounds_max);
        long p = men * (50 + side.morale) * (60 + 40 * rounds / std::max(1, B.rounds_max)) / 100 *
                 (100 + static_cast<long>(R.ar_fort_pct) * side.fortification_level) / 100;
        const int span = 2 * R.ar_luck_pct + 1;
        const int luck = static_cast<int>(detail::mix64(setup.seed ^ detail::fnv1a(faction)) % static_cast<uint64_t>(span)) - R.ar_luck_pct;
        return std::max(0L, p * (100 + luck) / 100);
    };
    const long attacker = power(setup.attacker_faction);
    const long defender = power(setup.defender_faction);
    r.winner = attacker > defender ? setup.attacker_faction : setup.defender_faction;  // a tie holds the ground
    const std::string loser = detail::other_faction(r.winner);
    const long win = std::max(attacker, defender), lose = std::max(1L, std::min(attacker, defender));
    r.end_reason = win * 100 / lose >= R.ar_broke_pct ? "army_broke"
                                                      : (r.winner == setup.attacker_faction ? "posts_swept" : "time_limit");
    r.objective_holder = r.winner;
    r.objectives_held["green"] = r.winner;
    auto take = [&](const std::string& faction, int pct) {
        auto sit = setup.sides.find(faction);
        if (sit == setup.sides.end()) return;
        auto ait = state.armies.find(sit->second.army_id);
        if (ait == state.armies.end()) return;
        const int total = std::max(0, detail::total_units(ait->second));
        int owed = std::max(total > 0 ? 1 : 0, total * pct / 100);
        // Class order, each its share; what rounding leaves over comes off the largest class.
        std::map<std::string, int> lost;
        for (const auto& u : ait->second.units) {
            const int n = std::min(std::max(0, u.second) * pct / 100, owed);
            if (n > 0) lost[u.first] = n;
            owed -= n;
        }
        while (owed > 0) {
            std::string biggest;
            int most = 0;
            for (const auto& u : ait->second.units) {
                const int left = std::max(0, u.second) - detail::map_get(lost, u.first);
                if (left > most) {
                    most = left;
                    biggest = u.first;
                }
            }
            if (biggest.empty()) break;
            ++lost[biggest];
            --owed;
        }
        for (const auto& kv : lost) {
            r.losses[faction][kv.first] = kv.second;
            if (kv.second / 2 > 0) r.wounded[faction][kv.first] = kv.second / 2;
        }
        long men = 0;
        for (const auto& f : sit->second.factors)
            if (f.label == "men") {
                men = f.value;
                break;
            }
        r.shots_fired[faction] = static_cast<int>(men * std::min(sit->second.rounds_per_man, B.rounds_max) / 2);
    };
    take(loser, R.ar_loser_pct);
    take(r.winner, R.ar_winner_pct);
    auto lit = setup.sides.find(loser);
    if (lit != setup.sides.end()) r.retreat_province = lit->second.retreat_province;
    return r;
}

CampaignState run_idle_campaign(const CampaignState& state, int max_hours) {
    CampaignState s = state;
    for (int i = 0; i < max_hours && !s.progress.outcome.decided; ++i) {
        s = advance_hour(s).state;
        if (!s.pending_battle.active) continue;
        const BattleSetup setup = build_battle_setup(s, s.pending_battle.province);
        const CampaignState after = apply_battle_result(s, auto_resolve_battle(s, setup));
        // A result the sim refuses would leave the battle pending for ever: drop it instead.
        s = after.pending_battle.active ? s : after;
        if (s.pending_battle.active) s.pending_battle = PendingBattle{};
    }
    return s;
}

}  // namespace pa250
