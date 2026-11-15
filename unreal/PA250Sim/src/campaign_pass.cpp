// Campaign pass: people (generals, agents, organizers, delegates, envoys), lobbying (bodies,
// measures, dispatches, favors, Unity, the ministry's confidence) and decisions. Plain C++; state
// changes only through the hourly tick, orders (CommandKind::CampaignOrder) and battle results.
// GAMEPLAY TUNING lives in data/persons_boston_1775.json, bodies_boston_1775.json,
// measures_boston_1775.json and decisions_boston_1775.json. Each part runs only while its file is
// loaded, so a campaign without them is the campaign as it was.
#include "internal.hpp"
#include "nlohmann/json.hpp"

namespace pa250 {
namespace {

using cp_json = nlohmann::json;

// ---- Small helpers ---------------------------------------------------------

YMDH cp_ymdh(const cp_json& j) {
    YMDH v{1775, 1, 1, 0};
    if (!j.is_object()) return v;
    v.y = j.value("y", 1775);
    v.m = static_cast<uint8_t>(j.value("m", 1));
    v.d = static_cast<uint8_t>(j.value("d", 1));
    v.h = static_cast<uint8_t>(j.value("h", 0));
    return v;
}
int64_t cp_hour(YMDH v) { return from_ymdh(v).hour_index; }

std::string cp_date_words(int64_t hour_index) {
    static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
    const YMDH v = to_ymdh(GameTime{hour_index});
    const int m = std::max(1, std::min(12, static_cast<int>(v.m)));
    return std::to_string(static_cast<int>(v.d)) + " " + months[m - 1] + " " + std::to_string(v.y);
}

std::string cp_town(const CampaignState& s, const std::string& id) {
    auto it = s.provinces.find(id);
    return it == s.provinces.end() ? id : it->second.name;
}

void cp_note(CampaignState& s, const std::string& reason, int gold = 0, int support = 0,
             const std::string& province = std::string()) {
    ResourceChange ch;
    ch.reason = reason;
    ch.gold = gold;
    ch.local_support = support;
    ch.province_id = province;
    detail::record_resource_change(s, ch);
}

bool cp_known(const std::string& v, std::initializer_list<const char*> set) {
    for (const char* k : set)
        if (v == k) return true;
    return false;
}

bool cp_has_flag(const CampaignState& s, const std::string& flag) { return detail::contains(s.pass.flags, flag); }

// ---- Effects (closed set, shared by measures and decisions) -------------------

struct CpUnits {
    std::string province, unit_class, own_army, army_name;
    int count = 0;
    bool allied = false;
};
struct CpFactor {
    std::string province, label, text, faction;  // faction: "" / "own" = the acting side, "enemy" = the other
    int value = 0, days = 0;
    bool one_battle = false;
};
struct CpEffects {
    int gold = 0, favors = 0, local_support = 0, british_attitude = 0, army_morale = 0, army_readiness = 0;
    int clerk_load = 0, ministry_confidence = 0, upkeep_share_pct = 0, price_rise_pct = 0;
    int reenlist_share_pct = 0, detach_units = 0;
    std::map<std::string, int> bloc_shift;
    std::string stores_province;
    int powder = 0, cartridges = 0, muskets = 0, provisions = 0, guns = 0;
    std::string monthly_province;
    int m_powder = 0, m_muskets = 0, m_provisions = 0;
    std::vector<CpUnits> units;
    std::vector<std::string> persons;
    std::vector<CpFactor> factors;
    std::string fort_province;
    int fort_levels = 0;
    std::string lure_province;
    int lure_days = 0;
    bool start_evacuation = false;
    std::string set_flag;
    std::string summary;
    // Something beyond words: a number moves, a unit comes or goes, a factor is set.
    bool any() const {
        return gold || favors || local_support || british_attitude || army_morale || army_readiness || clerk_load ||
               ministry_confidence || upkeep_share_pct || price_rise_pct || reenlist_share_pct || detach_units ||
               !bloc_shift.empty() || !stores_province.empty() || !monthly_province.empty() || !units.empty() ||
               !persons.empty() || !factors.empty() || fort_levels || lure_days || start_evacuation || !set_flag.empty();
    }
};

bool cp_parse_effects(const cp_json& j, CpEffects& out, std::vector<std::string>& errors, const std::string& where) {
    out = CpEffects{};
    if (!j.is_object()) {
        errors.push_back(detail::err("malformed_effects", where));
        return false;
    }
    try {
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string& k = it.key();
            const cp_json& v = it.value();
            if (k == "gold") out.gold = v.get<int>();
            else if (k == "favors") out.favors = v.get<int>();
            else if (k == "local_support") out.local_support = v.get<int>();
            else if (k == "british_attitude") out.british_attitude = v.get<int>();
            else if (k == "army_morale") out.army_morale = v.get<int>();
            else if (k == "army_readiness") out.army_readiness = v.get<int>();
            else if (k == "clerk_load") out.clerk_load = v.get<int>();
            else if (k == "ministry_confidence") out.ministry_confidence = v.get<int>();
            else if (k == "upkeep_share_pct") out.upkeep_share_pct = v.get<int>();
            else if (k == "price_rise_pct") out.price_rise_pct = v.get<int>();
            else if (k == "reenlist_share_pct") out.reenlist_share_pct = v.get<int>();
            else if (k == "detach_units") out.detach_units = v.get<int>();
            else if (k == "start_evacuation") out.start_evacuation = v.get<bool>();
            else if (k == "set_flag") out.set_flag = v.get<std::string>();
            else if (k == "summary") out.summary = v.get<std::string>();
            else if (k == "bloc_shift") out.bloc_shift = v.get<std::map<std::string, int>>();
            else if (k == "persons") out.persons = v.get<std::vector<std::string>>();
            else if (k == "stores") {
                out.stores_province = v.at("province").get<std::string>();
                out.powder = v.value("powder", 0);
                out.cartridges = v.value("cartridges", 0);
                out.muskets = v.value("muskets", 0);
                out.provisions = v.value("provisions", 0);
                out.guns = v.value("guns", 0);
            } else if (k == "monthly") {
                out.monthly_province = v.at("province").get<std::string>();
                out.m_powder = v.value("powder", 0);
                out.m_muskets = v.value("muskets", 0);
                out.m_provisions = v.value("provisions", 0);
            } else if (k == "units") {
                for (const auto& u : v) {
                    CpUnits cu;
                    cu.province = u.at("province").get<std::string>();
                    cu.unit_class = u.at("class").get<std::string>();
                    cu.count = u.at("count").get<int>();
                    cu.allied = u.value("allied", false);
                    cu.own_army = u.value("own_army", "");
                    cu.army_name = u.value("army_name", "");
                    out.units.push_back(cu);
                }
            } else if (k == "battle_factors") {
                for (const auto& f : v) {
                    CpFactor cf;
                    cf.province = f.at("province").get<std::string>();
                    cf.label = f.at("label").get<std::string>();
                    cf.value = f.at("value").get<int>();
                    cf.days = f.value("days", 0);
                    cf.one_battle = f.value("one_battle", false);
                    cf.faction = f.value("faction", "own");
                    cf.text = f.value("text", "");
                    if (!cp_known(cf.label, {"morale", "readiness", "fortification", "reinforcement"}))
                        errors.push_back(detail::err("unknown_factor_label", where + " " + cf.label));
                    if (cf.faction != "own" && cf.faction != "enemy")
                        errors.push_back(detail::err("unknown_factor_side", where + " " + cf.faction));
                    out.factors.push_back(cf);
                }
            } else if (k == "fort") {
                out.fort_province = v.at("province").get<std::string>();
                out.fort_levels = v.at("levels").get<int>();
            } else if (k == "lure") {
                out.lure_province = v.at("province").get<std::string>();
                out.lure_days = v.at("days").get<int>();
            } else {
                errors.push_back(detail::err("unknown_effect", where + " " + k));
                return false;
            }
        }
    } catch (const std::exception& e) {
        errors.push_back(detail::err("malformed_effects", where + ": " + e.what()));
        return false;
    }
    return true;
}

// ---- Data: persons -----------------------------------------------------------

struct CpTraitEffect {
    std::string when, label;
    int value = 0;
};
struct CpTrait {
    std::string label, text;
    std::vector<CpTraitEffect> effects;
};
struct CpAbility {
    std::string label, where, text;
    int morale = 0, days = 0, fortification = 0, local_support = 0;
    bool muster_at_once = false;
};
struct CpPerson {
    std::string id, name, rank, side, requires_measure, ability, note, basis, starts_role, starts_army, informer_for;
    std::vector<std::string> roles, traits;
    bool has_from = false, cic = false, has_exposed = false;
    YMDH from{}, exposed{};
    int uses = 0;
    // His own hero kit numbers (0 = the shared kit's).
    float hero_health_scale = 0.f, hero_aura_radius_m = 0.f, hero_rally_radius_m = 0.f;
};
struct CpPersonRules {
    int detachment_unit_cap = 12, army_unit_cap = 40;
    std::vector<std::string> muster_buildings = {"militia_muster", "fort"};
    int organizer_support = 1, organizer_muster_pct = 75;
    int delegate_shift_per_week = 3;
    int delegate_seat_shift = 3;      // applied once when seated (GAME INVENTION)
    int delegate_reseat_hours = 168;  // before relieve is allowed (GAME INVENTION, 7 days)
    int envoy_attitude = 5;
    float aura_radius_m = 30.f, home_post_radius_m = 25.f, fall_pressure = 0.6f, fall_pressure_seconds = 10.f;
    int fate_wounded = 60, fate_killed = 25, fate_captured = 15;
    int wounded_days = 21, army_morale_on_fall = -8, army_morale_on_fall_victory = 0;
    int step_men = 20, width_pct = 30;
    int exposed_support = 2;
};
CpPersonRules g_cp_prules;
HeroRules g_cp_hero;
std::vector<CpPerson> g_cp_persons;
std::map<std::string, CpTrait> g_cp_traits;
std::map<std::string, CpAbility> g_cp_abilities;
bool g_cp_persons_loaded = false;

const CpPerson* cp_person(const std::string& id) {
    for (const CpPerson& p : g_cp_persons)
        if (p.id == id) return &p;
    return nullptr;
}

PersonState cp_pstate(const CampaignState& s, const std::string& id) { return detail::map_get(s.pass.persons, id); }

// At hand to be given a role (or to act as an agent): arrived, not fallen, not away.
bool cp_person_present(const CampaignState& s, const CpPerson& p) {
    const PersonState st = cp_pstate(s, p.id);
    if (!st.status.empty()) return false;
    if (p.has_from && s.now.hour_index < cp_hour(p.from)) return false;
    if (!p.requires_measure.empty() && !st.unlocked) return false;
    return true;
}

int cp_uses_left(const CampaignState& s, const CpPerson& p) {
    const PersonState st = cp_pstate(s, p.id);
    return st.uses_left < 0 ? p.uses : st.uses_left;
}

bool cp_town_has_muster(const Province& p) {
    for (const std::string& b : g_cp_prules.muster_buildings) {
        if (b == "militia_muster" && p.militia_muster) return true;
        if (b == "fort" && p.fort_level > 0) return true;
        if (b == "church" && p.church) return true;
    }
    return false;
}

std::string cp_free_general(const CampaignState& s, const std::string& faction) {
    for (const CpPerson& p : g_cp_persons) {
        if (p.side != faction || !detail::contains(p.roles, "general")) continue;
        if (!cp_person_present(s, p) || !cp_pstate(s, p.id).role.empty()) continue;
        return p.id;
    }
    return "";
}

// An informer of `viewer` still holding a role on the other side: everything is known to `viewer`.
bool cp_informer_serves(const CampaignState& s, const std::string& viewer) {
    for (const CpPerson& p : g_cp_persons) {
        if (p.informer_for != viewer) continue;
        const PersonState st = cp_pstate(s, p.id);
        if (st.status.empty() && !st.role.empty()) return true;
    }
    return false;
}

bool cp_organizer_in(const CampaignState& s, const std::string& faction, const std::string& province) {
    for (const CpPerson& p : g_cp_persons) {
        if (p.side != faction) continue;
        const PersonState st = cp_pstate(s, p.id);
        if (st.status.empty() && st.role == "organizer" && st.province == province) return true;
    }
    return false;
}

// What `viewer` makes of an army: exact, or a rough count in men.
struct CpIntel {
    bool known = true;
    int men = 0, men_low = 0, men_high = 0;
    std::string source;
};
CpIntel cp_intel(const CampaignState& s, const std::string& viewer, const Army& army) {
    CpIntel out;
    const int spu = std::max(1, battle_rules().soldiers_per_unit);
    out.men = std::max(0, detail::total_units(army)) * spu;
    out.men_low = out.men_high = out.men;
    if (!g_cp_persons_loaded || army.faction == viewer) return out;
    auto sc = s.pass.scouted_until.find(viewer + ":" + army.id);
    if (sc != s.pass.scouted_until.end() && sc->second > s.now.hour_index) {
        out.source = "Scouted";
        return out;
    }
    if (cp_organizer_in(s, viewer, army.province)) {
        out.source = "Your organizer in " + cp_town(s, army.province) + " counts them";
        return out;
    }
    if (cp_informer_serves(s, viewer)) {
        out.source = "An informer in their councils";
        return out;
    }
    // Unscouted: a band centred on the true count (never shifted past it). Width comes from data;
    // a seed may only widen the band further. width_pct 0 adds nothing.
    const int step = std::max(1, g_cp_prules.step_men);
    const int width = std::max(0, g_cp_prules.width_pct);
    const int truth = out.men;
    int extra = 0;
    if (width > 0) {
        const int half0 = std::max(step, truth * width / 100);
        const uint64_t h = detail::mix64(s.seed ^ detail::fnv1a(army.id) ^ detail::mix64(static_cast<uint64_t>(s.now.hour_index / 24)));
        extra = static_cast<int>(h % static_cast<uint64_t>(half0 + 1));
    }
    const MenBand band = unscouted_men_band(truth, step, width, extra);
    out.known = false;
    out.men_low = band.low;
    out.men_high = band.high;
    out.men = band.mid;
    out.source = "Rough estimate: nobody has counted them";
    return out;
}

// ---- Data: politics ------------------------------------------------------------

struct CpBloc {
    std::string id, name, moves;
    int lean = 0, on_victory = 0, on_defeat = 0;
};
struct CpBody {
    std::string id, side, name, button, note, basis;
    bool has_from = false, has_until = false, dispatch = false;
    YMDH from{}, until{};
    std::vector<CpBloc> blocs;
};
struct CpMeasure {
    std::string id, body, title, text, note, basis;
    bool has_earliest = false, has_vote = false, has_arrives = false, has_ai_send = false, repeatable = false;
    YMDH earliest{}, vote{}, arrives{}, ai_send{};
    std::map<std::string, int> bias;
    CpEffects passed, failed, later;
    std::string passed_json, failed_json, later_json;
    int later_days = 0, retry_days = 0;
};
struct CpLobbyRules {
    int write_shift = 5, favor_shift = 15, vote_base = 50, vote_min = 5, vote_max = 95, blocs_to_pass = 2;
    int favors_at_start = 1, favors_per_mission = 1, favors_per_victory = 1;
    int unity_floor = 40, desertion_per_day = 1;
    int days_out = 38, days_back = 38;
    int confidence_start = 60, on_defeat = -8, on_victory = 4, recall_floor = 30, confidence_after_recall = 55;
    std::vector<std::string> replacements;
};
CpLobbyRules g_cp_lobby;
std::vector<CpBody> g_cp_bodies;
std::vector<CpMeasure> g_cp_measures;
bool g_cp_politics_loaded = false;

const CpBody* cp_body(const std::string& id) {
    for (const CpBody& b : g_cp_bodies)
        if (b.id == id) return &b;
    return nullptr;
}
const CpMeasure* cp_measure(const std::string& id) {
    for (const CpMeasure& m : g_cp_measures)
        if (m.id == id) return &m;
    return nullptr;
}
// The body a side answers to at this hour.
const CpBody* cp_active_body(const CampaignState& s, const std::string& side) {
    for (const CpBody& b : g_cp_bodies) {
        if (b.side != side) continue;
        if (b.has_from && s.now.hour_index < cp_hour(b.from)) continue;
        if (b.has_until && s.now.hour_index >= cp_hour(b.until)) continue;
        return &b;
    }
    return nullptr;
}
const CpBloc* cp_bloc(const CpBody& body, const std::string& id) {
    for (const CpBloc& b : body.blocs)
        if (b.id == id) return &b;
    return nullptr;
}
int cp_lean(const CampaignState& s, const CpBloc& bloc) {
    auto it = s.pass.bloc_lean.find(bloc.id);
    return it == s.pass.bloc_lean.end() ? bloc.lean : it->second;
}
void cp_shift_bloc(CampaignState& s, const std::string& bloc_id, int by) {
    for (const CpBody& body : g_cp_bodies)
        for (const CpBloc& b : body.blocs)
            if (b.id == bloc_id) s.pass.bloc_lean[bloc_id] = detail::clamp_int(cp_lean(s, b) + by, -100, 100);
}
int cp_yes_chance(const CampaignState& s, const CpMeasure& m, const CpBloc& bloc) {
    return detail::clamp_int(g_cp_lobby.vote_base + cp_lean(s, bloc) + detail::map_get(m.bias, bloc.id), g_cp_lobby.vote_min,
                             g_cp_lobby.vote_max);
}
// The vote, from the campaign seed: the same state always votes the same way.
int cp_count_votes(const CampaignState& s, const CpMeasure& m, const CpBody& body) {
    int yes = 0;
    const MeasureState st = detail::map_get(s.pass.measures, m.id);
    for (const CpBloc& bloc : body.blocs) {
        const uint64_t roll = detail::mix64(s.seed ^ detail::fnv1a(m.id) ^ detail::mix64(detail::fnv1a(bloc.id)) ^
                                            static_cast<uint64_t>(st.times) * 0x9E37ULL ^
                                            detail::mix64(static_cast<uint64_t>(s.now.hour_index / 24))) %
                              100ULL;
        if (static_cast<int>(roll) < cp_yes_chance(s, m, bloc)) ++yes;
    }
    return yes;
}

// ---- Data: decisions ------------------------------------------------------------

struct CpRequire {
    std::string type, flag, province, faction, role, decision, option, measure;
    int level = 0;
};
struct CpOption {
    std::string id, label, later_text, commit_role, later_json;
    CpEffects cost, gain, later;
    int later_days = 0, commit_days = 0;
};
struct CpDecision {
    std::string id, side, title, painting, text, ai_option, note, basis;
    YMDH from{};
    std::vector<CpRequire> requires_all;
    std::vector<CpOption> options;
};
std::vector<CpDecision> g_cp_decisions;
bool g_cp_decisions_loaded = false;

const CpDecision* cp_decision(const std::string& id) {
    for (const CpDecision& d : g_cp_decisions)
        if (d.id == id) return &d;
    return nullptr;
}

// ---- Applying effects -------------------------------------------------------------

std::string cp_hq_town(const CampaignState& s, const std::string& faction) { return default_depot_province(s, faction); }

void cp_add_units(CampaignState& s, const std::string& faction, const CpUnits& u) {
    if (u.count <= 0 || !s.provinces.count(u.province)) return;
    std::string target;
    if (!u.own_army.empty()) {
        target = u.own_army;
    } else {
        for (const auto& ak : s.armies) {
            const Army& a = ak.second;
            if (a.faction != faction || a.province != u.province || !a.path.empty() || a.role == "allied_company") continue;
            const int cap = detail::army_unit_cap(s, a);
            if (cap > 0 && detail::total_units(a) + u.count > cap) continue;
            target = ak.first;
            break;
        }
    }
    if (target.empty()) target = faction + "_levy_" + std::to_string(s.next_command_seq++);
    if (!s.armies.count(target)) {
        Army a;
        a.id = target;
        a.name = !u.army_name.empty() ? u.army_name : cp_town(s, u.province) + (faction == kBritish ? " Detachment" : " Levy");
        a.faction = faction;
        a.province = u.province;
        a.previous_province = u.province;
        a.morale = 65;
        a.readiness = 80;
        if (!u.own_army.empty()) a.role = "allied_company";
        s.armies[target] = a;
    }
    Army& a = s.armies.at(target);
    a.units[u.unit_class] += u.count;
    if (u.allied || !u.own_army.empty()) a.allied[u.unit_class] += u.count;
    if (s.progress.enlistment_warned && !s.progress.enlistments_expired) a.reenlisted[u.unit_class] += u.count;
}

void cp_apply_effects(CampaignState& s, const std::string& faction, const CpEffects& fx, const std::string& source) {
    const bool players = faction == s.player_faction;
    const std::string enemy = detail::other_faction(faction);
    int gold_delta = 0;
    if (fx.gold != 0) {
        if (players) {
            const int before = s.player_gold;
            s.player_gold = std::max(0, s.player_gold + fx.gold);
            gold_delta = s.player_gold - before;
        } else {
            s.progress.enemy_gold = std::max(0, s.progress.enemy_gold + fx.gold);
        }
    }
    if (players) s.pass.favors = std::max(0, s.pass.favors + fx.favors);
    const int support_before = s.local_support;
    s.local_support = detail::clamp_int(s.local_support + fx.local_support, 0, 100);
    if (fx.british_attitude != 0 && s.relations.count(kBritish))
        s.relations[kBritish] = detail::clamp_int(s.relations[kBritish] + fx.british_attitude, -100, 100);
    if (fx.army_morale != 0 || fx.army_readiness != 0)
        for (auto& ak : s.armies) {
            if (ak.second.faction != faction) continue;
            ak.second.morale = detail::clamp_int(ak.second.morale + fx.army_morale, 0, 100);
            ak.second.readiness = detail::clamp_int(ak.second.readiness + fx.army_readiness, 0, 100);
        }
    if (players) s.staff.letters_processed_today += std::max(0, fx.clerk_load);
    if (fx.ministry_confidence != 0)
        s.pass.ministry_confidence = detail::clamp_int(s.pass.ministry_confidence + fx.ministry_confidence, 0, 100);
    if (fx.upkeep_share_pct > 0)
        s.pass.upkeep_share_pct[faction] = detail::clamp_int(std::max(detail::map_get(s.pass.upkeep_share_pct, faction), fx.upkeep_share_pct), 0, 100);
    if (fx.price_rise_pct != 0) s.pass.price_rise_pct = std::max(0, s.pass.price_rise_pct + fx.price_rise_pct);
    for (const auto& bs : fx.bloc_shift) cp_shift_bloc(s, bs.first, bs.second);
    if (!fx.stores_province.empty() && s.provinces.count(fx.stores_province)) {
        ProvinceStores& st = province_ledger(s, fx.stores_province);
        st.powder = std::max(0, st.powder + fx.powder);
        st.cartridges = std::max(0, st.cartridges + fx.cartridges);
        st.muskets = std::max(0, st.muskets + fx.muskets);
        st.provisions = std::max(0, st.provisions + fx.provisions);
        st.guns = std::max(0, st.guns + fx.guns);
    }
    if (!fx.monthly_province.empty()) {
        MonthlyGrant g;
        g.source = source;
        g.faction = faction;
        g.province = fx.monthly_province;
        g.powder = fx.m_powder;
        g.muskets = fx.m_muskets;
        g.provisions = fx.m_provisions;
        s.pass.grants.push_back(g);
    }
    for (const CpUnits& u : fx.units) cp_add_units(s, faction, u);
    for (const std::string& pid : fx.persons) s.pass.persons[pid].unlocked = true;
    for (const CpFactor& f : fx.factors) {
        TownFactor tf;
        tf.province = f.province;
        tf.faction = f.faction == "enemy" ? enemy : faction;
        tf.label = f.label;
        tf.value = f.value;
        tf.source = source + (f.text.empty() ? "" : ": " + f.text);
        tf.until_hour = f.days > 0 ? s.now.hour_index + 24LL * f.days : 0;
        tf.one_battle = f.one_battle;
        s.pass.town_factors.push_back(tf);
    }
    if (fx.fort_levels != 0 && s.provinces.count(fx.fort_province)) {
        Province& p = s.provinces.at(fx.fort_province);
        p.fort_level = detail::clamp_int(p.fort_level + fx.fort_levels, 0, development_max_level());
    }
    if (fx.lure_days > 0 && s.provinces.count(fx.lure_province)) {
        AiLure l;
        l.province = fx.lure_province;
        l.faction = enemy_faction(s);
        l.until_hour = s.now.hour_index + 24LL * fx.lure_days;
        s.pass.lures.push_back(l);
    }
    if (fx.start_evacuation && campaign_rules_loaded() && !s.progress.evacuated && s.progress.evacuation_hours_remaining <= 0)
        s.progress.evacuation_hours_remaining = std::max(1, campaign_rules().evacuation_hours);
    if (!fx.set_flag.empty() && !cp_has_flag(s, fx.set_flag)) {
        s.pass.flags.push_back(fx.set_flag);
        std::sort(s.pass.flags.begin(), s.pass.flags.end());
    }
    if (fx.reenlist_share_pct > 0)
        for (auto& ak : s.armies) {
            if (ak.second.faction != faction) continue;
            for (const auto& u : ak.second.units) {
                const int keep = std::max(0, u.second) * std::min(100, fx.reenlist_share_pct) / 100;
                int& r = ak.second.reenlisted[u.first];
                r = std::max(r, keep);
            }
        }
    if (fx.detach_units > 0) {
        // The men go from the army at headquarters (else the side's largest), its largest classes first.
        std::string from;
        const std::string hq = cp_hq_town(s, faction);
        for (const auto& ak : s.armies) {
            if (ak.second.faction != faction || ak.second.role == "allied_company") continue;
            if (from.empty() || (ak.second.province == hq && s.armies.at(from).province != hq) ||
                ((ak.second.province == hq) == (s.armies.at(from).province == hq) &&
                 detail::total_units(ak.second) > detail::total_units(s.armies.at(from))))
                from = ak.first;
        }
        if (!from.empty()) {
            Army& a = s.armies.at(from);
            for (int left = fx.detach_units; left > 0 && detail::total_units(a) > 1; --left) {
                std::string biggest;
                for (const auto& u : a.units)
                    if (u.first != battle_rules().guns_crew_class && u.second > 0 && (biggest.empty() || u.second > a.units.at(biggest)))
                        biggest = u.first;
                if (biggest.empty()) break;
                --a.units[biggest];
            }
        }
    }
    const std::string line = (players ? std::string() : detail::faction_word(faction) + " news: ") + source +
                             (fx.summary.empty() ? "" : " - " + fx.summary);
    cp_note(s, line, gold_delta, s.local_support - support_before);
}

void cp_push_delayed(CampaignState& s, const std::string& faction, const std::string& source, const std::string& text,
                     int days, const std::string& effects_json, const std::string& condition = std::string()) {
    if (days <= 0 || effects_json.empty()) return;
    DelayedEffect d;
    d.due_hour = s.now.hour_index + 24LL * days;
    d.faction = faction;
    d.source = source;
    d.text = text;
    d.effects_json = effects_json;
    d.condition = condition;
    s.pass.delayed.push_back(d);
}

// ---- People: seeding and the hourly rules ----------------------------------------

void cp_release_person(CampaignState& s, const std::string& id) {
    PersonState& st = s.pass.persons[id];
    st.role.clear();
    st.army_id.clear();
    st.province.clear();
    st.bloc.clear();
    // seated_hour kept: the reseat cooldown runs from when he was last seated
}

void cp_seed(CampaignState& s) {
    if (s.pass.seeded) return;
    s.pass.seeded = true;
    if (g_cp_persons_loaded)
        for (const CpPerson& p : g_cp_persons) {
            if (p.starts_role.empty()) continue;
            if (p.starts_role == "general" && !s.armies.count(p.starts_army)) continue;
            PersonState& st = s.pass.persons[p.id];
            st.role = p.starts_role;
            st.army_id = p.starts_role == "general" ? p.starts_army : std::string();
        }
    if (g_cp_politics_loaded) {
        s.pass.favors = g_cp_lobby.favors_at_start;
        s.pass.ministry_confidence = g_cp_lobby.confidence_start;
        // What was ordered before the campaign opens is already at sea: it lands on its day.
        for (const CpMeasure& m : g_cp_measures) {
            if (!m.has_arrives) continue;
            MeasureState& ms = s.pass.measures[m.id];
            ms.status = "sent";
            ms.voted_yes = true;
            ms.yes = 3;
            ms.answer_hour = cp_hour(m.arrives);
            ms.vote_hour = ms.answer_hour - 24LL * g_cp_lobby.days_back;
        }
    }
}

void cp_tick_people(CampaignState& s) {
    if (!g_cp_persons_loaded) return;
    const YMDH now = to_ymdh(s.now);
    for (const CpPerson& p : g_cp_persons) {
        auto it = s.pass.persons.find(p.id);
        if (it == s.pass.persons.end()) continue;
        PersonState& st = it->second;
        // Back from a wound or an errand.
        if ((st.status == "wounded" || st.status == "away") && st.away_until_hour > 0 && s.now.hour_index >= st.away_until_hour) {
            cp_note(s, p.name + (st.status == "wounded" ? " has recovered from his wound and is at hand again" : " is back and at hand again"));
            st.status.clear();
            st.away_until_hour = 0;
        }
        // A general whose army is gone, an organizer whose town is lost: free again.
        if (st.role == "general" && (!s.armies.count(st.army_id) || detail::total_units(s.armies.at(st.army_id)) <= 0)) cp_release_person(s, p.id);
        if (st.role == "organizer") {
            auto pit = s.provinces.find(st.province);
            if (pit == s.provinces.end() || pit->second.owner != p.side) {
                cp_note(s, p.name + " leaves " + cp_town(s, st.province) + ": the town is lost");
                cp_release_person(s, p.id);
            }
        }
    }
    // The informer is found out on his day.
    for (const CpPerson& p : g_cp_persons) {
        if (!p.has_exposed || s.now.hour_index < cp_hour(p.exposed)) continue;
        PersonState& st = s.pass.persons[p.id];
        if (st.status == "exposed") continue;
        const bool served = st.status.empty() && !st.role.empty();
        cp_release_person(s, p.id);
        st.status = "exposed";
        if (p.side != s.player_faction) continue;
        CorrespondenceItem letter;
        letter.id = "exposed_" + p.id;
        letter.created_hour = s.now.hour_index;
        letter.recipient = "committee_of_safety";
        letter.kind = "historical_notice";
        letter.status = "delivered";
        letter.decision_id = "filed";
        letter.decision_applied = true;
        letter.fictionalized = true;
        letter.subject = "A ciphered letter: " + p.rank + " " + p.name;
        letter.body = "A letter in cipher, carried toward Boston, has been read. " + p.rank + " " + p.name +
                      " has been writing to the enemy. A council of war has examined him and he is under guard." +
                      (served ? " While he held a place among us the enemy knew our numbers." : "") + "\n\n" + p.note;
        letter.reason = "Filed. He can hold no place again.";
        s.staff.queue.push_back(letter);
        const int before = s.local_support;
        if (served) s.local_support = detail::clamp_int(s.local_support + g_cp_prules.exposed_support, 0, 100);
        cp_note(s, p.rank + " " + p.name + " is exposed as an informer and removed" +
                       (served ? ": the enemy no longer knows your numbers" : std::string()),
                0, s.local_support - before);
    }
    // Organizers: Support each month from every town one works in (the player's side).
    if (now.d == 1 && now.h == 0) {
        for (const CpPerson& p : g_cp_persons) {
            const PersonState st = cp_pstate(s, p.id);
            if (st.role != "organizer" || !st.status.empty() || g_cp_prules.organizer_support == 0) continue;
            const int before = s.local_support;
            const int sign = p.side == kContinental ? 1 : -1;  // Support is the Patriot cause's
            s.local_support = detail::clamp_int(s.local_support + sign * g_cp_prules.organizer_support, 0, 100);
            if (p.side == s.player_faction)
                cp_note(s, p.name + ", organizer at " + cp_town(s, st.province) + ": Support " + (sign > 0 ? "+" : "") +
                               std::to_string(s.local_support - before),
                        0, s.local_support - before, st.province);
        }
    }
    // The other side follows the same rules: a free general for an army standing at a muster, an
    // organizer for its base, a scout for the player's largest army.
    if (now.h == 0) {
        const std::string ai = enemy_faction(s);
        std::vector<std::string> ids;
        for (const auto& ak : s.armies)
            if (ak.second.faction == ai) ids.push_back(ak.first);
        for (const std::string& id : ids) {
            const Army& a = s.armies.at(id);
            if (!general_of(s, id).empty() || !a.path.empty() || a.role == "allied_company") continue;
            auto pit = s.provinces.find(a.province);
            if (pit == s.provinces.end() || pit->second.owner != ai || !cp_town_has_muster(pit->second)) continue;
            const std::string gen = cp_free_general(s, ai);
            if (gen.empty()) break;
            PersonState& st = s.pass.persons[gen];
            st.role = "general";
            st.army_id = id;
            detail::add_log(s, gen + " takes command of " + id);
        }
        const std::string base = cp_hq_town(s, ai);
        if (s.provinces.count(base) && s.provinces.at(base).owner == ai && !cp_organizer_in(s, ai, base))
            for (const CpPerson& p : g_cp_persons) {
                if (p.side != ai || !detail::contains(p.roles, "organizer") || !cp_person_present(s, p) || !cp_pstate(s, p.id).role.empty()) continue;
                PersonState& st = s.pass.persons[p.id];
                st.role = "organizer";
                st.province = base;
                break;
            }
        if (!cp_informer_serves(s, ai))
            for (const CpPerson& p : g_cp_persons) {
                if (p.side != ai || p.ability != "scout" || !cp_person_present(s, p) || cp_uses_left(s, p) <= 0) continue;
                std::string biggest;
                for (const auto& ak : s.armies) {
                    if (ak.second.faction == ai) continue;
                    auto sc = s.pass.scouted_until.find(ai + ":" + ak.first);
                    if (sc != s.pass.scouted_until.end() && sc->second > s.now.hour_index) continue;
                    if (biggest.empty() || detail::total_units(ak.second) > detail::total_units(s.armies.at(biggest))) biggest = ak.first;
                }
                if (biggest.empty()) break;
                s.pass.scouted_until[ai + ":" + biggest] = s.now.hour_index + 24LL * std::max(1, g_cp_abilities["scout"].days);
                s.pass.persons[p.id].uses_left = cp_uses_left(s, p) - 1;
                detail::add_log(s, p.id + " scouts " + biggest);
                break;
            }
    }
    // Spent factors, boats and scouting lapse.
    auto& tf = s.pass.town_factors;
    tf.erase(std::remove_if(tf.begin(), tf.end(), [&](const TownFactor& f) { return f.until_hour > 0 && s.now.hour_index >= f.until_hour; }), tf.end());
    auto& lu = s.pass.lures;
    lu.erase(std::remove_if(lu.begin(), lu.end(), [&](const AiLure& l) { return s.now.hour_index >= l.until_hour; }), lu.end());
    for (auto it = s.pass.scouted_until.begin(); it != s.pass.scouted_until.end();)
        it = it->second <= s.now.hour_index ? s.pass.scouted_until.erase(it) : std::next(it);
    for (auto it = s.pass.boats_until.begin(); it != s.pass.boats_until.end();)
        it = it->second <= s.now.hour_index ? s.pass.boats_until.erase(it) : std::next(it);
}

// ---- Politics: the hourly rules -----------------------------------------------------

void cp_settle_measure(CampaignState& s, const CpMeasure& m, const CpBody& body, bool passed) {
    MeasureState& ms = s.pass.measures[m.id];
    const std::string where = body.dispatch ? "Answer from London" : body.name;
    if (passed) {
        ms.status = m.repeatable ? "" : "passed";
        ++ms.times;
        ms.retry_hour = m.repeatable && m.retry_days > 0 ? s.now.hour_index + 24LL * m.retry_days : 0;
        ms.vote_hour = ms.answer_hour = 0;
        cp_apply_effects(s, body.side, m.passed, where + ": " + m.title + (body.dispatch ? " is granted" : " passes") + " (" +
                                                     std::to_string(ms.yes) + " of " + std::to_string(body.blocs.size()) + " for)");
        if (m.later_days > 0) cp_push_delayed(s, body.side, m.title, "", m.later_days, m.later_json);
    } else {
        ms.status = "failed";
        ms.retry_hour = m.retry_days > 0 ? s.now.hour_index + 24LL * m.retry_days : 0;
        ms.vote_hour = ms.answer_hour = 0;
        cp_apply_effects(s, body.side, m.failed, where + ": " + m.title + (body.dispatch ? " is refused" : " fails") + " (" +
                                                     std::to_string(ms.yes) + " of " + std::to_string(body.blocs.size()) + " for)");
    }
    s.memory.push_back({s.now.hour_index, body.side, m.title + (passed ? " passed" : " failed")});
}

void cp_vote(CampaignState& s, const CpMeasure& m, const CpBody& body) {
    MeasureState& ms = s.pass.measures[m.id];
    ms.yes = cp_count_votes(s, m, body);
    cp_settle_measure(s, m, body, ms.yes >= g_cp_lobby.blocs_to_pass);
}

void cp_send_dispatch(CampaignState& s, const CpMeasure& m) {
    MeasureState& ms = s.pass.measures[m.id];
    ms.status = "sent";
    ms.vote_hour = s.now.hour_index + 24LL * g_cp_lobby.days_out;
    ms.answer_hour = ms.vote_hour + 24LL * g_cp_lobby.days_back;
    ms.voted_yes = false;
    ms.yes = 0;
}

// When a congress measure is voted: its date, or the later date a failed one came back with.
int64_t cp_vote_hour(const CampaignState& s, const CpMeasure& m) {
    const MeasureState ms = detail::map_get(s.pass.measures, m.id);
    return ms.vote_hour > 0 ? ms.vote_hour : (m.has_vote ? cp_hour(m.vote) : 0);
}
bool cp_measure_open(const CampaignState& s, const CpMeasure& m) {
    const MeasureState ms = detail::map_get(s.pass.measures, m.id);
    if (!ms.status.empty()) return false;
    if (ms.retry_hour > 0 && s.now.hour_index < ms.retry_hour) return false;
    return !m.has_earliest || s.now.hour_index >= cp_hour(m.earliest);
}

void cp_recall_commander(CampaignState& s) {
    const CpPerson* cic = nullptr;
    for (const CpPerson& p : g_cp_persons)
        if (p.cic && p.side == kBritish && cp_pstate(s, p.id).status.empty()) cic = &p;
    if (!cic) return;
    const CpPerson* next = nullptr;
    for (const std::string& id : g_cp_lobby.replacements) {
        const CpPerson* p = cp_person(id);
        if (p && cp_person_present(s, *p)) {
            next = p;
            break;
        }
    }
    if (!next) return;  // nobody at Boston to take the command yet: he stays until someone is
    const std::string old_role = cp_pstate(s, cic->id).role;
    cp_release_person(s, cic->id);
    s.pass.persons[cic->id].status = "recalled";
    cp_release_person(s, next->id);
    s.pass.persons[next->id].role = old_role.empty() ? "envoy" : old_role;
    auto fit = s.factions.find(kBritish);
    if (fit != s.factions.end()) {
        fit->second.envoy_name = next->rank + " " + next->name;
        fit->second.envoy_title = "Commander in Chief at Boston";
        fit->second.grievance = 0;
    }
    s.pass.ministry_confidence = g_cp_lobby.confidence_after_recall;
    cp_note(s, "The ministers have lost patience: " + cic->rank + " " + cic->name + " is recalled and " + next->rank + " " + next->name +
                   " takes the command at Boston");
    s.memory.push_back({s.now.hour_index, kBritish, cic->name + " was recalled; " + next->name + " commands"});
}

void cp_tick_politics(CampaignState& s) {
    if (!g_cp_politics_loaded) return;
    const YMDH now = to_ymdh(s.now);
    const std::string ai = enemy_faction(s);
    for (const CpMeasure& m : g_cp_measures) {
        const CpBody* body = cp_body(m.body);
        if (!body) continue;
        MeasureState ms = detail::map_get(s.pass.measures, m.id);
        const bool sitting = cp_active_body(s, body->side) == body;
        if (body->dispatch) {
            if (ms.status == "sent") {
                if (!ms.voted_yes && ms.yes == 0 && ms.vote_hour > 0 && s.now.hour_index >= ms.vote_hour && s.now.hour_index < ms.answer_hour) {
                    // London votes when the dispatch arrives; Boston learns of it when the answer does.
                    MeasureState& live = s.pass.measures[m.id];
                    live.yes = cp_count_votes(s, m, *body);
                    live.voted_yes = live.yes >= g_cp_lobby.blocs_to_pass;
                    if (live.yes == 0) live.yes = -1;  // voted, nobody for it
                }
                if (s.now.hour_index >= ms.answer_hour) {
                    MeasureState& live = s.pass.measures[m.id];
                    if (live.yes < 0) live.yes = 0;
                    cp_settle_measure(s, m, *body, live.voted_yes);
                }
            } else if (body->side == ai && m.has_ai_send && cp_measure_open(s, m) && s.now.hour_index >= cp_hour(m.ai_send)) {
                cp_send_dispatch(s, m);
            }
            continue;
        }
        if (!sitting) {
            if (ms.status.empty() && body->has_until && s.now.hour_index >= cp_hour(body->until)) s.pass.measures[m.id].status = "lapsed";
            continue;
        }
        if (ms.status == "failed" && ms.retry_hour > 0 && s.now.hour_index >= ms.retry_hour) {
            // It comes back to the floor: a week to work on the blocs before it is put again.
            MeasureState& live = s.pass.measures[m.id];
            live.status.clear();
            live.retry_hour = 0;
            live.vote_hour = s.now.hour_index + 24LL * 7;
            if (body->side == s.player_faction) cp_note(s, body->name + ": " + m.title + " is put again, to be voted on " + cp_date_words(live.vote_hour));
            continue;
        }
        if (cp_measure_open(s, m) && cp_vote_hour(s, m) > 0 && s.now.hour_index >= cp_vote_hour(s, m)) cp_vote(s, m, *body);
    }
    // Delegates work on their bloc week by week.
    if (g_cp_persons_loaded && s.now.hour_index % (24 * 7) == 0)
        for (const CpPerson& p : g_cp_persons) {
            const PersonState st = cp_pstate(s, p.id);
            if (st.role != "delegate" || !st.status.empty() || st.bloc.empty()) continue;
            cp_shift_bloc(s, st.bloc, g_cp_prules.delegate_shift_per_week);
        }
    // Monthly grants (the powder committee, the cruisers' prizes).
    if (now.d == 1 && now.h == 0)
        for (const MonthlyGrant& g : s.pass.grants) {
            if (!s.provinces.count(g.province) || s.provinces.at(g.province).owner != g.faction) continue;
            ProvinceStores& st = province_ledger(s, g.province);
            st.powder += std::max(0, g.powder);
            st.muskets += std::max(0, g.muskets);
            st.provisions += std::max(0, g.provisions);
            if (g.faction != s.player_faction) continue;
            ResourceChange ch;
            ch.reason = g.source + ", " + cp_town(s, g.province) + ":" + (g.powder > 0 ? " +" + std::to_string(g.powder) + " powder" : "") +
                        (g.muskets > 0 ? " +" + std::to_string(g.muskets) + " muskets" : "") +
                        (g.provisions > 0 ? " +" + std::to_string(g.provisions) + " provisions" : "");
            ch.powder = g.powder;
            ch.muskets = g.muskets;
            ch.provisions = g.provisions;
            ch.province_id = g.province;
            detail::record_resource_change(s, ch);
        }
    // Favors for missions done.
    if (missions_loaded())
        for (const MissionDef& m : mission_defs()) {
            if (m.faction != s.player_faction || detail::map_get(s.progress.missions, m.id).status != "complete") continue;
            if (detail::contains(s.pass.counted_missions, m.id)) continue;
            s.pass.counted_missions.push_back(m.id);
            if (g_cp_lobby.favors_per_mission <= 0) continue;
            s.pass.favors += g_cp_lobby.favors_per_mission;
            cp_note(s, "Mission done, " + m.title + ": +" + std::to_string(g_cp_lobby.favors_per_mission) + " favor in " +
                           (cp_active_body(s, s.player_faction) ? cp_active_body(s, s.player_faction)->name : std::string("your councils")));
        }
    // Unity: under the floor the troops of other colonies begin to go home, and a company serving
    // on its own terms goes when it is left unpaid.
    if (now.h == 0) {
        const bool low = cp_active_body(s, kContinental) && unity(s) < g_cp_lobby.unity_floor && s.player_faction == kContinental;
        const bool unpaid = s.progress.pay_arrears_months > 0;
        int leaving = low ? g_cp_lobby.desertion_per_day : 0;
        std::vector<std::string> ids;
        for (const auto& ak : s.armies)
            if (ak.second.faction == kContinental) ids.push_back(ak.first);
        for (const std::string& id : ids) {
            Army& a = s.armies.at(id);
            if (a.role == "allied_company" && unpaid && s.player_faction == kContinental) {
                cp_note(s, "The " + a.name + " goes home: it was promised its pay and has not had it");
                s.armies.erase(id);
                continue;
            }
            for (auto& al : a.allied) {
                al.second = std::min(al.second, std::max(0, detail::map_get(a.units, al.first)));
                while (leaving > 0 && al.second > 0) {
                    --al.second;
                    --a.units[al.first];
                    --leaving;
                    cp_note(s, "Unity is low (" + std::to_string(unity(s)) + "): a unit of " + detail::class_words(al.first) +
                                   " from another colony leaves " + a.name + " and goes home");
                }
            }
            if (detail::total_units(a) <= 0) s.armies.erase(id);
        }
    }
    // The ministry's patience.
    if (g_cp_persons_loaded && s.pass.ministry_confidence < g_cp_lobby.recall_floor) cp_recall_commander(s);
    // Delayed consequences fall due.
    std::vector<DelayedEffect> due, later;
    for (const DelayedEffect& d : s.pass.delayed) (s.now.hour_index >= d.due_hour ? due : later).push_back(d);
    s.pass.delayed = later;
    for (const DelayedEffect& d : due) {
        if (d.condition == "boston_not_taken" && campaign_rules_loaded()) {
            auto b = s.provinces.find(campaign_rules().british_base);
            if (b != s.provinces.end() && b->second.owner == d.faction) continue;
        }
        CpEffects fx;
        std::vector<std::string> errors;
        try {
            if (!cp_parse_effects(cp_json::parse(d.effects_json), fx, errors, d.source)) continue;
        } catch (...) {
            continue;
        }
        cp_apply_effects(s, d.faction, fx, d.source + (d.text.empty() ? "" : ": " + d.text));
    }
}

// ---- Decisions ---------------------------------------------------------------------

bool cp_require_ok(const CampaignState& s, const CpDecision& d, const CpRequire& r) {
    if (r.type == "flag") return cp_has_flag(s, r.flag);
    if (r.type == "owner_is") {
        auto it = s.provinces.find(r.province);
        return it != s.provinces.end() && it->second.owner == r.faction;
    }
    if (r.type == "fort_at_least") {
        auto it = s.provinces.find(r.province);
        return it != s.provinces.end() && it->second.owner == r.faction && it->second.fort_level >= r.level;
    }
    if (r.type == "measure_passed") return detail::map_get(s.pass.measures, r.measure).times > 0;
    if (r.type == "decision_taken") {
        auto it = s.pass.decisions.find(r.decision);
        return it != s.pass.decisions.end() && (r.option.empty() || it->second == r.option);
    }
    if (r.type == "free_person") {
        for (const CpPerson& p : g_cp_persons)
            if (p.side == d.side && detail::contains(p.roles, r.role) && cp_person_present(s, p)) return true;
        return false;
    }
    return false;
}

bool cp_decision_due(const CampaignState& s, const CpDecision& d) {
    if (s.pass.decisions.count(d.id) || s.now.hour_index < cp_hour(d.from)) return false;
    for (const CpRequire& r : d.requires_all)
        if (!cp_require_ok(s, d, r)) return false;
    return true;
}

// The person an option commits: the one named, else (for the AI) the first who can go.
std::string cp_commit_person(const CampaignState& s, const CpDecision& d, const CpOption& o, const std::string& named, std::string& error) {
    if (o.commit_role.empty()) return "";
    if (!named.empty()) {
        const CpPerson* p = cp_person(named);
        if (!p) error = "unknown_person";
        else if (p->side != d.side) error = "not_your_person";
        else if (!detail::contains(p->roles, o.commit_role)) error = "wrong_role";
        else if (!cp_person_present(s, *p)) error = "person_not_available";
        return named;
    }
    for (const CpPerson& p : g_cp_persons)
        if (p.side == d.side && detail::contains(p.roles, o.commit_role) && cp_person_present(s, p) && cp_pstate(s, p.id).role.empty()) return p.id;
    for (const CpPerson& p : g_cp_persons)
        if (p.side == d.side && detail::contains(p.roles, o.commit_role) && cp_person_present(s, p)) return p.id;
    error = "needs_person";
    return "";
}

void cp_take_decision(CampaignState& s, const CpDecision& d, const CpOption& o, const std::string& person_id) {
    s.pass.decisions[d.id] = o.id;
    if (s.pass.pending_decision == d.id) s.pass.pending_decision.clear();
    std::string who;
    if (!person_id.empty()) {
        const CpPerson* p = cp_person(person_id);
        cp_release_person(s, person_id);
        PersonState& st = s.pass.persons[person_id];
        st.status = "away";
        st.away_until_hour = s.now.hour_index + 24LL * std::max(1, o.commit_days);
        who = p ? " (" + p->name + " goes, back " + cp_date_words(st.away_until_hour) + ")" : std::string();
    }
    const std::string source = d.title + " " + o.label;
    CpEffects both = o.cost;
    const std::string cost_words = o.cost.summary;
    cp_apply_effects(s, d.side, o.cost, source + who);
    cp_apply_effects(s, d.side, o.gain, source);
    (void)both;
    (void)cost_words;
    if (o.later_days > 0)
        cp_push_delayed(s, d.side, d.title, o.later_text, o.later_days, o.later_json, o.id == "assault" ? "boston_not_taken" : "");
    s.memory.push_back({s.now.hour_index, d.side, "Decided: " + d.title + " " + o.label});
}

void cp_tick_decisions(CampaignState& s, std::vector<Interrupt>& interrupts) {
    if (!g_cp_decisions_loaded) return;
    for (const CpDecision& d : g_cp_decisions) {
        if (!cp_decision_due(s, d)) continue;
        if (d.side == s.player_faction) {
            if (!s.pass.pending_decision.empty()) continue;  // one at a time
            s.pass.pending_decision = d.id;
            interrupts.push_back(Interrupt::Decision);
            detail::add_log(s, "decision put to the player: " + d.id);
            continue;
        }
        // The other side's commander decides for himself.
        for (const CpOption& o : d.options) {
            if (o.id != d.ai_option) continue;
            std::string error;
            const std::string person = cp_commit_person(s, d, o, "", error);
            if (!error.empty()) {
                // Nobody to send: he takes the first option that needs no one.
                bool taken = false;
                for (const CpOption& alt : d.options)
                    if (alt.commit_role.empty() && !taken) {
                        cp_take_decision(s, d, alt, "");
                        taken = true;
                    }
                if (!taken) s.pass.decisions[d.id] = "";
            } else {
                cp_take_decision(s, d, o, person);
            }
        }
        if (!s.pass.decisions.count(d.id)) s.pass.decisions[d.id] = "";
    }
}

// ---- Orders ----------------------------------------------------------------------

struct CpOrder {
    std::string order, army_id, province, person, role, bloc, action, measure, decision, option;
    std::string from_army, to_army;
    std::map<std::string, int> units;  // transfer_units: class -> count
};

std::string cp_parse_order(const std::string& text, CpOrder& o) {
    try {
        const cp_json j = cp_json::parse(text);
        if (!j.is_object() || !j.contains("order") || !j.at("order").is_string()) return "malformed_order";
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string& k = it.key();
            if (k == "units") {
                if (!it.value().is_object()) return "malformed_order";
                for (auto uit = it.value().begin(); uit != it.value().end(); ++uit) {
                    if (!uit.value().is_number_integer()) return "malformed_order";
                    o.units[uit.key()] = uit.value().get<int>();
                }
                continue;
            }
            if (!it.value().is_string()) return "malformed_order";
            const std::string v = it.value().get<std::string>();
            if (k == "order") o.order = v;
            else if (k == "army_id") o.army_id = v;
            else if (k == "province") o.province = v;
            else if (k == "person") o.person = v;
            else if (k == "role") o.role = v;
            else if (k == "bloc") o.bloc = v;
            else if (k == "action") o.action = v;
            else if (k == "measure") o.measure = v;
            else if (k == "decision") o.decision = v;
            else if (k == "option") o.option = v;
            else if (k == "from_army") o.from_army = v;
            else if (k == "to_army") o.to_army = v;
            else return "unexpected_field";
        }
    } catch (...) {
        return "malformed_order";
    }
    if (!cp_known(o.order, {"halt", "form_army", "assign_person", "relieve_person", "use_agent", "lobby", "vote_now",
                            "send_dispatch", "decide", "transfer_units"}))
        return "unknown_order";
    return "";
}

// The side an order is given for: the player. (The AI acts through the hourly rules.)
std::string cp_check_person(const CampaignState& s, const std::string& id, const CpPerson*& out) {
    if (!g_cp_persons_loaded) return "persons_not_loaded";
    out = cp_person(id);
    if (!out) return "unknown_person";
    if (out->side != s.player_faction) return "not_your_person";
    if (!cp_person_present(s, *out)) return "person_not_available";
    return "";
}

std::string cp_validate(const CampaignState& s, const CpOrder& o) {
    const std::string side = s.player_faction;
    if (o.order == "halt") return s.armies.count(o.army_id) ? "" : "unknown_army";
    if (o.order == "form_army") {
        const CpPerson* p = nullptr;
        if (const std::string e = cp_check_person(s, o.person, p); !e.empty()) return e;
        if (!detail::contains(p->roles, "general")) return "not_a_general";
        if (!cp_pstate(s, p->id).role.empty()) return "person_busy";
        auto pit = s.provinces.find(o.province);
        if (pit == s.provinces.end()) return "unknown_province";
        if (pit->second.owner != side) return "not_your_province";
        if (!cp_town_has_muster(pit->second)) return "no_muster";
        if (s.pending_battle.active) return "battle_pending";
        bool any = false, free_army = false;
        for (const auto& ak : s.armies) {
            if (ak.second.faction != side || ak.second.province != o.province || !ak.second.path.empty() || ak.second.role == "allied_company") continue;
            if (!o.army_id.empty() && ak.first != o.army_id) continue;
            any = true;
            if (general_of(s, ak.first).empty()) free_army = true;
        }
        if (!any) return "no_army_here";
        return free_army ? "" : "army_has_general";
    }
    if (o.order == "assign_person") {
        const CpPerson* p = nullptr;
        if (const std::string e = cp_check_person(s, o.person, p); !e.empty()) return e;
        if (!cp_known(o.role, {"organizer", "delegate", "envoy"})) return "unknown_role";
        if (!detail::contains(p->roles, o.role)) return "wrong_role";
        if (!cp_pstate(s, p->id).role.empty()) return "person_busy";
        if (o.role == "organizer") {
            auto pit = s.provinces.find(o.province);
            if (pit == s.provinces.end()) return "unknown_province";
            if (pit->second.owner != side) return "not_your_province";
            if (cp_organizer_in(s, side, o.province)) return "town_has_organizer";
        }
        if (o.role == "delegate") {
            if (!g_cp_politics_loaded) return "politics_not_loaded";
            const CpBody* body = cp_active_body(s, side);
            if (!body || !cp_bloc(*body, o.bloc)) return "unknown_bloc";
            for (const auto& pk : s.pass.persons)
                if (pk.second.role == "delegate" && pk.second.bloc == o.bloc && pk.second.status.empty()) return "bloc_taken";
        }
        if (o.role == "envoy")
            for (const CpPerson& q : g_cp_persons)
                if (q.side == side && cp_pstate(s, q.id).role == "envoy" && cp_pstate(s, q.id).status.empty()) return "already_have_envoy";
        return "";
    }
    if (o.order == "relieve_person") {
        const CpPerson* p = nullptr;
        if (const std::string e = cp_check_person(s, o.person, p); !e.empty()) return e;
        const PersonState st = cp_pstate(s, p->id);
        if (st.role.empty()) return "holds_no_role";
        if (s.pending_battle.active && st.role == "general") return "battle_pending";
        if (st.role == "delegate" && g_cp_prules.delegate_reseat_hours > 0 && st.seated_hour > 0 &&
            s.now.hour_index < st.seated_hour + g_cp_prules.delegate_reseat_hours)
            return "recently_seated";
        return "";
    }
    if (o.order == "transfer_units") {
        if (o.from_army.empty() || o.to_army.empty()) return "unknown_army";
        if (o.from_army == o.to_army) return "same_army";
        auto fit = s.armies.find(o.from_army);
        auto tit = s.armies.find(o.to_army);
        if (fit == s.armies.end() || tit == s.armies.end()) return "unknown_army";
        const Army& from = fit->second;
        const Army& to = tit->second;
        if (from.faction != side || to.faction != side) return "not_your_army";
        if (from.role == "allied_company" || to.role == "allied_company") return "allied_company";
        // A halt part-way clears the path and sets returning_hours: the army is still on the road.
        if (!from.path.empty() || !to.path.empty() || from.returning_hours > 0 || to.returning_hours > 0)
            return "army_on_the_march";
        if (from.province != to.province) return "not_same_town";
        if (o.units.empty()) return "no_units";
        int moving = 0;
        for (const auto& u : o.units) {
            if (u.second <= 0) return "no_units";
            if (detail::map_get(from.units, u.first) < u.second) return "not_enough_units";
            moving += u.second;
        }
        const int cap = detail::army_unit_cap(s, to);
        if (cap > 0 && detail::total_units(to) + moving > cap) return "over_cap";
        return "";
    }
    if (o.order == "use_agent") {
        const CpPerson* p = nullptr;
        if (const std::string e = cp_check_person(s, o.person, p); !e.empty()) return e;
        if (p->ability.empty() || !detail::contains(p->roles, "agent")) return "not_an_agent";
        if (cp_uses_left(s, *p) <= 0) return "no_uses_left";
        if (p->ability == "alarm") {
            if (!s.pending_battle.active) return "no_battle_pending";
            auto dit = s.armies.find(s.pending_battle.defender_army);
            if (dit == s.armies.end() || dit->second.faction != side) return "not_defending";
            for (const TownFactor& f : s.pass.town_factors)
                if (f.label == "alarm" && f.province == s.pending_battle.province && f.faction == side) return "already_alarmed";
            return "";
        }
        if (p->ability == "scout") {
            auto ait = s.armies.find(o.army_id);
            if (ait == s.armies.end()) return "unknown_army";
            if (ait->second.faction == side) return "not_an_enemy_army";
            return "";
        }
        if (p->ability == "boats") {
            auto ait = s.armies.find(o.army_id);
            if (ait == s.armies.end()) return "unknown_army";
            if (ait->second.faction != side) return "not_your_army";
            return "";
        }
        // engineer, pen: a town the side holds
        auto pit = s.provinces.find(o.province);
        if (pit == s.provinces.end()) return "unknown_province";
        if (pit->second.owner != side) return "not_your_province";
        return "";
    }
    if (o.order == "lobby") {
        if (!g_cp_politics_loaded) return "politics_not_loaded";
        const CpBody* body = cp_active_body(s, side);
        if (!body || !cp_bloc(*body, o.bloc)) return "unknown_bloc";
        if (o.action == "write") return s.staff.letters_processed_today < s.staff.capacity_per_day ? "" : "no_clerk_slot";
        if (o.action == "favor") return s.pass.favors > 0 ? "" : "no_favors";
        return "unknown_action";
    }
    if (o.order == "vote_now" || o.order == "send_dispatch") {
        if (!g_cp_politics_loaded) return "politics_not_loaded";
        const CpMeasure* m = cp_measure(o.measure);
        const CpBody* body = m ? cp_body(m->body) : nullptr;
        if (!m || !body) return "unknown_measure";
        if (body->side != side || cp_active_body(s, side) != body) return "not_your_body";
        if (body->dispatch != (o.order == "send_dispatch")) return o.order == "send_dispatch" ? "not_a_dispatch" : "is_a_dispatch";
        const MeasureState ms = detail::map_get(s.pass.measures, m->id);
        if (ms.status == "passed") return "already_passed";
        if (ms.status == "sent") return "already_sent";
        if (ms.status == "failed" || (ms.retry_hour > 0 && s.now.hour_index < ms.retry_hour)) return "not_before_it_returns";
        if (!cp_measure_open(s, *m)) return "too_early";
        return "";
    }
    if (o.order == "decide") {
        if (!g_cp_decisions_loaded) return "decisions_not_loaded";
        const CpDecision* d = cp_decision(o.decision);
        if (!d) return "unknown_decision";
        if (d->side != side) return "not_your_decision";
        if (s.pass.pending_decision != d->id) return "not_pending";
        for (const CpOption& opt : d->options) {
            if (opt.id != o.option) continue;
            if (opt.commit_role.empty()) return o.person.empty() ? "" : "no_person_needed";
            if (o.person.empty()) return "needs_person";
            std::string error;
            cp_commit_person(s, *d, opt, o.person, error);
            return error;
        }
        return "unknown_option";
    }
    return "unknown_order";
}

}  // namespace

// ---- Loading -----------------------------------------------------------------------

std::vector<std::string> load_persons(const std::string& json_text) {
    std::vector<std::string> errors;
    CpPersonRules rules;
    std::vector<CpPerson> persons;
    std::map<std::string, CpTrait> traits;
    std::map<std::string, CpAbility> abilities;
    HeroRules hero;
    try {
        const cp_json j = cp_json::parse(json_text);
        const cp_json& r = j.at("rules");
        rules.detachment_unit_cap = r.at("detachment_unit_cap").get<int>();
        rules.army_unit_cap = r.at("army_unit_cap").get<int>();
        rules.muster_buildings = r.at("muster_buildings").get<std::vector<std::string>>();
        rules.organizer_support = r.at("organizer").at("support_per_month").get<int>();
        rules.organizer_muster_pct = r.at("organizer").at("muster_hours_pct").get<int>();
        rules.delegate_shift_per_week = r.at("delegate").at("shift_per_week").get<int>();
        rules.delegate_seat_shift = r.at("delegate").value("seat_shift", rules.delegate_shift_per_week);
        rules.delegate_reseat_hours = r.at("delegate").value("reseat_hours", 168);
        rules.envoy_attitude = r.at("envoy").at("attitude_once").get<int>();
        const cp_json& c = r.at("commander");
        rules.aura_radius_m = c.at("aura_radius_m").get<float>();
        rules.home_post_radius_m = c.at("home_post_radius_m").get<float>();
        rules.fall_pressure = c.at("fall_pressure").get<float>();
        rules.fall_pressure_seconds = c.at("fall_pressure_seconds").get<float>();
        rules.fate_wounded = c.at("fall_fate_pct").at("wounded").get<int>();
        rules.fate_killed = c.at("fall_fate_pct").at("killed").get<int>();
        rules.fate_captured = c.at("fall_fate_pct").at("captured").get<int>();
        rules.wounded_days = c.at("wounded_days").get<int>();
        rules.army_morale_on_fall = c.at("army_morale_on_fall").get<int>();
        rules.army_morale_on_fall_victory = c.value("army_morale_on_fall_victory", 0);
        rules.step_men = r.at("estimates").at("step_men").get<int>();
        rules.width_pct = r.at("estimates").at("width_pct").get<int>();
        rules.exposed_support = r.at("informer").at("exposed_local_support").get<int>();
        for (auto it = j.at("traits").begin(); it != j.at("traits").end(); ++it) {
            if (!cp_known(it.key(), {"defensive", "aggressive", "steadfast", "adroit", "engineer", "popular"}))
                errors.push_back(detail::err("unknown_trait", it.key()));
            CpTrait t;
            t.label = it.value().at("label").get<std::string>();
            t.text = it.value().value("text", "");
            for (const auto& e : it.value().at("effects")) {
                CpTraitEffect te;
                te.when = e.at("when").get<std::string>();
                te.label = e.at("label").get<std::string>();
                te.value = e.at("value").get<int>();
                if (!cp_known(te.when, {"always", "attacking", "defending", "defending_own_ground"}))
                    errors.push_back(detail::err("unknown_trait_condition", it.key() + " " + te.when));
                if (!cp_known(te.label, {"morale", "readiness", "reinforcement", "fortification"}))
                    errors.push_back(detail::err("unknown_trait_label", it.key() + " " + te.label));
                t.effects.push_back(te);
            }
            traits[it.key()] = t;
        }
        for (auto it = j.at("abilities").begin(); it != j.at("abilities").end(); ++it) {
            if (!cp_known(it.key(), {"alarm", "scout", "engineer", "boats", "pen"})) errors.push_back(detail::err("unknown_ability", it.key()));
            CpAbility a;
            a.label = it.value().at("label").get<std::string>();
            a.where = it.value().at("where").get<std::string>();
            a.text = it.value().value("text", "");
            a.morale = it.value().value("morale", 0);
            a.days = it.value().value("days", 0);
            a.fortification = it.value().value("fortification", 0);
            a.local_support = it.value().value("local_support", 0);
            a.muster_at_once = it.value().value("muster_at_once", false);
            abilities[it.key()] = a;
        }
        // GAME INVENTION: the hero rules. A file without the block has no hero.
        if (j.contains("hero")) {
            const cp_json& h = j.at("hero");
            if (h.at("basis").get<std::string>() != "invented") errors.push_back(detail::err("hero_not_marked_invented", "hero"));
            hero.loaded = true;
            hero.score_threshold = h.at("score").at("threshold").get<int>();
            hero.score_kill = h.at("score").at("kill").get<int>();
            hero.score_capture = h.at("score").at("capture").get<int>();
            hero.score_rally = h.at("score").at("rally").get<int>();
            hero.meter_seconds = h.at("meter").at("seconds").get<float>();
            hero.refill_kill_seconds = h.at("meter").at("refill_kill_seconds").get<float>();
            hero.refill_capture_seconds = h.at("meter").at("refill_capture_seconds").get<float>();
            hero.refill_rally_seconds = h.at("meter").at("refill_rally_seconds").get<float>();
            const cp_json& k = h.at("kit");
            hero.health_scale = k.at("health_scale").get<float>();
            hero.aura_radius_m = k.at("aura_radius_m").get<float>();
            hero.rally_radius_m = k.at("rally_radius_m").get<float>();
            hero.rally_cooldown_seconds = k.at("rally_cooldown_seconds").get<float>();
            hero.sword_damage = k.at("sword").at("damage").get<float>();
            hero.sword_reach_cm = k.at("sword").at("reach_cm").get<float>();
            hero.sword_recover_seconds = k.at("sword").at("recover_seconds").get<float>();
            hero.pistol_damage = k.at("pistol").at("damage").get<float>();
            hero.pistol_range_m = k.at("pistol").at("range_m").get<float>();
            hero.pistol_reload_seconds = k.at("pistol").at("reload_seconds").get<float>();
            if (hero.score_threshold < 1 || hero.meter_seconds <= 0.f || hero.health_scale < 1.f)
                errors.push_back(detail::err("bad_hero_rules", "threshold, meter seconds or health scale"));
            for (auto it = h.at("trait_abilities").begin(); it != h.at("trait_abilities").end(); ++it) {
                if (!traits.count(it.key())) errors.push_back(detail::err("unknown_trait", "hero ability " + it.key()));
                HeroAbility a;
                a.trait = it.key();
                a.kind = it.value().at("kind").get<std::string>();
                a.label = it.value().at("label").get<std::string>();
                a.text = it.value().at("text").get<std::string>();
                a.radius_m = it.value().at("radius_m").get<float>();
                a.seconds = it.value().at("seconds").get<float>();
                a.value = it.value().at("value").get<float>();
                a.cooldown_seconds = it.value().at("cooldown_seconds").get<float>();
                if (!cp_known(a.kind, {"hold_fast", "charge", "steady", "quick_step", "throw_up_works", "huzza"}))
                    errors.push_back(detail::err("unknown_hero_ability", it.key() + " " + a.kind));
                hero.trait_abilities[it.key()] = a;
            }
            // A general's first trait gives his ability, so every trait must have one.
            for (const auto& tk : traits)
                if (!hero.trait_abilities.count(tk.first)) errors.push_back(detail::err("trait_without_hero_ability", tk.first));
        }
        for (const auto& pj : j.at("persons")) {
            CpPerson p;
            p.id = pj.at("id").get<std::string>();
            p.name = pj.at("name").get<std::string>();
            p.rank = pj.value("rank", "");
            p.side = pj.at("side").get<std::string>();
            p.roles = pj.at("roles").get<std::vector<std::string>>();
            p.note = pj.at("note").get<std::string>();
            p.basis = pj.at("basis").get<std::string>();
            if (p.side != kBritish && p.side != kContinental) errors.push_back(detail::err("unknown_side", p.id));
            if (p.basis != "documented" && p.basis != "invented") errors.push_back(detail::err("unmarked_claim", p.id));
            for (const std::string& role : p.roles)
                if (!cp_known(role, {"general", "delegate", "organizer", "envoy", "agent"})) errors.push_back(detail::err("unknown_role", p.id + " " + role));
            if (pj.contains("available_from")) {
                p.has_from = true;
                p.from = cp_ymdh(pj.at("available_from"));
            }
            p.requires_measure = pj.value("requires_measure", "");
            if (!p.has_from && p.requires_measure.empty()) errors.push_back(detail::err("no_availability", p.id));
            p.traits = pj.value("traits", std::vector<std::string>{});
            for (const std::string& t : p.traits)
                if (!traits.count(t)) errors.push_back(detail::err("unknown_trait", p.id + " " + t));
            if (detail::contains(p.roles, "general") && p.traits.size() != 2) errors.push_back(detail::err("general_needs_two_traits", p.id));
            p.ability = pj.value("ability", "");
            p.uses = pj.value("uses", 0);
            if (!p.ability.empty() && (!abilities.count(p.ability) || p.uses <= 0)) errors.push_back(detail::err("bad_ability", p.id));
            if (detail::contains(p.roles, "agent") && p.ability.empty()) errors.push_back(detail::err("agent_needs_ability", p.id));
            if (pj.contains("starts_as")) {
                p.starts_role = pj.at("starts_as").at("role").get<std::string>();
                p.starts_army = pj.at("starts_as").value("army_id", "");
                if (!detail::contains(p.roles, p.starts_role)) errors.push_back(detail::err("starts_in_a_role_he_cannot_hold", p.id));
            }
            p.cic = pj.value("commander_in_chief", false);
            if (pj.contains("hero")) {
                const cp_json& ph = pj.at("hero");
                if (ph.at("basis").get<std::string>() != "invented") errors.push_back(detail::err("hero_not_marked_invented", p.id));
                if (!detail::contains(p.roles, "general")) errors.push_back(detail::err("hero_kit_on_one_who_is_no_general", p.id));
                p.hero_health_scale = ph.value("health_scale", 0.f);
                p.hero_aura_radius_m = ph.value("aura_radius_m", 0.f);
                p.hero_rally_radius_m = ph.value("rally_radius_m", 0.f);
            }
            if (pj.contains("informer")) {
                p.informer_for = pj.at("informer").at("for").get<std::string>();
                if (pj.at("informer").contains("exposed")) {
                    p.has_exposed = true;
                    p.exposed = cp_ymdh(pj.at("informer").at("exposed"));
                }
            }
            for (const CpPerson& other : persons)
                if (other.id == p.id) errors.push_back(detail::err("duplicate_person", p.id));
            persons.push_back(p);
        }
    } catch (const std::exception& e) {
        errors.push_back(detail::err("persons_parse_error", e.what()));
    }
    if (!errors.empty()) return errors;
    g_cp_prules = rules;
    g_cp_persons = persons;
    g_cp_hero = hero;
    g_cp_traits = traits;
    g_cp_abilities = abilities;
    g_cp_persons_loaded = true;
    return errors;
}
void unload_persons() {
    g_cp_persons.clear();
    g_cp_traits.clear();
    g_cp_abilities.clear();
    g_cp_prules = CpPersonRules{};
    g_cp_persons_loaded = false;
    g_cp_hero = HeroRules{};
}
bool persons_loaded() { return g_cp_persons_loaded; }

std::vector<std::string> load_politics(const std::string& bodies_json, const std::string& measures_json) {
    std::vector<std::string> errors;
    CpLobbyRules rules;
    std::vector<CpBody> bodies;
    std::vector<CpMeasure> measures;
    try {
        const cp_json j = cp_json::parse(bodies_json);
        const cp_json& l = j.at("lobby");
        rules.write_shift = l.at("write_shift").get<int>();
        rules.favor_shift = l.at("favor_shift").get<int>();
        rules.vote_base = l.at("vote_base_pct").get<int>();
        rules.vote_min = l.at("vote_min_pct").get<int>();
        rules.vote_max = l.at("vote_max_pct").get<int>();
        rules.blocs_to_pass = l.at("blocs_to_pass").get<int>();
        rules.favors_at_start = l.at("favors_at_start").get<int>();
        rules.favors_per_mission = l.at("favors_per_mission_done").get<int>();
        rules.favors_per_victory = l.at("favors_per_victory").get<int>();
        rules.unity_floor = l.at("unity_floor").get<int>();
        rules.desertion_per_day = l.at("unity_desertion_units_per_day").get<int>();
        rules.days_out = j.at("dispatch").at("days_out").get<int>();
        rules.days_back = j.at("dispatch").at("days_back").get<int>();
        const cp_json& mn = j.at("ministry");
        rules.confidence_start = mn.at("confidence_start").get<int>();
        rules.on_defeat = mn.at("on_defeat").get<int>();
        rules.on_victory = mn.at("on_victory").get<int>();
        rules.recall_floor = mn.at("recall_floor").get<int>();
        rules.confidence_after_recall = mn.at("confidence_after_recall").get<int>();
        rules.replacements = mn.at("replacements").get<std::vector<std::string>>();
        for (const char* k : {"dispatch", "ministry"})
            if (!cp_known(j.at(k).value("basis", ""), {"documented", "invented"})) errors.push_back(detail::err("unmarked_claim", k));
        std::set<std::string> bloc_ids;
        for (const auto& bj : j.at("bodies")) {
            CpBody b;
            b.id = bj.at("id").get<std::string>();
            b.side = bj.at("side").get<std::string>();
            b.name = bj.at("name").get<std::string>();
            b.button = bj.at("button").get<std::string>();
            b.note = bj.value("note", "");
            b.basis = bj.value("basis", "");
            b.dispatch = bj.value("dispatch", false);
            if (b.side != kBritish && b.side != kContinental) errors.push_back(detail::err("unknown_side", b.id));
            if (b.basis != "documented" && b.basis != "invented") errors.push_back(detail::err("unmarked_claim", b.id));
            if (bj.contains("from")) {
                b.has_from = true;
                b.from = cp_ymdh(bj.at("from"));
            }
            if (bj.contains("until")) {
                b.has_until = true;
                b.until = cp_ymdh(bj.at("until"));
            }
            for (const auto& cj : bj.at("blocs")) {
                CpBloc c;
                c.id = cj.at("id").get<std::string>();
                c.name = cj.at("name").get<std::string>();
                c.moves = cj.at("moves").get<std::string>();
                c.lean = cj.at("lean").get<int>();
                c.on_victory = cj.value("on_victory", 0);
                c.on_defeat = cj.value("on_defeat", 0);
                if (!bloc_ids.insert(c.id).second) errors.push_back(detail::err("duplicate_bloc", c.id));
                b.blocs.push_back(c);
            }
            if (b.blocs.size() != 3) errors.push_back(detail::err("body_needs_three_blocs", b.id));
            bodies.push_back(b);
        }
        const cp_json mj = cp_json::parse(measures_json);
        for (const auto& m : mj.at("measures")) {
            CpMeasure cm;
            cm.id = m.at("id").get<std::string>();
            cm.body = m.at("body").get<std::string>();
            cm.title = m.at("title").get<std::string>();
            cm.text = m.value("text", "");
            cm.note = m.at("note").get<std::string>();
            cm.basis = m.at("basis").get<std::string>();
            if (cm.basis != "documented" && cm.basis != "invented") errors.push_back(detail::err("unmarked_claim", cm.id));
            const CpBody* body = nullptr;
            for (const CpBody& b : bodies)
                if (b.id == cm.body) body = &b;
            if (!body) {
                errors.push_back(detail::err("unknown_body", cm.id + " -> " + cm.body));
                continue;
            }
            if (m.contains("earliest")) {
                cm.has_earliest = true;
                cm.earliest = cp_ymdh(m.at("earliest"));
            }
            if (m.contains("vote")) {
                cm.has_vote = true;
                cm.vote = cp_ymdh(m.at("vote"));
            }
            if (m.contains("arrives")) {
                cm.has_arrives = true;
                cm.arrives = cp_ymdh(m.at("arrives"));
            }
            if (m.contains("ai_send")) {
                cm.has_ai_send = true;
                cm.ai_send = cp_ymdh(m.at("ai_send"));
            }
            if (!body->dispatch && (!cm.has_earliest || !cm.has_vote)) errors.push_back(detail::err("measure_needs_dates", cm.id));
            if (body->dispatch && !cm.has_arrives && !cm.has_earliest) errors.push_back(detail::err("dispatch_needs_a_date", cm.id));
            cm.bias = m.value("bias", std::map<std::string, int>{});
            for (const auto& bk : cm.bias) {
                bool found = false;
                for (const CpBloc& c : body->blocs) found = found || c.id == bk.first;
                if (!found) errors.push_back(detail::err("unknown_bloc", cm.id + " " + bk.first));
            }
            cp_parse_effects(m.at("passed"), cm.passed, errors, cm.id + ".passed");
            cp_parse_effects(m.at("failed"), cm.failed, errors, cm.id + ".failed");
            cm.passed_json = m.at("passed").dump();
            cm.failed_json = m.at("failed").dump();
            if (m.contains("passed_later")) {
                cm.later_days = m.at("passed_later").at("days").get<int>();
                cp_parse_effects(m.at("passed_later").at("effects"), cm.later, errors, cm.id + ".passed_later");
                cm.later_json = m.at("passed_later").at("effects").dump();
            }
            cm.retry_days = m.value("retry_days", 0);
            cm.repeatable = m.value("repeatable", false);
            if (!cm.passed.any() && !cm.later.any()) errors.push_back(detail::err("measure_without_effect", cm.id));
            for (const CpMeasure& other : measures)
                if (other.id == cm.id) errors.push_back(detail::err("duplicate_measure", cm.id));
            measures.push_back(cm);
        }
    } catch (const std::exception& e) {
        errors.push_back(detail::err("politics_parse_error", e.what()));
    }
    if (!errors.empty()) return errors;
    g_cp_lobby = rules;
    g_cp_bodies = bodies;
    g_cp_measures = measures;
    g_cp_politics_loaded = true;
    return errors;
}
void unload_politics() {
    g_cp_bodies.clear();
    g_cp_measures.clear();
    g_cp_lobby = CpLobbyRules{};
    g_cp_politics_loaded = false;
}
bool politics_loaded() { return g_cp_politics_loaded; }

std::vector<std::string> load_decisions(const std::string& json_text) {
    std::vector<std::string> errors;
    std::vector<CpDecision> decisions;
    try {
        const cp_json j = cp_json::parse(json_text);
        for (const auto& dj : j.at("decisions")) {
            CpDecision d;
            d.id = dj.at("id").get<std::string>();
            d.side = dj.at("side").get<std::string>();
            d.title = dj.at("title").get<std::string>();
            d.painting = dj.at("painting").get<std::string>();
            d.text = dj.at("text").get<std::string>();
            d.ai_option = dj.at("ai_option").get<std::string>();
            d.note = dj.at("note").get<std::string>();
            d.basis = dj.at("basis").get<std::string>();
            d.from = cp_ymdh(dj.at("from"));
            if (d.side != kBritish && d.side != kContinental) errors.push_back(detail::err("unknown_side", d.id));
            if (d.basis != "documented" && d.basis != "invented") errors.push_back(detail::err("unmarked_claim", d.id));
            if (dj.contains("requires"))
                for (const auto& rj : dj.at("requires")) {
                    CpRequire r;
                    r.type = rj.at("type").get<std::string>();
                    if (!cp_known(r.type, {"flag", "owner_is", "fort_at_least", "measure_passed", "decision_taken", "free_person"})) {
                        errors.push_back(detail::err("unknown_requirement", d.id + " " + r.type));
                        continue;
                    }
                    r.flag = rj.value("flag", "");
                    r.province = rj.value("province", "");
                    r.faction = rj.value("faction", "");
                    r.role = rj.value("role", "");
                    r.decision = rj.value("decision", "");
                    r.option = rj.value("option", "");
                    r.measure = rj.value("measure", "");
                    r.level = rj.value("level", 0);
                    d.requires_all.push_back(r);
                }
            bool ai_found = false;
            for (const auto& oj : dj.at("options")) {
                CpOption o;
                o.id = oj.at("id").get<std::string>();
                o.label = oj.at("label").get<std::string>();
                cp_parse_effects(oj.at("cost"), o.cost, errors, d.id + "." + o.id + ".cost");
                cp_parse_effects(oj.at("gain"), o.gain, errors, d.id + "." + o.id + ".gain");
                o.later_days = oj.at("later").at("days").get<int>();
                o.later_text = oj.at("later").value("text", "");
                cp_parse_effects(oj.at("later").at("effects"), o.later, errors, d.id + "." + o.id + ".later");
                o.later_json = oj.at("later").at("effects").dump();
                if (oj.contains("commit")) {
                    o.commit_role = oj.at("commit").at("role").get<std::string>();
                    o.commit_days = oj.at("commit").at("days").get<int>();
                }
                if (o.cost.summary.empty() || o.gain.summary.empty() || o.later.summary.empty())
                    errors.push_back(detail::err("option_needs_cost_gain_and_consequence", d.id + "." + o.id));
                if (!o.gain.any() || !o.later.any() || o.later_days <= 0) errors.push_back(detail::err("option_without_effect", d.id + "." + o.id));
                if (!o.cost.any() && o.commit_role.empty()) errors.push_back(detail::err("option_without_cost", d.id + "." + o.id));
                ai_found = ai_found || o.id == d.ai_option;
                d.options.push_back(o);
            }
            if (d.options.size() != 2) errors.push_back(detail::err("decision_needs_two_options", d.id));
            if (!ai_found) errors.push_back(detail::err("unknown_ai_option", d.id));
            for (const CpDecision& other : decisions)
                if (other.id == d.id) errors.push_back(detail::err("duplicate_decision", d.id));
            decisions.push_back(d);
        }
    } catch (const std::exception& e) {
        errors.push_back(detail::err("decisions_parse_error", e.what()));
    }
    if (!errors.empty()) return errors;
    g_cp_decisions = decisions;
    g_cp_decisions_loaded = true;
    return errors;
}
void unload_decisions() {
    g_cp_decisions.clear();
    g_cp_decisions_loaded = false;
}
bool decisions_loaded() { return g_cp_decisions_loaded; }

// ---- Public queries ------------------------------------------------------------------

std::string general_of(const CampaignState& s, const std::string& army_id) {
    if (!g_cp_persons_loaded || army_id.empty()) return "";
    for (const auto& pk : s.pass.persons)
        if (pk.second.role == "general" && pk.second.army_id == army_id && pk.second.status.empty()) return pk.first;
    return "";
}

std::string how_to_form_army(const CampaignState& s, const std::string& faction) {
    if (!g_cp_persons_loaded) return "";
    // Who can take the command: a general at hand, else the first to come.
    std::string who;
    const std::string free_general = cp_free_general(s, faction);
    if (!free_general.empty()) {
        who = person_name(free_general) + " is at hand";
    } else {
        int64_t first = 0;
        std::string first_name;
        for (const CpPerson& p : g_cp_persons) {
            if (p.side != faction || !detail::contains(p.roles, "general") || !p.requires_measure.empty()) continue;
            const PersonState st = cp_pstate(s, p.id);
            if (!st.status.empty() || !st.role.empty() || !p.has_from) continue;
            const int64_t at = cp_hour(p.from);
            if (at <= s.now.hour_index) continue;
            if (first_name.empty() || at < first) {
                first = at;
                first_name = person_name(p.id);
            }
        }
        who = first_name.empty() ? std::string("No general is free") : "No general has come in yet (" + first_name + " on " + cp_date_words(first) + ")";
    }
    // Where: a town the side holds with a muster building.
    std::string towns;
    int n = 0;
    for (const auto& pk : s.provinces) {
        if (pk.second.owner != faction || !cp_town_has_muster(pk.second)) continue;
        towns += (n++ == 0 ? "" : ", ") + pk.second.name;
    }
    const std::string where = n > 0 ? "An army is formed where yours stands in a town you hold that has a Militia Muster or a Fort: " + towns
                                    : std::string("You hold no town with a Militia Muster or a Fort: build a Militia Muster where your men stand");
    return who + ". " + where + ". Then open People, choose the general and form the army.";
}

std::string why_cannot_besiege(const CampaignState& s, const std::string& army_id, const std::string& province) {
    auto ait = s.armies.find(army_id);
    auto pit = s.provinces.find(province);
    if (ait == s.armies.end() || pit == s.provinces.end()) return "";
    const Army& army = ait->second;
    const Province& town = pit->second;
    if (town.owner == army.faction || town.fort_level <= 0) return "";  // nothing to besiege
    if (detail::army_can_besiege(s, army)) return "";
    return army.name + " has no general, so it cannot besiege the works of " + town.name + " (Fort L" + std::to_string(town.fort_level) +
           "): neither a won battle nor standing there takes the town.";
}

std::string person_name(const std::string& person_id) {
    const CpPerson* p = cp_person(person_id);
    return p ? (p->rank.empty() ? p->name : p->rank + " " + p->name) : std::string();
}

CommanderRules commander_rules() {
    CommanderRules r;
    r.aura_radius_m = g_cp_prules.aura_radius_m;
    r.home_post_radius_m = g_cp_prules.home_post_radius_m;
    r.fall_pressure = g_cp_prules.fall_pressure;
    r.fall_pressure_seconds = g_cp_prules.fall_pressure_seconds;
    r.fate_wounded = g_cp_prules.fate_wounded;
    r.fate_killed = g_cp_prules.fate_killed;
    r.fate_captured = g_cp_prules.fate_captured;
    return r;
}

HeroRules hero_rules() { return g_cp_hero; }

std::string commander_fate(uint64_t battle_seed, const std::string& faction) {
    const int total = std::max(1, g_cp_prules.fate_wounded + g_cp_prules.fate_killed + g_cp_prules.fate_captured);
    const int roll = static_cast<int>(detail::mix64(battle_seed ^ detail::fnv1a("general:" + faction)) % static_cast<uint64_t>(total));
    if (roll < g_cp_prules.fate_wounded) return "wounded";
    if (roll < g_cp_prules.fate_wounded + g_cp_prules.fate_killed) return "killed";
    return "captured";
}

int unity(const CampaignState& s) {
    if (!g_cp_politics_loaded) return 100;
    const CpBody* body = cp_active_body(s, s.player_faction);
    if (!body || body->blocs.empty()) return 100;
    int lo = 100, hi = -100;
    for (const CpBloc& b : body->blocs) {
        lo = std::min(lo, cp_lean(s, b));
        hi = std::max(hi, cp_lean(s, b));
    }
    return detail::clamp_int(100 - (hi - lo), 0, 100);
}

int army_unit_cap(const CampaignState& s, const std::string& army_id) {
    auto it = s.armies.find(army_id);
    return it == s.armies.end() ? 0 : detail::army_unit_cap(s, it->second);
}

int purchase_cost_now(const CampaignState& s, int powder, int cartridges, int muskets, int bayonets, int provisions,
                      int lead, int paper) {
    const int base = purchase_cost(powder, cartridges, muskets, bayonets, provisions, lead, paper);
    return (base * (100 + std::max(0, s.pass.price_rise_pct)) + 99) / 100;
}

namespace detail {

bool army_can_besiege(const CampaignState& s, const Army& army) {
    return !g_cp_persons_loaded || !general_of(s, army.id).empty();
}

int army_unit_cap(const CampaignState& s, const Army& army) {
    if (!g_cp_persons_loaded) return 0;
    return general_of(s, army.id).empty() ? g_cp_prules.detachment_unit_cap : g_cp_prules.army_unit_cap;
}

int upkeep_share_pct(const CampaignState& s, const std::string& faction) {
    return clamp_int(map_get(s.pass.upkeep_share_pct, faction), 0, 100);
}

int organizer_muster_hours(const CampaignState& s, const std::string& province, const std::string& faction, int hours) {
    if (!g_cp_persons_loaded || !cp_organizer_in(s, faction, province)) return hours;
    return std::max(1, hours * clamp_int(g_cp_prules.organizer_muster_pct, 1, 100) / 100);
}

bool army_has_boats(const CampaignState& s, const std::string& army_id) {
    auto it = s.pass.boats_until.find(army_id);
    return it != s.pass.boats_until.end() && it->second > s.now.hour_index;
}

int units_as_seen(const CampaignState& s, const std::string& viewer, const Army& army) {
    const CpIntel in = cp_intel(s, viewer, army);
    const int truth = std::max(0, total_units(army));
    if (in.known || truth == 0) return truth;
    // The midpoint can sit above the true count (the band rounds outward). Targeting stops at the truth.
    const int mid_units = in.men / std::max(1, battle_rules().soldiers_per_unit);
    return std::min(truth, std::max(1, mid_units));
}

long ai_lure_value(const CampaignState& s, const std::string& faction, const std::string& province) {
    for (const AiLure& l : s.pass.lures)
        if (l.faction == faction && l.province == province && l.until_hour > s.now.hour_index)
            return campaign_rules_loaded() ? campaign_rules().v_mission : 100000L;
    return 0;
}

// ---- Battle hooks ------------------------------------------------------------------

void pass_side_setup(const CampaignState& s, const Province& ground, const Army& army, bool is_attacker, bool owns_ground,
                     SideSetup& side) {
    auto factor = [&side](const std::string& label, int value, const std::string& source) {
        side.factors.push_back({label, value, source});
    };
    auto apply = [&](const std::string& label, int value) {
        if (label == "morale") side.morale = clamp_int(side.morale + value, 0, 100);
        else if (label == "readiness") side.readiness = clamp_int(side.readiness + value, 0, 100);
        else if (label == "fortification") side.fortification_level = clamp_int(side.fortification_level + value, 0, development_max_level());
        else if (label == "reinforcement") side.reinforcement_scale = std::max(0.5f, side.reinforcement_scale - static_cast<float>(value) / 100.f);
    };
    if (g_cp_persons_loaded) {
        const std::string gen = general_of(s, army.id);
        const CpPerson* p = cp_person(gen);
        if (p) {
            const std::string who = p->rank + " " + p->name;
            side.commander_id = p->id;
            side.commander_name = who;
            side.commander_traits = p->traits;
            side.commander_aura_radius_m = g_cp_prules.aura_radius_m;
            factor("commander", 1, who + " commands " + army.name);
            // GAME INVENTION: the player's own general may be played. Never the other side's.
            if (g_cp_hero.loaded && army.faction == s.player_faction && !p->traits.empty()) {
                auto ab = g_cp_hero.trait_abilities.find(p->traits.front());
                if (ab != g_cp_hero.trait_abilities.end()) {
                    side.has_hero = true;
                    side.hero_health_scale = p->hero_health_scale > 0.f ? p->hero_health_scale : g_cp_hero.health_scale;
                    side.hero_aura_radius_m = p->hero_aura_radius_m > 0.f ? p->hero_aura_radius_m : g_cp_hero.aura_radius_m;
                    side.hero_rally_radius_m = p->hero_rally_radius_m > 0.f ? p->hero_rally_radius_m : g_cp_hero.rally_radius_m;
                    side.hero_rally_cooldown_seconds = g_cp_hero.rally_cooldown_seconds;
                    side.hero_ability = ab->second;
                    side.hero_ability.trait = p->traits.front();
                }
            }
            for (const std::string& tid : p->traits) {
                auto tit = g_cp_traits.find(tid);
                if (tit == g_cp_traits.end()) continue;
                for (const CpTraitEffect& e : tit->second.effects) {
                    const bool on = e.when == "always" || (e.when == "attacking" && is_attacker) || (e.when == "defending" && !is_attacker) ||
                                    (e.when == "defending_own_ground" && !is_attacker && owns_ground);
                    if (!on) continue;
                    // Works are for a defender: a general's eye for ground does nothing for a column on the attack.
                    if (e.label == "fortification" && is_attacker) continue;
                    apply(e.label, e.value);
                    factor(e.label, e.value, who + ": " + tit->second.label);
                }
            }
        } else {
            factor("commander", 0, army.name + ": a detachment under its own captains (no general)");
        }
    }
    for (const TownFactor& f : s.pass.town_factors) {
        if (f.province != ground.id || f.faction != army.faction) continue;
        if (f.until_hour > 0 && s.now.hour_index >= f.until_hour) continue;
        if (f.label == "alarm") {
            if (is_attacker) continue;
            side.alarmed = true;
            apply("morale", f.value);
            factor("alarm", 1, f.source + ": every company is under arms when the enemy comes");
            if (f.value != 0) factor("morale", f.value, f.source);
            continue;
        }
        if (f.label == "fortification" && is_attacker) continue;  // works are the defender's
        apply(f.label, f.value);
        factor(f.label, f.value, f.source);
    }
}

void pass_setup_done(const CampaignState& s, BattleSetup& setup) {
    if (!g_cp_persons_loaded) return;
    // What the player knows of the other side's column (the card shows it from this).
    for (auto& sk : setup.sides) {
        if (sk.first == setup.player_faction) continue;
        auto ait = s.armies.find(sk.second.army_id);
        if (ait == s.armies.end()) continue;
        const CpIntel in = cp_intel(s, setup.player_faction, ait->second);
        sk.second.intel = in.known ? "exact" : "estimate";
        sk.second.intel_men_low = in.men_low;
        sk.second.intel_men_high = in.men_high;
        sk.second.intel_source = in.source;
    }
}

void pass_after_battle(CampaignState& s, const CampaignState& before, const BattleResult& r, const PendingBattle& pb) {
    BattleReport& report = s.progress.last_battle_report;
    const std::string loser = other_faction(r.winner);
    // Generals who fell.
    if (g_cp_persons_loaded) {
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto bit = before.armies.find(aid);
            if (bit == before.armies.end()) continue;
            const std::string faction = bit->second.faction;
            const std::string gen = general_of(before, aid);
            const CpPerson* p = cp_person(gen);
            auto ev = r.commander_events.find(faction);
            if (ev == r.commander_events.end() || !p) continue;
            const std::string who = p->rank + " " + p->name;
            cp_release_person(s, gen);
            PersonState& st = s.pass.persons[gen];
            std::string line;
            if (ev->second == "wounded") {
                st.status = "wounded";
                st.away_until_hour = s.now.hour_index + 24LL * std::max(1, g_cp_prules.wounded_days);
                line = who + " was wounded: he is out of the field until " + cp_date_words(st.away_until_hour);
            } else if (ev->second == "killed") {
                st.status = "killed";
                line = who + " was killed on the field";
            } else {
                st.status = "captured";
                s.prisoners_held[other_faction(faction)] += 1;
                line = who + " was taken prisoner";
            }
            auto ait = s.armies.find(aid);
            if (ait != s.armies.end()) {
                // A win still loses the general (detachment), but does not take the full fall hit.
                const int hit = (faction == r.winner) ? g_cp_prules.army_morale_on_fall_victory
                                                      : g_cp_prules.army_morale_on_fall;
                if (hit != 0) {
                    ait->second.morale = clamp_int(ait->second.morale + hit, 0, 100);
                    report.morale_delta[aid] += hit;
                    line += ". " + ait->second.name + " is a detachment again (morale " + std::to_string(hit) + ")";
                } else {
                    line += ". " + ait->second.name + " is a detachment again";
                }
            }
            report.commander_events[faction] = ev->second;
            report.change_reasons.push_back(line);
            report.map_changes.push_back(line);
            cp_note(s, line, 0, 0, r.province_id);
        }
    }
    // The player fought as his general (GAME INVENTION): one line for the report, and nothing else.
    // What became of the general is commander_events' to say, above. A report for the other side's
    // general, or for someone who does not command, is ignored.
    if (g_cp_persons_loaded && !r.hero_faction.empty() && r.hero_faction == before.player_faction) {
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto bit = before.armies.find(aid);
            if (bit == before.armies.end() || bit->second.faction != r.hero_faction) continue;
            const CpPerson* p = cp_person(general_of(before, aid));
            if (!p || p->id != r.hero_person_id) continue;
            std::string line = "You fought as " + p->rank + " " + p->name + " for " +
                               std::to_string(static_cast<int>(r.hero_seconds_played + 0.5f)) + " seconds";
            if (r.hero_takeovers > 1) line += " (" + std::to_string(r.hero_takeovers) + " times)";
            if (r.hero_fell_while_played) line += "; he fell while you led him";
            report.change_reasons.push_back(line);
        }
    }
    // "Preserve the company": part of the magazine was carried off before the town fell.
    for (const TownFactor& f : s.pass.town_factors) {
        if (f.label != "magazine_saved" || f.province != r.province_id || f.faction == r.winner || f.value <= 0) continue;
        auto pit = s.provinces.find(r.province_id);
        const std::string depot = default_depot_province(s, f.faction);
        if (pit == s.provinces.end() || pit->second.owner == f.faction || depot.empty() || depot == r.province_id) continue;
        ProvinceStores& st = province_ledger(s, r.province_id);
        const int pct = std::min(100, f.value);
        Shipment sh;
        sh.id = "saved_" + r.province_id + "_" + std::to_string(s.next_command_seq++);
        sh.source_province = r.province_id;
        sh.dest_province = depot;
        sh.dest_faction = f.faction;
        sh.powder = std::max(0, st.powder) * pct / 100;
        sh.cartridges = std::max(0, st.cartridges) * pct / 100;
        sh.muskets = std::max(0, st.muskets) * pct / 100;
        sh.guns = std::max(0, st.guns) * pct / 100;
        if (sh.powder + sh.cartridges + sh.muskets + sh.guns <= 0) continue;
        st.powder -= sh.powder;
        st.cartridges -= sh.cartridges;
        st.muskets -= sh.muskets;
        st.guns -= sh.guns;
        sh.commit_hour = s.now.hour_index;
        sh.deliver_on_hour = s.now.hour_index + 12;
        sh.status = "in_transit";
        s.shipments.push_back(sh);
        const std::string line = "Carried off before " + cp_town(s, r.province_id) + " fell: " + std::to_string(sh.cartridges) + " cartridges, " +
                                 std::to_string(sh.powder) + " powder, " + std::to_string(sh.muskets) + " muskets" +
                                 (sh.guns > 0 ? ", " + std::to_string(sh.guns) + (sh.guns == 1 ? " field gun" : " field guns") : std::string()) +
                                 " are on the road to " + cp_town(s, depot) + " (" + f.source + ")";
        report.map_changes.push_back(line);
        report.change_reasons.push_back(line);
        cp_note(s, line, 0, 0, r.province_id);
    }
    // A factor good for one battle is spent.
    auto& tf = s.pass.town_factors;
    tf.erase(std::remove_if(tf.begin(), tf.end(), [&](const TownFactor& f) { return f.one_battle && f.province == r.province_id; }), tf.end());
    // The bodies at home hear of it.
    if (g_cp_politics_loaded) {
        for (const std::string& side : {std::string(kBritish), std::string(kContinental)}) {
            const CpBody* body = cp_active_body(s, side);
            if (!body) continue;
            for (const CpBloc& b : body->blocs) cp_shift_bloc(s, b.id, side == r.winner ? b.on_victory : b.on_defeat);
        }
        s.pass.ministry_confidence = clamp_int(s.pass.ministry_confidence + (r.winner == kBritish ? g_cp_lobby.on_victory : g_cp_lobby.on_defeat), 0, 100);
        if (r.winner == s.player_faction && g_cp_lobby.favors_per_victory > 0) {
            s.pass.favors += g_cp_lobby.favors_per_victory;
            const CpBody* body = cp_active_body(s, s.player_faction);
            report.change_reasons.push_back("The victory is talked of" + (body ? " in the " + body->name : std::string()) + ": +" +
                                            std::to_string(g_cp_lobby.favors_per_victory) + " favor");
        }
    }
    (void)loser;
}

void pass_seed(CampaignState& s) {
    if (!g_cp_persons_loaded && !g_cp_politics_loaded && !g_cp_decisions_loaded) return;
    cp_seed(s);
}

void pass_tick(CampaignState& s, std::vector<Interrupt>& interrupts) {
    if (!g_cp_persons_loaded && !g_cp_politics_loaded && !g_cp_decisions_loaded) return;
    if (s.progress.outcome.decided) return;
    cp_seed(s);
    cp_tick_people(s);
    cp_tick_politics(s);
    cp_tick_decisions(s, interrupts);
}

// ---- Save ---------------------------------------------------------------------------

std::string pass_to_json(const CampaignPass& p) {
    cp_json j = cp_json::object();
    j["seeded"] = p.seeded;
    cp_json persons = cp_json::object();
    for (const auto& pk : p.persons) {
        const PersonState& st = pk.second;
        persons[pk.first] = {{"role", st.role},
                             {"army_id", st.army_id},
                             {"province", st.province},
                             {"bloc", st.bloc},
                             {"uses_left", st.uses_left},
                             {"status", st.status},
                             {"away_until_hour", st.away_until_hour},
                             {"seated_hour", st.seated_hour},
                             {"unlocked", st.unlocked},
                             {"envoy_counted", st.envoy_counted}};
    }
    j["persons"] = persons;
    j["bloc_lean"] = p.bloc_lean;
    cp_json measures = cp_json::object();
    for (const auto& mk : p.measures) {
        const MeasureState& m = mk.second;
        measures[mk.first] = {{"status", m.status}, {"vote_hour", m.vote_hour}, {"answer_hour", m.answer_hour}, {"retry_hour", m.retry_hour},
                              {"yes", m.yes},       {"voted_yes", m.voted_yes}, {"times", m.times}};
    }
    j["measures"] = measures;
    j["favors"] = p.favors;
    j["ministry_confidence"] = p.ministry_confidence;
    j["pending_decision"] = p.pending_decision;
    j["decisions"] = p.decisions;
    cp_json delayed = cp_json::array();
    for (const DelayedEffect& d : p.delayed)
        delayed.push_back({{"due_hour", d.due_hour}, {"faction", d.faction}, {"source", d.source}, {"text", d.text},
                           {"effects", cp_json::parse(d.effects_json)}, {"condition", d.condition}});
    j["delayed"] = delayed;
    cp_json factors = cp_json::array();
    for (const TownFactor& f : p.town_factors)
        factors.push_back({{"province", f.province}, {"faction", f.faction}, {"label", f.label}, {"value", f.value},
                           {"source", f.source},     {"until_hour", f.until_hour}, {"one_battle", f.one_battle}});
    j["town_factors"] = factors;
    cp_json grants = cp_json::array();
    for (const MonthlyGrant& g : p.grants)
        grants.push_back({{"source", g.source}, {"faction", g.faction}, {"province", g.province}, {"powder", g.powder},
                          {"muskets", g.muskets}, {"provisions", g.provisions}});
    j["grants"] = grants;
    cp_json lures = cp_json::array();
    for (const AiLure& l : p.lures) lures.push_back({{"province", l.province}, {"faction", l.faction}, {"until_hour", l.until_hour}});
    j["lures"] = lures;
    j["scouted_until"] = p.scouted_until;
    j["boats_until"] = p.boats_until;
    j["upkeep_share_pct"] = p.upkeep_share_pct;
    j["price_rise_pct"] = p.price_rise_pct;
    j["flags"] = p.flags;
    j["counted_missions"] = p.counted_missions;
    return j.dump();
}

CampaignPass pass_from_json(const std::string& text) {
    CampaignPass p;
    const cp_json j = cp_json::parse(text);
    p.seeded = j.value("seeded", false);
    if (j.contains("persons"))
        for (auto it = j.at("persons").begin(); it != j.at("persons").end(); ++it) {
            PersonState st;
            st.role = it.value().value("role", "");
            st.army_id = it.value().value("army_id", "");
            st.province = it.value().value("province", "");
            st.bloc = it.value().value("bloc", "");
            st.uses_left = it.value().value("uses_left", -1);
            st.status = it.value().value("status", "");
            st.away_until_hour = it.value().value("away_until_hour", static_cast<int64_t>(0));
            st.seated_hour = it.value().value("seated_hour", static_cast<int64_t>(0));
            st.unlocked = it.value().value("unlocked", false);
            st.envoy_counted = it.value().value("envoy_counted", false);
            p.persons[it.key()] = st;
        }
    p.bloc_lean = j.value("bloc_lean", std::map<std::string, int>{});
    if (j.contains("measures"))
        for (auto it = j.at("measures").begin(); it != j.at("measures").end(); ++it) {
            MeasureState m;
            m.status = it.value().value("status", "");
            m.vote_hour = it.value().value("vote_hour", static_cast<int64_t>(0));
            m.answer_hour = it.value().value("answer_hour", static_cast<int64_t>(0));
            m.retry_hour = it.value().value("retry_hour", static_cast<int64_t>(0));
            m.yes = it.value().value("yes", 0);
            m.voted_yes = it.value().value("voted_yes", false);
            m.times = it.value().value("times", 0);
            p.measures[it.key()] = m;
        }
    p.favors = j.value("favors", 0);
    p.ministry_confidence = j.value("ministry_confidence", 0);
    p.pending_decision = j.value("pending_decision", "");
    p.decisions = j.value("decisions", std::map<std::string, std::string>{});
    if (j.contains("delayed"))
        for (const auto& d : j.at("delayed")) {
            DelayedEffect de;
            de.due_hour = d.value("due_hour", static_cast<int64_t>(0));
            de.faction = d.value("faction", "");
            de.source = d.value("source", "");
            de.text = d.value("text", "");
            de.effects_json = d.contains("effects") ? d.at("effects").dump() : std::string("{}");
            de.condition = d.value("condition", "");
            p.delayed.push_back(de);
        }
    if (j.contains("town_factors"))
        for (const auto& f : j.at("town_factors")) {
            TownFactor tf;
            tf.province = f.value("province", "");
            tf.faction = f.value("faction", "");
            tf.label = f.value("label", "");
            tf.value = f.value("value", 0);
            tf.source = f.value("source", "");
            tf.until_hour = f.value("until_hour", static_cast<int64_t>(0));
            tf.one_battle = f.value("one_battle", false);
            p.town_factors.push_back(tf);
        }
    if (j.contains("grants"))
        for (const auto& g : j.at("grants")) {
            MonthlyGrant mg;
            mg.source = g.value("source", "");
            mg.faction = g.value("faction", "");
            mg.province = g.value("province", "");
            mg.powder = g.value("powder", 0);
            mg.muskets = g.value("muskets", 0);
            mg.provisions = g.value("provisions", 0);
            p.grants.push_back(mg);
        }
    if (j.contains("lures"))
        for (const auto& l : j.at("lures"))
            p.lures.push_back({l.value("province", ""), l.value("faction", ""), l.value("until_hour", static_cast<int64_t>(0))});
    p.scouted_until = j.value("scouted_until", std::map<std::string, int64_t>{});
    p.boats_until = j.value("boats_until", std::map<std::string, int64_t>{});
    p.upkeep_share_pct = j.value("upkeep_share_pct", std::map<std::string, int>{});
    p.price_rise_pct = j.value("price_rise_pct", 0);
    p.flags = j.value("flags", std::vector<std::string>{});
    p.counted_missions = j.value("counted_missions", std::vector<std::string>{});
    return p;
}

}  // namespace detail

// ---- Orders ---------------------------------------------------------------------------

std::string validate_campaign_order(const CampaignState& s, const std::string& order_json) {
    CpOrder o;
    const std::string bad = cp_parse_order(order_json, o);
    return bad.empty() ? cp_validate(s, o) : bad;
}

CampaignState apply_campaign_order(const CampaignState& in, const std::string& order_json, std::string& error) {
    error.clear();
    CpOrder o;
    error = cp_parse_order(order_json, o);
    if (error.empty()) error = cp_validate(in, o);
    if (!error.empty()) return in;
    if (o.order == "halt") return halt_army(in, o.army_id, error);
    CampaignState s = in;
    cp_seed(s);
    const std::string side = s.player_faction;
    const CpPerson* p = o.person.empty() ? nullptr : cp_person(o.person);
    const std::string who = p ? p->rank + " " + p->name : std::string();
    if (o.order == "form_army") {
        std::string army_id = o.army_id;
        if (army_id.empty())
            for (const auto& ak : s.armies) {
                const Army& a = ak.second;
                if (a.faction != side || a.province != o.province || !a.path.empty() || a.role == "allied_company") continue;
                if (!general_of(s, ak.first).empty()) continue;
                // The largest body of men in the town is the army he forms.
                if (army_id.empty() || detail::total_units(a) > detail::total_units(s.armies.at(army_id))) army_id = ak.first;
            }
        PersonState& st = s.pass.persons[o.person];
        st.role = "general";
        st.army_id = army_id;
        cp_note(s, who + " forms an army at " + cp_town(s, o.province) + ": he commands " + s.armies.at(army_id).name + " (up to " +
                       std::to_string(g_cp_prules.army_unit_cap) + " units; it may besiege)",
                0, 0, o.province);
    } else if (o.order == "assign_person") {
        PersonState& st = s.pass.persons[o.person];
        st.role = o.role;
        if (o.role == "organizer") {
            st.province = o.province;
            cp_note(s, who + " goes to " + cp_town(s, o.province) + " as organizer: Support each month, and companies muster there in " +
                           std::to_string(g_cp_prules.organizer_muster_pct) + " of every 100 hours",
                    0, 0, o.province);
        } else if (o.role == "delegate") {
            st.bloc = o.bloc;
            st.seated_hour = s.now.hour_index;
            if (g_cp_prules.delegate_seat_shift != 0) cp_shift_bloc(s, o.bloc, g_cp_prules.delegate_seat_shift);
            const CpBody* body = cp_active_body(s, side);
            const CpBloc* bloc = body ? cp_bloc(*body, o.bloc) : nullptr;
            std::string note = who + " sits as your delegate among the " + (bloc ? bloc->name : o.bloc);
            if (g_cp_prules.delegate_seat_shift != 0)
                note += ": " + std::string(g_cp_prules.delegate_seat_shift > 0 ? "+" : "") +
                        std::to_string(g_cp_prules.delegate_seat_shift) + " now, +" +
                        std::to_string(g_cp_prules.delegate_shift_per_week) + " a week while he stays";
            else
                note += ": +" + std::to_string(g_cp_prules.delegate_shift_per_week) + " a week while he stays";
            cp_note(s, note);
        } else {
            int gain = 0;
            if (!st.envoy_counted && s.relations.count(kBritish) && side != kBritish) {
                const int before = s.relations[kBritish];
                s.relations[kBritish] = detail::clamp_int(before + g_cp_prules.envoy_attitude, -100, 100);
                gain = s.relations[kBritish] - before;
                st.envoy_counted = true;
            }
            cp_note(s, who + " is your envoy for a parley" + (gain != 0 ? ": the British envoy's Attitude +" + std::to_string(gain) : std::string()));
        }
    } else if (o.order == "relieve_person") {
        const std::string role = cp_pstate(s, o.person).role;
        cp_release_person(s, o.person);
        cp_note(s, who + " is relieved as " + role + " and is at hand");
    } else if (o.order == "transfer_units") {
        Army& from = s.armies.at(o.from_army);
        Army& to = s.armies.at(o.to_army);
        int moved = 0, guns_moved = 0;
        for (const auto& u : o.units) {
            from.units[u.first] -= u.second;
            if (from.units[u.first] <= 0) from.units.erase(u.first);
            to.units[u.first] += u.second;
            moved += u.second;
            if (u.first == "artilleryman") {
                const int take = std::min(u.second, std::max(0, from.guns));
                from.guns -= take;
                to.guns += take;
                guns_moved += take;
            }
        }
        const int a = std::max(0, detail::total_units(to) - moved);
        const int b = moved;
        if (a + b > 0) {
            // Morale of the joined men averages into the receiver.
            to.morale = (to.morale * a + from.morale * b) / (a + b);
            to.readiness = (to.readiness * a + from.readiness * b) / (a + b);
        }
        std::string note = std::to_string(moved) + (moved == 1 ? " company" : " companies") + " leave " + from.name +
                           " for " + to.name;
        if (guns_moved > 0) note += " (and " + std::to_string(guns_moved) + " guns)";
        cp_note(s, note, 0, 0, to.province);
        if (detail::total_units(from) <= 0) {
            for (auto& pk : s.pass.persons)
                if (pk.second.role == "general" && pk.second.army_id == o.from_army) cp_release_person(s, pk.first);
            for (auto& pc : s.progress.personnel_commitments)
                if (pc.army_id == o.from_army) pc.army_id = o.to_army;
            for (auto& cj : s.progress.cartridge_jobs)
                if (cj.army_id == o.from_army) cj.army_id = o.to_army;
            s.armies.erase(o.from_army);
        }
    } else if (o.order == "use_agent") {
        const CpAbility& ab = g_cp_abilities.at(p->ability);
        s.pass.persons[o.person].uses_left = cp_uses_left(s, *p) - 1;
        const int left = s.pass.persons[o.person].uses_left;
        const std::string tail = " (" + std::to_string(left) + (left == 1 ? " use left)" : " uses left)");
        if (p->ability == "alarm") {
            TownFactor f;
            f.province = s.pending_battle.province;
            f.faction = side;
            f.label = "alarm";
            f.value = ab.morale;
            f.source = who + "'s alarm";
            f.one_battle = true;
            s.pass.town_factors.push_back(f);
            cp_note(s, who + " raises the alarm at " + cp_town(s, f.province) + ": the companies are under arms" + tail, 0, 0, f.province);
        } else if (p->ability == "scout") {
            s.pass.scouted_until[side + ":" + o.army_id] = s.now.hour_index + 24LL * std::max(1, ab.days);
            cp_note(s, who + " counts " + s.armies.at(o.army_id).name + ": its strength is known for " + std::to_string(ab.days) + " days" + tail);
        } else if (p->ability == "boats") {
            s.pass.boats_until[o.army_id] = s.now.hour_index + 24LL * std::max(1, ab.days);
            cp_note(s, who + " finds boats for " + s.armies.at(o.army_id).name + ": it may cross the water for " + std::to_string(ab.days) + " days" + tail);
        } else if (p->ability == "engineer") {
            TownFactor f;
            f.province = o.province;
            f.faction = side;
            f.label = "fortification";
            f.value = ab.fortification;
            f.source = who + "'s works";
            f.until_hour = s.now.hour_index + 24LL * std::max(1, ab.days);
            s.pass.town_factors.push_back(f);
            cp_note(s, who + " lays out works at " + cp_town(s, o.province) + ": +" + std::to_string(ab.fortification) +
                           " fortification for its defenders for " + std::to_string(ab.days) + " days" + tail,
                    0, 0, o.province);
        } else {
            const int before = s.local_support;
            s.local_support = detail::clamp_int(s.local_support + (side == kContinental ? ab.local_support : -ab.local_support), 0, 100);
            cp_note(s, who + " writes for " + cp_town(s, o.province) + ": Support " + (s.local_support >= before ? "+" : "") +
                           std::to_string(s.local_support - before) + tail,
                    0, s.local_support - before, o.province);
        }
    } else if (o.order == "lobby") {
        const CpBody* body = cp_active_body(s, side);
        const CpBloc* bloc = cp_bloc(*body, o.bloc);
        if (o.action == "write") {
            ++s.staff.letters_processed_today;
            cp_shift_bloc(s, o.bloc, g_cp_lobby.write_shift);
            cp_note(s, "Wrote to the " + bloc->name + ": +" + std::to_string(g_cp_lobby.write_shift) + " (one clerk slot)");
        } else {
            --s.pass.favors;
            cp_shift_bloc(s, o.bloc, g_cp_lobby.favor_shift);
            cp_note(s, "Called in a favor with the " + bloc->name + ": +" + std::to_string(g_cp_lobby.favor_shift) + " (" +
                           std::to_string(s.pass.favors) + (s.pass.favors == 1 ? " favor left)" : " favors left)"));
        }
    } else if (o.order == "vote_now") {
        const CpMeasure* m = cp_measure(o.measure);
        cp_vote(s, *m, *cp_body(m->body));
    } else if (o.order == "send_dispatch") {
        const CpMeasure* m = cp_measure(o.measure);
        cp_send_dispatch(s, *m);
        cp_note(s, "Dispatch sent to London: " + m->title + ". The answer is looked for on " + cp_date_words(s.pass.measures[m->id].answer_hour));
    } else if (o.order == "decide") {
        const CpDecision* d = cp_decision(o.decision);
        for (const CpOption& opt : d->options)
            if (opt.id == o.option) cp_take_decision(s, *d, opt, o.person);
    }
    return s;
}

// ---- HUD snapshots ----------------------------------------------------------------------

std::string persons_json(const CampaignState& s) {
    cp_json out = cp_json::object();
    cp_json list = cp_json::array();
    for (const CpPerson& p : g_cp_persons) {
        if (p.side != s.player_faction) continue;
        const PersonState st = cp_pstate(s, p.id);
        const bool present = cp_person_present(s, p);
        std::string where;
        if (st.status == "wounded") where = "Wounded: back " + cp_date_words(st.away_until_hour);
        else if (st.status == "away") where = "Away: back " + cp_date_words(st.away_until_hour);
        else if (st.status == "killed") where = "Killed";
        else if (st.status == "captured") where = "A prisoner";
        else if (st.status == "recalled") where = "Recalled";
        else if (st.status == "exposed") where = "Exposed as an informer";
        else if (!present) where = p.has_from ? "Expected " + cp_date_words(cp_hour(p.from)) : "Not yet named";
        else if (st.role == "general") where = "Commands " + (s.armies.count(st.army_id) ? s.armies.at(st.army_id).name : st.army_id);
        else if (st.role == "organizer") where = "Organizer at " + cp_town(s, st.province);
        else if (st.role == "delegate") {
            std::string bloc = st.bloc;
            for (const CpBody& b : g_cp_bodies)
                for (const CpBloc& c : b.blocs)
                    if (c.id == st.bloc) bloc = c.name;
            where = "Delegate among the " + bloc;
        } else if (st.role == "envoy") where = "Envoy";
        else where = "At hand";
        cp_json traits = cp_json::array();
        for (const std::string& t : p.traits) {
            auto tit = g_cp_traits.find(t);
            if (tit != g_cp_traits.end()) traits.push_back({{"id", t}, {"label", tit->second.label}, {"text", tit->second.text}});
        }
        cp_json row{{"id", p.id},        {"name", p.name},     {"rank", p.rank},          {"roles", p.roles},   {"role", st.role},
                    {"status", st.status}, {"present", present}, {"where", where},          {"army_id", st.army_id},
                    {"province", st.province}, {"bloc", st.bloc}, {"traits", traits},       {"note", p.note},     {"basis", p.basis}};
        if (!p.ability.empty()) {
            auto ait = g_cp_abilities.find(p.ability);
            row["ability"] = {{"id", p.ability},
                              {"label", ait != g_cp_abilities.end() ? ait->second.label : p.ability},
                              {"where", ait != g_cp_abilities.end() ? ait->second.where : std::string()},
                              {"text", ait != g_cp_abilities.end() ? ait->second.text : std::string()},
                              {"uses_left", cp_uses_left(s, p)},
                              {"uses", p.uses}};
        }
        list.push_back(row);
    }
    out["persons"] = list;
    out["detachment_unit_cap"] = g_cp_prules.detachment_unit_cap;
    out["army_unit_cap"] = g_cp_prules.army_unit_cap;
    return out.dump();
}

std::string politics_json(const CampaignState& s) {
    cp_json out = cp_json::object();
    const CpBody* body = g_cp_politics_loaded ? cp_active_body(s, s.player_faction) : nullptr;
    out["loaded"] = body != nullptr;
    if (!body) return out.dump();
    out["body"] = {{"id", body->id}, {"name", body->name}, {"button", body->button}, {"dispatch", body->dispatch}, {"note", body->note}};
    out["favors"] = s.pass.favors;
    out["unity"] = unity(s);
    out["unity_floor"] = g_cp_lobby.unity_floor;
    out["clerk_slots_left"] = std::max(0, s.staff.capacity_per_day - s.staff.letters_processed_today);
    out["write_shift"] = g_cp_lobby.write_shift;
    out["favor_shift"] = g_cp_lobby.favor_shift;
    out["delegate_shift_per_week"] = g_cp_prules.delegate_shift_per_week;
    out["delegate_seat_shift"] = g_cp_prules.delegate_seat_shift;
    out["delegate_reseat_hours"] = g_cp_prules.delegate_reseat_hours;
    if (s.player_faction == kBritish) {
        out["ministry_confidence"] = s.pass.ministry_confidence;
        out["recall_floor"] = g_cp_lobby.recall_floor;
    }
    cp_json blocs = cp_json::array();
    for (const CpBloc& b : body->blocs) {
        std::string delegate;
        for (const CpPerson& p : g_cp_persons) {
            const PersonState st = cp_pstate(s, p.id);
            if (p.side == body->side && st.role == "delegate" && st.bloc == b.id && st.status.empty()) delegate = p.name;
        }
        blocs.push_back({{"id", b.id}, {"name", b.name}, {"lean", cp_lean(s, b)}, {"moves", b.moves}, {"delegate", delegate}});
    }
    out["blocs"] = blocs;
    cp_json measures = cp_json::array();
    for (const CpMeasure& m : g_cp_measures) {
        if (m.body != body->id) continue;
        const MeasureState ms = detail::map_get(s.pass.measures, m.id);
        std::string status = ms.status, when;
        const bool open = cp_measure_open(s, m);
        if (status.empty()) {
            if (ms.retry_hour > 0 && s.now.hour_index < ms.retry_hour) {
                status = "waiting";
                when = "May be asked again from " + cp_date_words(ms.retry_hour);
            } else if (!open) {
                status = "upcoming";
                when = "Not before " + cp_date_words(cp_hour(m.earliest));
            } else {
                status = "open";
                when = body->dispatch ? "Can be sent now: answer in " + std::to_string(g_cp_lobby.days_out + g_cp_lobby.days_back) + " days"
                                      : "Vote on " + cp_date_words(cp_vote_hour(s, m));
            }
        } else if (status == "sent") {
            when = s.now.hour_index < ms.vote_hour ? "At sea: reaches London " + cp_date_words(ms.vote_hour) + ", answer " + cp_date_words(ms.answer_hour)
                                                   : "Answer at sea: arrives " + cp_date_words(ms.answer_hour);
        } else if (status == "failed") {
            when = ms.retry_hour > 0 ? "Failed: returns " + cp_date_words(ms.retry_hour) : "Failed";
        } else if (status == "passed") {
            when = "Passed";
        }
        cp_json chances = cp_json::object();
        for (const CpBloc& b : body->blocs) chances[b.id] = cp_yes_chance(s, m, b);
        measures.push_back({{"id", m.id},
                            {"title", m.title},
                            {"text", m.text},
                            {"status", status},
                            {"when", when},
                            {"chances", chances},
                            {"if_passed", m.passed.summary + (m.later_days > 0 ? " Later: " + m.later.summary : std::string())},
                            {"if_failed", m.failed.summary},
                            {"can_vote_now", !body->dispatch && status == "open"},
                            {"can_send", body->dispatch && status == "open"},
                            {"answer_hour", ms.answer_hour},
                            {"note", m.note},
                            {"basis", m.basis}});
    }
    out["measures"] = measures;
    out["blocs_to_pass"] = g_cp_lobby.blocs_to_pass;
    return out.dump();
}

std::string decision_json(const CampaignState& s) {
    const CpDecision* d = g_cp_decisions_loaded ? cp_decision(s.pass.pending_decision) : nullptr;
    if (!d) return cp_json{{"pending", false}}.dump();
    cp_json options = cp_json::array();
    for (const CpOption& o : d->options) {
        cp_json candidates = cp_json::array();
        if (!o.commit_role.empty())
            for (const CpPerson& p : g_cp_persons)
                if (p.side == d->side && detail::contains(p.roles, o.commit_role) && cp_person_present(s, p))
                    candidates.push_back({{"id", p.id}, {"name", (p.rank.empty() ? "" : p.rank + " ") + p.name}, {"busy", !cp_pstate(s, p.id).role.empty()}});
        options.push_back({{"id", o.id},
                           {"label", o.label},
                           {"cost", o.cost.summary},
                           {"gain", o.gain.summary},
                           {"later", "After " + std::to_string(o.later_days) + " days: " + o.later.summary},
                           {"later_text", o.later_text},
                           {"commit_role", o.commit_role},
                           {"commit_days", o.commit_days},
                           {"candidates", candidates}});
    }
    return cp_json{{"pending", true}, {"id", d->id},     {"title", d->title}, {"painting", d->painting},
                   {"text", d->text}, {"options", options}, {"note", d->note},   {"basis", d->basis}}
        .dump();
}

MenBand unscouted_men_band(int truth_men, int step_men, int width_pct, int extra_half) {
    MenBand out;
    const int step = std::max(1, step_men);
    const int width = std::max(0, width_pct);
    const int truth = std::max(0, truth_men);
    int half = std::max(step, truth * width / 100);
    if (width > 0 && extra_half > 0) half += extra_half;
    int low = (truth - half) / step * step;
    if (low < step) low = step;
    int high = ((truth + half + step - 1) / step) * step;
    if (high < low + step) high = low + step;
    if (low > truth) low = truth / step * step;
    if (low < 0) low = 0;
    if (high < truth) high = ((truth + step - 1) / step) * step;
    out.low = low;
    out.high = high;
    out.mid = (low + high) / 2;
    return out;
}

int units_as_seen(const CampaignState& s, const std::string& viewer, const std::string& army_id) {
    auto it = s.armies.find(army_id);
    if (it == s.armies.end()) return 0;
    return detail::units_as_seen(s, viewer, it->second);
}

std::string army_intel_json(const CampaignState& s, const std::string& viewer, const std::string& army_id) {
    auto it = s.armies.find(army_id);
    if (it == s.armies.end()) return cp_json{{"known", false}, {"men_low", 0}, {"men_high", 0}, {"source", ""}}.dump();
    const CpIntel in = cp_intel(s, viewer, it->second);
    return cp_json{{"known", in.known}, {"men_low", in.men_low}, {"men_high", in.men_high}, {"source", in.source}}.dump();
}

}  // namespace pa250
