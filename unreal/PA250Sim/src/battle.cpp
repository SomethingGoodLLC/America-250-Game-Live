#include "internal.hpp"
#include "nlohmann/json.hpp"

namespace pa250 {
namespace {

BattleRules g_battle_rules;
bool g_battle_rules_loaded = false;

// The magazine a side has at hand in the fight: the town it holds and fights in. Empty: nothing but
// what the men carry. (Until 2026-11-14 a column also drew on the friendly town it marched from.
// Boxes now fill only while an army stands in a town its side holds: refill_cartridge_boxes.)
std::string side_magazine_province(const Army& army, const Province& ground) {
    return army.faction == ground.owner ? ground.id : std::string();
}

int ticket_total(const std::map<std::string, int>& tickets) {
    int n = 0;
    for (const auto& kv : tickets) n += std::max(0, kv.second);
    return n;
}

// The units an army fields in a battle, and who was stood down and why.
struct Fielded {
    std::map<std::string, int> tickets;                     // per class, the units that fight
    std::vector<std::pair<int, std::string>> stood_down;    // (units, the reason in plain words)
};

// Men under arms keep their arms (user decision 2026-11-14). Until then an army arming from the
// battle town's own magazine was stood down by the muskets and, for militia, the cartridges that
// magazine held. A company is charged its muskets when it is raised (the recruit catalog) and its
// rounds are in its boxes (Army.rounds_per_man), so the magazine was charged twice; and a player
// who emptied Concord's magazine for mission 1 was left with one unit on paper while nineteen of
// his men took the field, all of whom could fall for a reported loss of one unit.
// What still stands men down: low readiness or high fatigue, and personnel commitments.
Fielded fielded_units(const CampaignState& state, const Army& army) {
    Fielded f;
    f.tickets = army.units;  // fit only

    int total = 0;
    for (const auto& kv : f.tickets) total += std::max(0, kv.second);
    if (total <= 0) return f;

    // Readiness / fatigue soft-cap (aggregate, not personal inventory).
    if (army.readiness < 50 || army.fatigue > 60) {
        auto mit = f.tickets.find("militia");
        if (mit != f.tickets.end() && mit->second > 0) {
            const int kept = (mit->second * 3) / 4;
            if (kept < mit->second)
                f.stood_down.push_back({mit->second - kept,
                                        army.readiness < 50 ? "Readiness under 50: a quarter of the militia stand down"
                                                            : "Fatigue over 60: a quarter of the militia stand down"});
            mit->second = kept;
        }
    }

    // Temporary personnel commitments hold paper units out of fielding without touching
    // Army.units, equipment, readiness, or morale.
    for (const auto& pc : state.progress.personnel_commitments) {
        if (pc.army_id != army.id || pc.count <= 0) continue;
        if (pc.release_on_hour > 0 && state.now.hour_index >= pc.release_on_hour) continue;
        auto uit = f.tickets.find(pc.unit_class);
        if (uit == f.tickets.end()) continue;
        const int held = std::min(std::max(0, uit->second), pc.count);
        uit->second -= held;
        if (held > 0) f.stood_down.push_back({held, "Detailed away from the army"});
    }

    // Never stand a still-existing army down to nothing: that made the next fight end on the
    // first Tick as tickets_exhausted before anyone spawned.
    if (ticket_total(f.tickets) <= 0 && total > 0) {
        for (const auto& kv : army.units) {
            if (kv.second <= 0) continue;
            f.tickets[kv.first] = 1;
            break;
        }
        int back = 1;
        for (auto it = f.stood_down.rbegin(); it != f.stood_down.rend() && back > 0; ++it) {
            const int give = std::min(back, it->first);
            it->first -= give;
            back -= give;
        }
    }
    return f;
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
// provincials who actually reached the crossing. With match_defender_units (battle_rules) the
// company matches the provincials when the column is large enough (parity). The cap is shared
// across the column's classes in proportion (largest remainder, class order breaks ties).
void limit_bridge_company(std::map<std::string, int>& british, int provincial_units) {
    int total = 0;
    for (const auto& kv : british) total += std::max(0, kv.second);
    const BattleRules& R = battle_rules();
    int company = total;
    if (R.bridge_match_defender && provincial_units > 0)
        company = std::min(total, provincial_units);
    else if (provincial_units > 0)
        company = std::min(total, std::min(std::max(1, R.bridge_fallback_units), provincial_units));
    else
        company = std::min(total, std::max(1, R.bridge_fallback_units));
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

// Both armies of the pending battle as they take the field (army id -> fielded), with the scenario's
// own rule applied: at Concord only one British company goes down to the bridge.
std::map<std::string, Fielded> fielded_for_battle(const CampaignState& state, const PendingBattle& pb,
                                                  const std::string& province) {
    std::map<std::string, Fielded> out;
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = state.armies.find(aid);
        if (it == state.armies.end()) continue;
        out[aid] = fielded_units(state, it->second);
    }
    if (scenario_preset_for_province(province) == "concord_bridge") {
        int prov = 0;
        std::string brit_id;
        for (const auto& fk : out) {
            const Army& a = state.armies.at(fk.first);
            if (a.faction == kContinental) prov += ticket_total(fk.second.tickets);
            if (a.faction == kBritish) brit_id = fk.first;
        }
        if (!brit_id.empty()) {
            Fielded& brit = out.at(brit_id);
            const int before = ticket_total(brit.tickets);
            limit_bridge_company(brit.tickets, prov);
            const int waiting = before - ticket_total(brit.tickets);
            if (waiting > 0)
                brit.stood_down.push_back({waiting, "North Bridge: one company goes down to the bridge; the column waits in the town"});
        }
    }
    return out;
}

}  // namespace

void load_battle_rules(const std::string& json_text) {
    using nlohmann::json;
    try {
        const json j = json::parse(json_text);
        BattleRules r;
        if (j.contains("field")) {
            const json& f = j.at("field");
            r.bot_budget = f.value("bot_budget", r.bot_budget);
            r.fps_bar = f.value("frame_rate_bar", r.fps_bar);
            r.fps_bar_bots = f.value("frame_rate_bar_bots", r.fps_bar_bots);
            r.field_min = f.value("min_per_side", r.field_min);
            r.field_max = f.value("max_per_side", r.field_max);
            r.soldiers_per_unit = f.value("soldiers_per_unit", r.soldiers_per_unit);
        }
        if (j.contains("rounds")) {
            const json& f = j.at("rounds");
            r.rounds_per_cartridge = f.value("rounds_per_cartridge", r.rounds_per_cartridge);
            r.rounds_min = f.value("min_per_man", r.rounds_min);
            r.rounds_max = f.value("max_per_man", r.rounds_max);
            r.rounds_carried = f.value("carried_per_man", r.rounds_carried);
            r.defeat_drop_pct = f.value("defeat_drop_pct", r.defeat_drop_pct);
            r.rout_drop_pct = f.value("rout_drop_pct", r.rout_drop_pct);
        }
        if (j.contains("casualties"))
            r.wounded_pct_of_fallen = j.at("casualties").value("wounded_pct_of_fallen", r.wounded_pct_of_fallen);
        if (j.contains("siege"))
            r.siege_hours_per_fort_level =
                j.at("siege").value("hours_per_fort_level", r.siege_hours_per_fort_level);
        if (j.contains("army_broke")) {
            const json& f = j.at("army_broke");
            r.army_broke_fraction = f.value("squad_fraction", r.army_broke_fraction);
            r.army_broke_seconds = f.value("seconds", r.army_broke_seconds);
            r.army_broke_min_elapsed_seconds = f.value("min_elapsed_seconds", r.army_broke_min_elapsed_seconds);
            r.army_broke_min_squads = f.value("min_squads", r.army_broke_min_squads);
            r.army_broke_morale_penalty = f.value("morale_penalty", r.army_broke_morale_penalty);
        }
        if (j.contains("morale")) {
            const json& f = j.at("morale");
            r.morale_victory = f.value("victory", r.morale_victory);
            r.morale_defeat = f.value("defeat", r.morale_defeat);
        }
        if (j.contains("posts_swept")) {
            const json& f = j.at("posts_swept");
            r.posts_swept_seconds = f.value("seconds", r.posts_swept_seconds);
            r.posts_swept_min_elapsed_seconds = f.value("min_elapsed_seconds", r.posts_swept_min_elapsed_seconds);
        }
        if (j.contains("modes")) {
            const json& f = j.at("modes");
            r.mode_landing_edge_types = f.value("landing_edge_types", r.mode_landing_edge_types);
            r.mode_landing_town_tag = f.value("landing_town_tag", r.mode_landing_town_tag);
            r.mode_assault_min_fort = std::max(1, f.value("assault_min_fort_level", r.mode_assault_min_fort));
            r.mode_conquest_presets = f.value("conquest_presets", r.mode_conquest_presets);
            if (f.contains("bridge_company")) {
                const json& b = f.at("bridge_company");
                r.bridge_match_defender = b.value("match_defender_units", r.bridge_match_defender);
                r.bridge_fallback_units = std::max(1, b.value("fallback_units", r.bridge_fallback_units));
            }
            if (f.contains("assault")) {
                const json& a = f.at("assault");
                r.assault_seconds_per_post = a.value("seconds_per_post_carried", r.assault_seconds_per_post);
                r.assault_garrison_share_pct = detail::clamp_int(a.value("garrison_share_pct", r.assault_garrison_share_pct), 0, 100);
            }
            if (f.contains("landing")) {
                const json& l = f.at("landing");
                r.landing_boats = std::max(1, l.value("boats", r.landing_boats));
                r.landing_men_per_boat = std::max(1, l.value("men_per_boat", r.landing_men_per_boat));
                r.landing_boat_speed_cm = l.value("boat_speed_cm", r.landing_boat_speed_cm);
                r.landing_row_out_m = l.value("row_out_m", r.landing_row_out_m);
                r.landing_disembark_seconds = l.value("disembark_seconds", r.landing_disembark_seconds);
                r.landing_player_wait_seconds = l.value("player_wait_seconds", r.landing_player_wait_seconds);
                r.landing_boat_spacing_cm = l.value("boat_spacing_cm", r.landing_boat_spacing_cm);
            }
        }
        if (j.contains("guns")) {
            const json& f = j.at("guns");
            r.guns_crew_units_per_gun = std::max(1, f.value("crew_units_per_gun", r.guns_crew_units_per_gun));
            r.guns_crew_class = f.value("crew_class", r.guns_crew_class);
        }
        if (j.contains("colours")) {
            const json& f = j.at("colours");
            r.colours_support_per_capture = f.value("support_per_capture", r.colours_support_per_capture);
            r.colours_class = f.value("carried_by_class", r.colours_class);
        }
        if (j.contains("cannon")) {
            const json& f = j.at("cannon");
            r.cannon_crew_men = std::max(1, f.value("crew_men", r.cannon_crew_men));
            r.cannon_min_crew = std::max(1, f.value("min_crew", r.cannon_min_crew));
            r.cannon_sponge_load_seconds = f.value("sponge_load_seconds", r.cannon_sponge_load_seconds);
            r.cannon_ram_seconds = f.value("ram_seconds", r.cannon_ram_seconds);
            r.cannon_prime_lay_seconds = f.value("prime_lay_seconds", r.cannon_prime_lay_seconds);
            r.cannon_untrained_scale = f.value("untrained_drill_scale", r.cannon_untrained_scale);
            r.cannon_traverse_deg_per_second = f.value("traverse_deg_per_second", r.cannon_traverse_deg_per_second);
            r.cannon_elevation_min_deg = f.value("elevation_min_deg", r.cannon_elevation_min_deg);
            r.cannon_elevation_max_deg = f.value("elevation_max_deg", r.cannon_elevation_max_deg);
            r.cannon_elevation_deg_per_second = f.value("elevation_deg_per_second", r.cannon_elevation_deg_per_second);
            r.cannon_round_range_m = f.value("round_shot_range_m", r.cannon_round_range_m);
            r.cannon_round_speed_m = f.value("round_shot_speed_m", r.cannon_round_speed_m);
            r.cannon_round_damage = f.value("round_shot_damage", r.cannon_round_damage);
            r.cannon_round_spread_degrees = f.value("round_shot_spread_degrees", r.cannon_round_spread_degrees);
            r.cannon_round_pierce = f.value("round_shot_pierce_men", r.cannon_round_pierce);
            r.cannon_knockdown_radius_cm = f.value("knockdown_radius_cm", r.cannon_knockdown_radius_cm);
            r.cannon_knockdown_seconds = f.value("knockdown_seconds", r.cannon_knockdown_seconds);
            r.cannon_works_damage = f.value("works_damage", r.cannon_works_damage);
            r.cannon_pressure = f.value("morale_pressure", r.cannon_pressure);
            r.cannon_pressure_seconds = f.value("morale_seconds", r.cannon_pressure_seconds);
            // Grape (player-facing); older files still say canister_*.
            r.cannon_grape_range_m = f.value("grape_range_m", f.value("canister_range_m", r.cannon_grape_range_m));
            r.cannon_grape_balls = f.value("grape_balls", f.value("canister_balls", r.cannon_grape_balls));
            r.cannon_grape_cone_degrees = f.value("grape_cone_degrees", f.value("canister_cone_degrees", r.cannon_grape_cone_degrees));
            r.cannon_shell_range_m = f.value("shell_range_m", r.cannon_shell_range_m);
            r.cannon_shell_speed_m = f.value("shell_speed_m", r.cannon_shell_speed_m);
            r.cannon_shell_fuse_min_m = f.value("shell_fuse_min_m", r.cannon_shell_fuse_min_m);
            r.cannon_shell_blast_radius_cm = f.value("shell_blast_radius_cm", r.cannon_shell_blast_radius_cm);
            r.cannon_shell_damage = f.value("shell_damage", r.cannon_shell_damage);
            r.cannon_shell_works_damage = f.value("shell_works_damage", r.cannon_shell_works_damage);
            r.cannon_shell_spread_degrees = f.value("shell_spread_degrees", r.cannon_shell_spread_degrees);
            r.cannon_push_speed_cm = f.value("push_speed_cm", r.cannon_push_speed_cm);
            r.cannon_follow_gap_m = f.value("follow_gap_m", r.cannon_follow_gap_m);
            r.cannon_capture_radius_cm = f.value("capture_radius_cm", r.cannon_capture_radius_cm);
            r.cannon_capture_seconds = f.value("capture_seconds", r.cannon_capture_seconds);
            r.cannon_crew_radius_cm = f.value("crew_radius_cm", r.cannon_crew_radius_cm);
            r.cannon_length_cm = f.value("length_cm", r.cannon_length_cm);
            r.cannon_knockdown_push_cm = f.value("knockdown_push_cm", r.cannon_knockdown_push_cm);
            r.cannon_dirt_burst_scale = f.value("dirt_burst_scale", r.cannon_dirt_burst_scale);
        }
        if (j.contains("cover")) {
            const json& c = j.at("cover");
            r.cover_snap_cm = c.value("snap_cm", r.cover_snap_cm);
            r.cover_slot_spacing_cm = c.value("slot_spacing_cm", r.cover_slot_spacing_cm);
            r.cover_stand_off_cm = c.value("stand_off_cm", r.cover_stand_off_cm);
            r.cover_search_cm = c.value("search_cm", r.cover_search_cm);
            r.cover_max_angle_deg = c.value("max_angle_deg", r.cover_max_angle_deg);
            r.cover_before_contact = c.value("before_contact", r.cover_before_contact);
            r.cover_line_troops = c.value("line_troops", r.cover_line_troops);
            if (r.cover_line_troops != "never" && r.cover_line_troops != "holding" && r.cover_line_troops != "always")
                throw std::runtime_error("battle rules: cover.line_troops must be never, holding or always");
        }
        if (j.contains("works")) {
            const json& f = j.at("works");
            r.works_breastwork_length_cm = f.value("breastwork_length_cm", r.works_breastwork_length_cm);
            r.works_breastwork_height_cm = f.value("breastwork_height_cm", r.works_breastwork_height_cm);
            r.works_breastwork_thickness_cm = f.value("breastwork_thickness_cm", r.works_breastwork_thickness_cm);
            r.works_breastwork_health = f.value("breastwork_health", r.works_breastwork_health);
            r.works_redoubt_side_cm = f.value("redoubt_side_cm", r.works_redoubt_side_cm);
            r.works_redoubt_height_cm = f.value("redoubt_height_cm", r.works_redoubt_height_cm);
            r.works_redoubt_health = f.value("redoubt_health", r.works_redoubt_health);
            r.works_cover_built_share = f.value("cover_built_share", r.works_cover_built_share);
            r.works_man_radius_cm = f.value("man_radius_cm", r.works_man_radius_cm);
            r.works_slot_ring_cm = f.value("slot_ring_cm", r.works_slot_ring_cm);
            r.works_breastwork_slots = f.value("breastwork_slots", r.works_breastwork_slots);
            r.works_breastworks_by_level = f.value("breastworks_by_level", r.works_breastworks_by_level);
            r.works_redoubts_by_level = f.value("redoubts_by_level", r.works_redoubts_by_level);
        }
        if (j.contains("smoke")) {
            const json& f = j.at("smoke");
            r.smoke_sight_block_opacity = f.value("sight_block_opacity", r.smoke_sight_block_opacity);
            r.smoke_sight_radius_scale = f.value("sight_radius_scale", r.smoke_sight_radius_scale);
        }
        if (j.contains("sound")) {
            const json& f = j.at("sound");
            r.sound_speed_m_per_second = f.value("speed_m_per_second", r.sound_speed_m_per_second);
            r.sound_no_delay_within_m = f.value("no_delay_within_m", r.sound_no_delay_within_m);
        }
        if (j.contains("duel")) {
            const json& f = j.at("duel");
            r.duel_shove_range_cm = f.value("shove_range_cm", r.duel_shove_range_cm);
            r.duel_shove_recover_seconds = f.value("shove_recover_seconds", r.duel_shove_recover_seconds);
            r.duel_shove_stagger_seconds = f.value("shove_stagger_seconds", r.duel_shove_stagger_seconds);
            r.duel_shove_open_seconds = f.value("shove_open_seconds", r.duel_shove_open_seconds);
            r.duel_shove_push_cm = f.value("shove_push_cm", r.duel_shove_push_cm);
            r.duel_shove_cooldown_seconds = f.value("shove_cooldown_seconds", r.duel_shove_cooldown_seconds);
        }
        if (j.contains("remnant")) {
            const json& f = j.at("remnant");
            r.remnant_men = f.value("men", r.remnant_men);
            r.remnant_mark_men = f.value("mark_men", r.remnant_mark_men);
            r.remnant_seconds = f.value("seconds", r.remnant_seconds);
        }
        if (j.contains("resupply")) {
            const json& f = j.at("resupply");
            r.resupply_seconds = f.value("seconds_per_refill", r.resupply_seconds);
            r.resupply_held_boxes = f.value("held_for_player_boxes", r.resupply_held_boxes);
        }
        r.soldiers_per_unit = std::max(1, r.soldiers_per_unit);
        r.rounds_per_cartridge = std::max(1, r.rounds_per_cartridge);
        r.wounded_pct_of_fallen = detail::clamp_int(r.wounded_pct_of_fallen, 0, 100);
        r.defeat_drop_pct = detail::clamp_int(r.defeat_drop_pct, 0, 100);
        r.rout_drop_pct = detail::clamp_int(r.rout_drop_pct, 0, 100);
        r.field_min = std::max(0, r.field_min);
        r.field_max = std::max(r.field_min, r.field_max);
        g_battle_rules = r;
        g_battle_rules_loaded = true;
    } catch (...) {
        g_battle_rules = BattleRules{};
        g_battle_rules_loaded = false;
    }
}

bool battle_rules_loaded() { return g_battle_rules_loaded; }
const BattleRules& battle_rules() { return g_battle_rules; }

bool known_end_reason(const std::string& reason) {
    for (const char* k : {"tickets_exhausted", "time_limit", "force_preserved", "routed", "army_broke",
                          "posts_swept"})
        if (reason == k) return true;
    return false;
}

FieldSplit split_field_strength(int men_a, int men_b) {
    const BattleRules& r = battle_rules();
    men_a = std::max(0, men_a);
    men_b = std::max(0, men_b);
    FieldSplit f;
    const long total = static_cast<long>(men_a) + men_b;
    if (total <= 0) return f;
    // Round half up, then give the other side the rest of the budget.
    const int share_a = static_cast<int>((2L * r.bot_budget * men_a + total) / (2L * total));
    const int share_b = r.bot_budget - share_a;
    // Never more soldiers on the field than the army has.
    f.a = std::min(men_a, detail::clamp_int(share_a, r.field_min, r.field_max));
    f.b = std::min(men_b, detail::clamp_int(share_b, r.field_min, r.field_max));
    return f;
}

int rounds_per_man_for(int cartridges, int men) {
    const BattleRules& r = battle_rules();
    if (men <= 0) return r.rounds_min;
    const long rounds = static_cast<long>(std::max(0, cartridges)) * r.rounds_per_cartridge / men;
    return detail::clamp_int(static_cast<int>(std::min<long>(rounds, r.rounds_max)), r.rounds_min, r.rounds_max);
}

MenDown split_fallen(int fallen) {
    MenDown m;
    fallen = std::max(0, fallen);
    m.wounded = (fallen * battle_rules().wounded_pct_of_fallen + 50) / 100;
    m.killed = fallen - m.wounded;
    return m;
}

int carried_rounds(const Army& army) {
    const BattleRules& r = battle_rules();
    return army.rounds_per_man < 0 ? r.rounds_carried : std::min(army.rounds_per_man, r.rounds_max);
}

std::string choose_battle_mode(const std::string& scenario_preset, const std::string& arrival_edge_type,
                               const std::vector<std::string>& terrain_tags, int defender_fortification_level) {
    const BattleRules& r = battle_rules();
    if (detail::contains(r.mode_conquest_presets, scenario_preset)) return "conquest";
    if (!arrival_edge_type.empty() && detail::contains(r.mode_landing_edge_types, arrival_edge_type) &&
        detail::contains(terrain_tags, r.mode_landing_town_tag))
        return "landing";
    if (defender_fortification_level >= r.mode_assault_min_fort) return "assault";
    return "conquest";
}

int served_guns(const Army& army) {
    const BattleRules& r = battle_rules();
    const int crews = std::max(0, detail::map_get(army.units, r.guns_crew_class)) / std::max(1, r.guns_crew_units_per_gun);
    return std::min(std::max(0, army.guns), crews);
}

void draw_guns(CampaignState& s) {
    const BattleRules& r = battle_rules();
    for (auto& ak : s.armies) {
        Army& a = ak.second;
        if (a.hours_into_edge > 0) continue;  // on the road
        auto pit = s.provinces.find(a.province);
        if (pit == s.provinces.end() || pit->second.owner != a.faction) continue;
        const int crews = std::max(0, detail::map_get(a.units, r.guns_crew_class)) / std::max(1, r.guns_crew_units_per_gun);
        const int spare = crews - std::max(0, a.guns);
        if (spare <= 0) continue;
        auto sit = s.province_stores.find(a.province);
        if (sit == s.province_stores.end() || sit->second.guns <= 0) continue;
        const int taken = std::min(spare, sit->second.guns);
        sit->second.guns -= taken;
        a.guns += taken;
        detail::add_log(s, ak.first + " draws " + std::to_string(taken) + " guns at " + a.province);
        if (a.faction != s.player_faction) continue;  // the enemy's parks are not the player's books
        ResourceChange ch;
        ch.reason = a.name + " takes " + std::to_string(taken) + (taken == 1 ? " field gun" : " field guns") + " from the magazine at " +
                    pit->second.name + ": its crews can serve them";
        ch.province_id = a.province;
        detail::record_resource_change(s, ch);
    }
}

void refill_cartridge_boxes(CampaignState& s) {
    const BattleRules& r = battle_rules();
    const int per_powder = std::max(1, cartridges_per_powder());
    for (auto& ak : s.armies) {
        Army& a = ak.second;
        if (a.rounds_per_man < 0 || a.rounds_per_man >= r.rounds_carried) continue;  // full boxes
        if (a.hours_into_edge > 0) continue;                                         // on the road
        auto pit = s.provinces.find(a.province);
        if (pit == s.provinces.end() || pit->second.owner != a.faction) continue;
        const int men = std::max(0, detail::total_units(a)) * r.soldiers_per_unit;
        if (men <= 0) continue;
        ProvinceStores& st = province_ledger(s, a.province);
        const long stock = static_cast<long>(std::max(0, st.cartridges)) +
                           static_cast<long>(std::max(0, st.powder)) * per_powder;
        // Every man gets the same: whole rounds a man, as far as the magazine goes.
        const int gain = static_cast<int>(
            std::min<long>(r.rounds_carried - a.rounds_per_man, stock * r.rounds_per_cartridge / men));
        if (gain <= 0) continue;
        const int cost = static_cast<int>((static_cast<long>(gain) * men + r.rounds_per_cartridge - 1) /
                                          r.rounds_per_cartridge);
        const int from_cartridges = std::min(cost, std::max(0, st.cartridges));
        const int powder_made_up = (cost - from_cartridges + per_powder - 1) / per_powder;
        st.cartridges -= from_cartridges;
        st.powder -= powder_made_up;
        a.rounds_per_man += gain;
        // The enemy's magazines are not the player's books (as with an enemy Powder Mill).
        if (a.faction != s.player_faction) continue;
        ResourceChange ch;
        ch.reason = pit->second.name + " magazine fills the boxes of " + a.name + ": -" +
                    std::to_string(from_cartridges) + " cartridges" +
                    (powder_made_up > 0 ? ", -" + std::to_string(powder_made_up) + " powder made up" : std::string()) +
                    " (now " + std::to_string(a.rounds_per_man) + " rounds a man)";
        ch.cartridges = -from_cartridges;
        ch.powder = -powder_made_up;
        ch.province_id = a.province;
        detail::record_resource_change(s, ch);
    }
}

void tick_sieges(CampaignState& s) {
    for (auto& pk : s.provinces) {
        Province& p = pk.second;
        if (p.siege_by.empty()) {
            p.siege_hours_remaining = 0;
            continue;
        }
        bool besieger_present = false;
        for (const auto& ak : s.armies)
            if (ak.second.province == pk.first && ak.second.faction == p.siege_by &&
                detail::total_units(ak.second) > 0 && detail::army_can_besiege(s, ak.second))
                besieger_present = true;
        if (p.owner == p.siege_by || !besieger_present) {
            if (p.owner != p.siege_by)
                detail::add_log(s, "siege of " + p.name + " lifted: the besiegers are gone");
            p.siege_by.clear();
            p.siege_hours_remaining = 0;
            continue;
        }
        // A relief force of the owner's faction standing in the town contests it: the clock
        // holds (neither lifts nor falls) until a battle there settles who stands.
        bool owner_present = false;
        for (const auto& ak : s.armies)
            if (ak.second.province == pk.first && ak.second.faction == p.owner &&
                detail::total_units(ak.second) > 0)
                owner_present = true;
        if (owner_present) continue;
        if (--p.siege_hours_remaining > 0) continue;
        const std::string before = p.owner;
        p.owner = p.siege_by;
        p.siege_by.clear();
        p.siege_hours_remaining = 0;
        ResourceChange ch;
        ch.reason = p.name + " falls after the siege: now " + detail::faction_word(p.owner) + " (was " + detail::faction_word(before) + ")";
        ch.province_id = pk.first;
        detail::record_resource_change(s, ch);
        s.memory.push_back({s.now.hour_index, p.owner, "Siege of " + p.name + " ended: taken by " + p.owner});
    }
}

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
    b.hour_of_day = static_cast<int>(to_ymdh(state.now).h);  // the light of the day; display only
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
    std::string arrival_edge_type;
    if (!ait->second.previous_province.empty()) {
        for (const auto& ek : state.edges) {
            const Edge& e = ek.second;
            const bool connects = (e.a == ait->second.previous_province && e.b == province) ||
                                  (e.b == ait->second.previous_province && e.a == province);
            if (!connects) continue;
            arrival_edge_type = e.type;
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
    const BattleRules& rules = battle_rules();
    b.soldiers_per_unit = rules.soldiers_per_unit;
    const Province& ground = pit->second;
    // The field is shared out over the men each side actually fields, so a side never has more men
    // on the field than it has to field (its tickets are its reserve and its loss ledger).
    const std::map<std::string, Fielded> fielded = fielded_for_battle(state, pb, province);
    const int men_attacker = ticket_total(fielded.at(pb.attacker_army).tickets) * rules.soldiers_per_unit;
    const int men_defender = ticket_total(fielded.at(pb.defender_army).tickets) * rules.soldiers_per_unit;
    const FieldSplit split = split_field_strength(men_attacker, men_defender);
    for (const Army* a : {&ait->second, &dit->second}) {
        SideSetup s;
        s.faction = a->faction;
        s.army_id = a->id;
        const bool owns_ground = a->faction == ground.owner;
        const bool is_attacker = a == &ait->second;
        const int extra_cart = owns_ground ? workshop_bonus : 0;
        const int units = detail::total_units(*a);
        // The men who fight (the rounds are shared among these).
        const int men = is_attacker ? men_attacker : men_defender;
        auto factor = [&s](const char* label, int value, const std::string& source) {
            s.factors.push_back({label, value, source});
        };
        const Fielded& fld = fielded.at(a->id);
        s.tickets = fld.tickets;
        // The army's whole strength first, then whoever was stood down, each with the reason.
        factor("men", units * rules.soldiers_per_unit,
               a->name + ": " + std::to_string(units) + " units of " + std::to_string(rules.soldiers_per_unit));
        for (const auto& out : fld.stood_down)
            if (out.first > 0) factor("men", -out.first * rules.soldiers_per_unit, out.second);
        s.field_strength = is_attacker ? split.a : split.b;
        factor("field_strength", s.field_strength,
               std::to_string(men) + " of " + std::to_string(men_attacker + men_defender) +
                   " men on the field: share of " + std::to_string(rules.bot_budget) + ", " +
                   std::to_string(rules.field_min) + " to " + std::to_string(rules.field_max) + " a side");
        s.morale = a->morale;
        s.readiness = a->readiness;
        factor("morale", a->morale, a->name);
        factor("readiness", a->readiness, a->name);
        // Fortifications help whoever holds the province defend it.
        if (owns_ground && ground.fort_level > 0) {
            const std::string fort = "Fort L" + std::to_string(ground.fort_level) + ", " + ground.name;
            s.morale = detail::clamp_int(s.morale + 3 * ground.fort_level, 0, 100);
            s.readiness = detail::clamp_int(s.readiness + 2 * ground.fort_level, 0, 100);
            s.fortification_level = ground.fort_level;
            factor("morale", 3 * ground.fort_level, fort);
            factor("readiness", 2 * ground.fort_level, fort);
            factor("fortification", ground.fort_level, fort);
        } else {
            factor("fortification", 0, owns_ground ? "No works at " + ground.name : "Enemy ground: no works");
        }
        // Meeting House: defender on its own ground only (matches the catalog text and the note).
        if (owns_ground && a->faction == b.defender_faction) {
            const int church_morale = building_battle_morale(ground);
            if (church_morale != 0) {
                s.morale = detail::clamp_int(s.morale + church_morale, 0, 100);
                factor("morale", church_morale, "Meeting House, " + ground.name);
            }
        }
        // Farms/depots stock the magazine and steady the force that fights here.
        if (owns_ground && logistics_ready > 0) {
            s.readiness = detail::clamp_int(s.readiness + logistics_ready, 0, 100);
            s.reinforcement_scale = std::max(0.75f, reinforce);
            if (farm_ready > 0)
                factor("readiness", farm_ready, "Farm L" + std::to_string(ground.farm_level) + ", " + ground.name);
            if (depot_ready > 0)
                factor("readiness", depot_ready,
                       "Depot L" + std::to_string(ground.depot_level) + ", " + ground.name);
            factor("reinforcement", static_cast<int>((1.f - s.reinforcement_scale) * 100.f + 0.5f),
                   "Farm L" + std::to_string(ground.farm_level) + " / Depot L" +
                       std::to_string(ground.depot_level) + ", " + ground.name + ": percent faster");
        }
        if (b.militia_muster && a->faction == b.defender_faction)
            factor("militia_muster", 1, "Militia Muster, " + ground.name + ": more companies march in");
        // Ammunition. The men fight from their own cartridge boxes (Army.rounds_per_man). A side on
        // ground it holds also has that town's magazine at hand: it tops the boxes up at form-up and
        // feeds the fight, and is charged for what is fired. A column has only what it carries.
        s.magazine_province = side_magazine_province(*a, ground);
        const int box = carried_rounds(*a);
        // The magazine's powder is made up into cartridges as the army forms up (Phase 4).
        const int made_up = std::max(0, magazine.powder) * cartridges_per_powder();
        if (s.magazine_province == province) {
            s.cartridge_budget = stored_cartridges + made_up + extra_cart;
            const int base_rounds = rounds_per_man_for(stored_cartridges, men);
            const int with_powder = rounds_per_man_for(stored_cartridges + made_up, men);
            s.rounds_per_man = rounds_per_man_for(s.cartridge_budget, men);
            factor("rounds_per_man", base_rounds,
                   ground.name + " magazine: " + std::to_string(stored_cartridges) + " cartridges for " +
                       std::to_string(men) + " men");
            if (made_up > 0)
                factor("rounds_per_man", with_powder - base_rounds,
                       ground.name + " magazine: " + std::to_string(magazine.powder) + " powder made up into " +
                           std::to_string(made_up) + " cartridges at form-up" +
                           (ground.powder_mill_level > 0
                                ? " (Powder Mill L" + std::to_string(ground.powder_mill_level) + ")"
                                : std::string()));
            if (extra_cart > 0)
                factor("rounds_per_man", s.rounds_per_man - with_powder,
                       "Workshop L" + std::to_string(ground.workshop_level) + ", " + ground.name + ": +" +
                           std::to_string(extra_cart) + " cartridges");
            // A thin magazine does not empty the boxes the men already carry.
            if (box > s.rounds_per_man) {
                factor("rounds_per_man", box - s.rounds_per_man,
                       a->name + ": the men already carry " + std::to_string(box) +
                           " rounds each; the magazine holds less");
                s.rounds_per_man = box;
            }
        } else {
            s.cartridge_budget = 0;
            s.rounds_per_man = box;
            factor("rounds_per_man", box,
                   a->name + ": what the men carry in their boxes. They fill only in a town their side holds");
            if (box < rules.rounds_min) {
                factor("rounds_per_man", rules.rounds_min - box,
                       "Scraped together: never fewer than " + std::to_string(rules.rounds_min) + " rounds a man");
                s.rounds_per_man = rules.rounds_min;
            }
        }
        // Field guns: what the army has crews to serve. Guns without crews stay in the park.
        s.guns = served_guns(*a);
        if (a->guns > 0 || s.guns > 0) {
            const int crews = std::max(0, detail::map_get(a->units, rules.guns_crew_class)) / std::max(1, rules.guns_crew_units_per_gun);
            factor("guns", s.guns,
                   a->name + ": " + std::to_string(a->guns) + (a->guns == 1 ? " field gun, " : " field guns, ") +
                       std::to_string(crews) + (crews == 1 ? " crew to serve them" : " crews to serve them"));
        }
        s.retreat_province = choose_retreat(state, province, a->faction);
        // Campaign pass: the general and his traits, an agent's alarm or works, a letter's or a
        // decision's factor at this town. Each is a row with its source.
        detail::pass_side_setup(state, ground, *a, is_attacker, owns_ground, s);
        b.sides[a->faction] = s;
    }
    detail::pass_setup_done(state, b);
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
            ": +" + std::to_string(workshop_bonus) + " cartridges for the owner's men");
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
    // The kind of fight: over the water into a harbour town, against works, or in the open.
    {
        const auto def = b.sides.find(b.defender_faction);
        b.mode = choose_battle_mode(b.scenario_preset, arrival_edge_type, pit->second.terrain,
                                    def != b.sides.end() ? def->second.fortification_level : 0);
    }
    return b;
}

CampaignState apply_battle_result(const CampaignState& in, const BattleResult& r) {
    const PendingBattle pb = in.pending_battle;
    // A result that does not match the pending battle is not a command: leave state, log and hash alone.
    if (!pb.active || pb.province != r.province_id) return in;
    if (r.winner != kBritish && r.winner != kContinental) return in;
    // An end reason outside the contract is not a command either.
    if (!known_end_reason(r.end_reason)) return in;
    for (const auto& ck : r.colours_captured)
        if (ck.second < 0 || (ck.first != kBritish && ck.first != kContinental)) return in;
    for (const auto& ck : r.guns_taken)
        if (ck.second < 0 || (ck.first != kBritish && ck.first != kContinental)) return in;
    // Nor is a negative count of men.
    for (const auto& fk : r.casualties)
        for (const auto& ck : fk.second)
            if (ck.second.killed < 0 || ck.second.wounded < 0 || ck.second.prisoners < 0) return in;
    const BattleRules& rules = battle_rules();
    const bool army_broke = r.end_reason == "army_broke";
    CampaignState s = in;
    ensure_logistics_defaults(s);
    const std::string loser = detail::other_faction(r.winner);
    const int support_before = s.local_support;
    const int gold_before = s.player_gold;
    const std::string owner_before = s.provinces.at(r.province_id).owner;

    std::map<std::string, int> fielded_by_army;
    for (const auto& fk : fielded_for_battle(in, pb, r.province_id))
        fielded_by_army[fk.first] = ticket_total(fk.second.tickets);

    // ---- Casualties, in men ----
    // The battle counts men (BattleResult.casualties): every man who fell is killed, wounded or a
    // prisoner. Older results count units (losses, wounded, prisoners), which is the same thing in
    // whole units, so one path takes both. A class never loses more men than it has standing
    // (killed first, then wounded, then prisoners). Whole units leave the ranks; the men left over
    // stay on the army (Army.men_down) until a later battle makes them up to a unit.
    const int spu = std::max(1, rules.soldiers_per_unit);
    const bool in_men = !r.casualties.empty();
    std::map<std::string, std::map<std::string, MenDown>> fallen;
    if (in_men) {
        fallen = r.casualties;
    } else {
        for (const auto& fk : r.losses)
            for (const auto& ck : fk.second)
                if (ck.second > 0) fallen[fk.first][ck.first].killed += ck.second * spu;
        for (const auto& fk : r.wounded)
            for (const auto& ck : fk.second)
                if (ck.second > 0) fallen[fk.first][ck.first].wounded += ck.second * spu;
        for (const auto& fk : r.prisoners)
            for (const auto& ck : fk.second)
                if (ck.second > 0) fallen[fk.first][ck.first].prisoners += ck.second * spu;
    }
    std::map<std::string, std::map<std::string, MenDown>> men_taken;                 // as applied
    std::map<std::string, std::map<std::string, int>> units_killed, units_wounded;   // whole units out
    std::map<std::string, std::map<std::string, int>> prisoners_taken;
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = s.armies.find(aid);
        if (it == s.armies.end()) continue;
        Army& a = it->second;
        auto fit = fallen.find(a.faction);
        if (fit == fallen.end()) continue;
        for (const auto& ck : fit->second) {
            auto uit = a.units.find(ck.first);
            if (uit == a.units.end() || uit->second <= 0) continue;  // a class this army does not have
            MenDown& down = a.men_down[ck.first];
            int standing = std::max(0, uit->second * spu - down.total());
            MenDown m;
            m.killed = detail::clamp_int(ck.second.killed, 0, standing);
            standing -= m.killed;
            m.wounded = detail::clamp_int(ck.second.wounded, 0, standing);
            standing -= m.wounded;
            m.prisoners = detail::clamp_int(ck.second.prisoners, 0, standing);
            if (m.total() <= 0) continue;
            men_taken[a.faction][ck.first] = m;
            down.killed += m.killed;
            down.wounded += m.wounded;
            down.prisoners += m.prisoners;
            // A unit takes the fate most of its men met (killed, then wounded, then prisoners on a
            // tie); its men come off that count first and off the larger of the others after.
            while (down.total() >= spu && uit->second > 0) {
                int* count[3] = {&down.killed, &down.wounded, &down.prisoners};
                int fate = 0;
                for (int i = 1; i < 3; ++i)
                    if (*count[i] > *count[fate]) fate = i;
                int need = spu;
                int from = fate;
                while (need > 0) {
                    const int take = std::min(need, *count[from]);
                    *count[from] -= take;
                    need -= take;
                    if (need <= 0) break;
                    from = 0;
                    for (int i = 1; i < 3; ++i)
                        if (*count[i] > *count[from]) from = i;
                    if (*count[from] <= 0) break;
                }
                --uit->second;
                if (fate == 0) {
                    ++units_killed[a.faction][ck.first];
                } else if (fate == 1) {
                    ++a.wounded[ck.first];
                    ++units_wounded[a.faction][ck.first];
                } else {
                    ++prisoners_taken[a.faction][ck.first];
                    ++s.prisoners_held[detail::other_faction(a.faction)];
                }
            }
            if (uit->second <= 0) down = MenDown{};  // nobody of the class is left to be down
        }
        for (auto dit = a.men_down.begin(); dit != a.men_down.end();)
            dit = dit->second.total() > 0 ? std::next(dit) : a.men_down.erase(dit);
    }
    // "By your own hand" never exceeds the enemy's killed and wounded (it is counted in men).
    int personal_kills = std::max(0, r.personal_kills);
    if (in_men) {
        int enemy_fallen = 0;
        auto eit = men_taken.find(detail::other_faction(s.player_faction));
        if (eit != men_taken.end())
            for (const auto& ck : eit->second) enemy_fallen += ck.second.killed + ck.second.wounded;
        personal_kills = std::min(personal_kills, enemy_fallen);
    }

    // What each side fired, in rounds. A side on ground it holds is fed by that town's magazine and
    // the magazine is charged; what the magazine cannot cover, and all a column fires, comes out
    // of the men's boxes. Powder wasted in misfires comes from the battle province. No free refill.
    const BattleSetup formed = build_battle_setup(in, r.province_id);
    ProvinceStores& pst = province_ledger(s, r.province_id);
    const int cart_before = pst.cartridges;
    const int powder_before = pst.powder;
    int total_powder_spent = 0;
    for (const auto& kv : r.powder_spent) total_powder_spent += std::max(0, kv.second);
    bool any_claim = false;
    for (const auto& kv : r.shots_fired) any_claim = any_claim || kv.second > 0;
    for (const auto& kv : r.cartridges_spent) any_claim = any_claim || kv.second > 0;
    // A side's claim in magazine cartridges: live shots (rounds, rounds_per_cartridge to a
    // cartridge, rounded up) when the battle reported them; else cartridges_spent; else, when the
    // result claims nothing at all, two per unit fielded.
    std::map<std::string, int> claim_by_faction;  // the side that draws on the battle province
    std::map<std::string, long> rounds_fired;     // every side
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = in.armies.find(aid);
        if (it == in.armies.end()) continue;
        const std::string& fac = it->second.faction;
        int claim = 0;
        const int shots = std::max(0, detail::map_get(r.shots_fired, fac));
        if (shots > 0)
            claim = (shots + rules.rounds_per_cartridge - 1) / rules.rounds_per_cartridge;
        else if (r.cartridges_spent.count(fac))
            claim = std::max(0, r.cartridges_spent.at(fac));
        else if (!any_claim)
            claim = std::max(0, detail::map_get(fielded_by_army, aid) * 2);
        rounds_fired[fac] = shots > 0 ? shots : static_cast<long>(claim) * rules.rounds_per_cartridge;
        const std::string magazine = side_magazine_province(it->second, in.provinces.at(r.province_id));
        claim_by_faction[fac] += magazine == r.province_id ? claim : 0;
    }
    // Powder wasted in misfires goes first; what is left can be made up into cartridges. The spend is
    // capped at what the magazine can supply: its cartridges, then its powder made up as needed.
    total_powder_spent = std::min(total_powder_spent, powder_before);
    const int per_powder = cartridges_per_powder();
    const int powder_left = powder_before - total_powder_spent;
    int total_cart_spent = 0;
    for (const auto& kv : claim_by_faction) total_cart_spent += kv.second;
    total_cart_spent = std::min(total_cart_spent, cart_before + powder_left * per_powder);
    const int from_cartridges = std::min(total_cart_spent, cart_before);
    const int powder_made_up = (total_cart_spent - from_cartridges + per_powder - 1) / per_powder;
    pst.cartridges = cart_before - from_cartridges;
    pst.powder = powder_left - powder_made_up;

    // Musket loss from killed (equipment broken/captured); no free recovery.
    int muskets_lost = 0;
    for (const auto& fk : units_killed)
        for (const auto& ck : fk.second) muskets_lost += std::max(0, ck.second);
    pst.muskets = std::max(0, pst.muskets - muskets_lost);

    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = s.armies.find(aid);
        if (it == s.armies.end()) continue;
        auto loss_it = units_killed.find(it->second.faction);
        int lost = 0;
        if (loss_it != units_killed.end())
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
    const int personal_morale = std::min(5, personal_kills);
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
            int bump = rules.morale_victory;
            if (a.faction == s.player_faction) bump += personal_morale;
            a.morale = detail::clamp_int(a.morale + bump, 0, 100);
        } else {
            // A broken army (army_broke) pays a heavier morale price but keeps its survivors: it is
            // not dispersed by the morale floor. An ordinary defeat costs morale.defeat.
            a.morale = detail::clamp_int(a.morale - (army_broke ? rules.army_broke_morale_penalty : rules.morale_defeat), 0, 100);
            if (a.faction == s.player_faction && personal_morale > 0)
                a.morale = detail::clamp_int(a.morale + personal_morale, 0, 100);
            const int fit = detail::total_units(a);
            if (fit <= 0) {
                loser_fate = "destroyed";
            } else if (!army_broke && a.morale <= 10) {
                loser_fate = "shattered";
                shatter_fit = fit;
            } else {
                // A broken army marches out with its survivors even when every neighbour holds an
                // enemy. Deleting it was the old "nowhere to go" shatter, and it contradicted the rule.
                if (retreat.empty() && army_broke) {
                    auto ground = s.provinces.find(r.province_id);
                    if (ground != s.provinces.end() && !ground->second.adjacent.empty())
                        retreat = ground->second.adjacent.front();
                }
                if (!retreat.empty()) {
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
        it->second.returning_edge.clear();  // a battle ends any turning back: the army is where it fought
        it->second.returning_hours = 0;
    }

    // Cartridge boxes. Each side leaves with what it formed up with, less the rounds its own
    // boxes paid for; never more than a full box on the march. A beaten army that quits the field
    // drops a share of what is left (more when it routed or broke), and the winner's men fill
    // their boxes from it.
    const std::map<std::string, int> magazine_paid = allocate_spend(claim_by_faction, total_cart_spent);
    std::map<std::string, int> rounds_before, rounds_after, rounds_dropped, rounds_picked_up;
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto before = in.armies.find(aid);
        if (before == in.armies.end()) continue;
        const std::string& fac = before->second.faction;
        auto side = formed.sides.find(fac);
        const int opening = side != formed.sides.end() ? side->second.rounds_per_man : carried_rounds(before->second);
        const long men = static_cast<long>(std::max(0, detail::total_units(before->second))) * rules.soldiers_per_unit;
        const long covered = static_cast<long>(detail::map_get(magazine_paid, fac)) * rules.rounds_per_cartridge;
        const long from_boxes = std::max(0L, detail::map_get(rounds_fired, fac) - covered);
        const int per_man = men > 0 ? static_cast<int>((from_boxes + men - 1) / men) : 0;
        rounds_before[fac] = opening;
        rounds_after[fac] = detail::clamp_int(opening - per_man, 0, rules.rounds_carried);
    }
    {
        Army* beaten = nullptr;
        Army* victor = nullptr;
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = s.armies.find(aid);
            if (it == s.armies.end()) continue;
            it->second.rounds_per_man = detail::map_get(rounds_after, it->second.faction);
            (it->second.faction == r.winner ? victor : beaten) = &it->second;
        }
        if (beaten && loser_fate == "retreated") {
            const bool rout = army_broke || r.end_reason == "routed";
            const int pct = rout ? rules.rout_drop_pct : rules.defeat_drop_pct;
            const int dropped = (beaten->rounds_per_man * pct + 99) / 100;
            if (dropped > 0) {
                beaten->rounds_per_man -= dropped;
                rounds_dropped[beaten->faction] = dropped;
                rounds_after[beaten->faction] = beaten->rounds_per_man;
                const long left = static_cast<long>(dropped) * std::max(0, detail::total_units(*beaten)) *
                                  rules.soldiers_per_unit;
                const long men = victor ? static_cast<long>(std::max(0, detail::total_units(*victor))) *
                                              rules.soldiers_per_unit
                                        : 0;
                const int picked = men > 0 ? static_cast<int>(std::min<long>(
                                                 std::max(0, rules.rounds_carried - victor->rounds_per_man), left / men))
                                           : 0;
                if (picked > 0) {
                    victor->rounds_per_man += picked;
                    rounds_picked_up[victor->faction] = picked;
                    rounds_after[victor->faction] = victor->rounds_per_man;
                }
            }
        }
    }

    // Salvage from enemy dead + flight/shatter (credited to battle province magazine).
    int loser_killed = 0;
    {
        auto lit = units_killed.find(loser);
        if (lit != units_killed.end())
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
    // The winner takes the town, whatever ended the fight. Two exceptions: on Battle Road
    // (force_preserved) a column that only kept itself together takes nothing unless it also held
    // the posts; and a defender beaten on its own fortified ground is besieged, not dispossessed.
    std::string ownership_reason = "unchanged";
    int siege_hours = 0;
    bool siege_lifted = false;
    {
        Province& ground = s.provinces.at(r.province_id);
        const bool needs_posts = r.end_reason == "force_preserved";
        const bool takes = stores_forfeit || !needs_posts || r.objective_holder == r.winner;
        // A siege needs a besieger: a winner whose army was wiped out on the field takes the
        // town outright instead of besieging it with nobody.
        // And a siege needs a general: a detachment that wins before a fort cannot sit down to it.
        bool besieger_standing = false, besieger_can = false;
        for (const auto& ak : s.armies)
            if (ak.second.province == r.province_id && ak.second.faction == r.winner &&
                detail::total_units(ak.second) > 0) {
                besieger_standing = true;
                besieger_can = besieger_can || detail::army_can_besiege(s, ak.second);
            }
        if (ground.owner == r.winner) {
            if (!ground.siege_by.empty()) {
                siege_lifted = true;
                ground.siege_by.clear();
                ground.siege_hours_remaining = 0;
            }
        } else if (takes && ground.fort_level > 0 && ground.owner == loser && besieger_standing && !besieger_can) {
            ownership_reason = "no_general_no_siege";
            detail::add_log(s, "no siege at " + r.province_id + ": the victors are a detachment with no general");
        } else if (takes && ground.fort_level > 0 && ground.owner == loser && besieger_standing) {
            if (ground.siege_by != r.winner || ground.siege_hours_remaining <= 0) {
                ground.siege_by = r.winner;
                ground.siege_hours_remaining = rules.siege_hours_per_fort_level * ground.fort_level;
            }
            siege_hours = ground.siege_hours_remaining;
            ownership_reason = "siege_begun";
        } else if (takes) {
            ground.owner = r.winner;
            ground.siege_by.clear();
            ground.siege_hours_remaining = 0;
            ownership_reason = stores_forfeit ? "stores_forfeit"
                                              : (r.objective_holder == r.winner ? "held_objective" : "won_field");
            if (stores_forfeit)
                detail::add_log(s, "stores exposed: " + r.province_id + " forfeited when the field was lost");
        }
    }
    // A siege that was already running and still stands (e.g. the besiegers held the field on
    // Battle Road without the posts) is part of this result's report too.
    {
        const Province& ground = s.provinces.at(r.province_id);
        if (!ground.siege_by.empty() && siege_hours <= 0) siege_hours = ground.siege_hours_remaining;
    }
    // Capture itself never changes local_support; defender win reward may raise it below.
    if (s.local_support != support_before) s.local_support = support_before;

    // Guns. Only the side that wins the field keeps what it overran (a beaten side cannot drag guns
    // off), never more than the other side brought; and a beaten army that is destroyed or dispersed
    // leaves every gun it had to the winner.
    std::map<std::string, int> guns_changed;
    {
        Army* victor = nullptr;
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = s.armies.find(aid);
            if (it != s.armies.end() && it->second.faction == r.winner) victor = &it->second;
        }
        const Army* beaten_before = nullptr;
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = in.armies.find(aid);
            if (it != in.armies.end() && it->second.faction == loser) beaten_before = &it->second;
        }
        if (beaten_before && beaten_before->guns > 0) {
            Army* beaten = nullptr;
            auto bit = s.armies.find(beaten_before->id);
            if (bit != s.armies.end()) beaten = &bit->second;
            const int brought = served_guns(*beaten_before);
            int taken = std::min(std::max(0, detail::map_get(r.guns_taken, r.winner)), brought);
            if (!beaten) taken = beaten_before->guns;  // the army is gone: its whole park is left on the field
            if (taken > 0) {
                if (beaten) beaten->guns = std::max(0, beaten->guns - taken);
                if (victor) {
                    victor->guns += taken;
                } else {
                    s.province_stores[r.province_id].guns += taken;  // nobody left to haul them: they lie in the town
                }
                guns_changed[r.winner] = taken;
                if (victor && victor->faction == s.player_faction) {
                    ResourceChange ch;
                    ch.reason = std::to_string(taken) + (taken == 1 ? " gun taken at " : " guns taken at ") +
                                s.provinces.at(r.province_id).name + ": they go with " + victor->name;
                    ch.province_id = r.province_id;
                    detail::record_resource_change(s, ch);
                } else if (beaten_before->faction == s.player_faction) {
                    ResourceChange ch;
                    ch.reason = std::to_string(taken) + (taken == 1 ? " of our guns lost at " : " of our guns lost at ") +
                                s.provinces.at(r.province_id).name;
                    ch.province_id = r.province_id;
                    detail::record_resource_change(s, ch);
                }
            }
        }
    }

    // Colours taken on the field. Each ensign unit an army brought carried one, so a side cannot
    // lose more than that. Support is the Patriot cause's: colours the player takes raise it, his
    // own colours lost lower it.
    std::map<std::string, int> colours_taken;
    for (const auto& ck : r.colours_captured) {
        if (ck.second <= 0) continue;
        int carried = 0;
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = in.armies.find(aid);
            if (it != in.armies.end() && it->second.faction != ck.first)
                carried += std::max(0, detail::map_get(it->second.units, rules.colours_class));
        }
        const int taken = std::min(ck.second, carried);
        if (taken <= 0) continue;
        colours_taken[ck.first] = taken;
        const int delta = (ck.first == s.player_faction ? 1 : -1) * taken * rules.colours_support_per_capture;
        const int before = s.local_support;
        s.local_support = detail::clamp_int(s.local_support + delta, 0, 100);
        ResourceChange ch;
        const std::string town = s.provinces.at(r.province_id).name;
        ch.reason = (ck.first == s.player_faction
                         ? std::to_string(taken) + (taken == 1 ? " enemy colour" : " enemy colours") + " taken at " + town
                         : std::to_string(taken) + (taken == 1 ? " of our colours" : " of our colours") + " lost at " + town) +
                    ": " + (s.local_support - before >= 0 ? "+" : "") + std::to_string(s.local_support - before) + " Support";
        ch.local_support = s.local_support - before;
        ch.province_id = r.province_id;
        detail::record_resource_change(s, ch);
    }

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
        ch.reason = "Battle ammunition at " + s.provinces.at(r.province_id).name + ": -" + std::to_string(from_cartridges) +
                    " cartridges, -" + std::to_string(total_powder_spent + powder_made_up) + " powder" +
                    (powder_made_up > 0 ? " (" + std::to_string(powder_made_up) + " made up into cartridges)" : std::string()) +
                    ", -" + std::to_string(muskets_lost) + " muskets";
        ch.cartridges = -from_cartridges;
        ch.powder = -(total_powder_spent + powder_made_up);
        ch.muskets = -muskets_lost;
        ch.province_id = r.province_id;
        detail::record_resource_change(s, ch);
    }
    if (scav_cart > 0 || scav_powder > 0 || scav_muskets > 0) {
        ResourceChange ch;
        ch.reason = "Scavenged the field at " + s.provinces.at(r.province_id).name + ": +" + std::to_string(scav_cart) +
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
        ownership_line = "Held the posts: " + s.provinces.at(r.province_id).name + " now " + detail::faction_word(r.winner);
        detail::add_log(s, ownership_line);
    } else if (ownership_reason == "won_field") {
        ownership_line = "Won the field: " + s.provinces.at(r.province_id).name + " now " + detail::faction_word(r.winner);
        detail::add_log(s, ownership_line);
    } else if (ownership_reason == "siege_begun") {
        const Province& g = s.provinces.at(r.province_id);
        ownership_line = "Fort L" + std::to_string(g.fort_level) + " holds: " + g.name + " is besieged and falls in " +
                         std::to_string(siege_hours) + " hours unless the besiegers leave or are beaten";
        detail::add_log(s, ownership_line);
    } else if (ownership_reason == "stores_forfeit") {
        ownership_line = "Stores exposed after field lost: " + s.provinces.at(r.province_id).name +
                         " now " + detail::faction_word(r.winner);
    } else if (ownership_reason == "no_general_no_siege") {
        // The field is won and the town is not: say why, and what to do about it.
        const Province& g = s.provinces.at(r.province_id);
        ownership_line = "Fort L" + std::to_string(g.fort_level) + " holds: " + g.name +
                         " is not taken by a won battle. Its works must be besieged, and the victors have no general to conduct a siege";
        detail::add_log(s, ownership_line);
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
    report.losses = units_killed;
    report.wounded = units_wounded;
    report.casualties = men_taken;
    report.colours_captured = colours_taken;
    report.guns_taken = guns_changed;
    report.ownership_reason = ownership_reason;
    report.defender_reward_support = defender_reward;
    report.personal_kills = personal_kills;
    report.retreat_to = retreat_to;
    report.loser_fate = loser_fate;
    report.scavenged_cartridges = scav_cart;
    report.scavenged_powder = scav_powder;
    report.scavenged_muskets = scav_muskets;
    report.end_reason = r.end_reason;
    // The kind of fight it was, so the card can say "the works were held" and not "more men standing".
    {
        const BattleSetup fought = build_battle_setup(in, r.province_id);
        report.mode = (fought.valid && fought.mode != "conquest") ? fought.mode : std::string();
    }
    report.prisoners = prisoners_taken;
    report.siege_hours = siege_hours;
    report.rounds_before = rounds_before;
    report.rounds_after = rounds_after;
    report.rounds_dropped = rounds_dropped;
    report.rounds_picked_up = rounds_picked_up;
    {
        const Province& g = s.provinces.at(r.province_id);
        if (g.owner != owner_before) {
            report.map_changes.push_back(g.name + " changes hands: " + detail::faction_word(owner_before) + " to " + detail::faction_word(g.owner));
            if (pst.cartridges > 0 || pst.powder > 0 || pst.muskets > 0 || pst.provisions > 0 || pst.guns > 0)
                report.map_changes.push_back("Magazine at " + g.name + " captured: " +
                                             std::to_string(pst.cartridges) + " cartridges, " +
                                             std::to_string(pst.powder) + " powder, " +
                                             std::to_string(pst.muskets) + " muskets, " +
                                             std::to_string(pst.provisions) + " provisions" +
                                             (pst.guns > 0 ? ", " + std::to_string(pst.guns) + (pst.guns == 1 ? " field gun" : " field guns")
                                                           : std::string()));
        } else if (siege_hours > 0) {
            report.map_changes.push_back(g.name + " is besieged: it falls in " + std::to_string(siege_hours) +
                                         " hours unless relieved");
        } else if (ownership_reason == "no_general_no_siege") {
            report.map_changes.push_back(g.name + " stays " + detail::faction_word(g.owner) + ": its works (Fort L" + std::to_string(g.fort_level) +
                                         ") must be besieged, and the victors have no general to conduct a siege");
            const std::string how = how_to_form_army(s, r.winner);
            if (!how.empty() && r.winner == s.player_faction) report.map_changes.push_back(how);
        } else {
            report.map_changes.push_back(g.name + " stays " + detail::faction_word(g.owner));
        }
        if (siege_lifted) report.map_changes.push_back("The siege of " + g.name + " is lifted");
        if (loser_fate == "retreated" && !retreat_to.empty())
            report.map_changes.push_back(
                "The beaten army falls back to " +
                (s.provinces.count(retreat_to) ? s.provinces.at(retreat_to).name : retreat_to));
        else if (loser_fate == "shattered")
            report.map_changes.push_back("The beaten army is dispersed and leaves the map");
        else if (loser_fate == "destroyed")
            report.map_changes.push_back("The beaten army is destroyed and leaves the map");
        int taken = 0;
        for (const auto& fk : prisoners_taken)
            for (const auto& ck : fk.second) taken += ck.second;
        if (taken > 0) report.map_changes.push_back(std::to_string(taken) + " prisoners taken");
        for (const auto& ck : guns_changed)
            report.map_changes.push_back(detail::faction_word(ck.first) + " troops took " + std::to_string(ck.second) +
                                         (ck.second == 1 ? " gun" : " guns") + " on the field");
        for (const auto& ck : colours_taken)
            report.map_changes.push_back(detail::faction_word(ck.first) + " troops took " + std::to_string(ck.second) +
                                         (ck.second == 1 ? " colour" : " colours") + " on the field");
        // Cartridge boxes of the armies that are still on the map.
        for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
            auto it = s.armies.find(aid);
            if (it == s.armies.end()) continue;
            const std::string& fac = it->second.faction;
            std::string line = it->second.name + ": " + std::to_string(it->second.rounds_per_man) +
                               " rounds a man left of " + std::to_string(detail::map_get(rounds_before, fac));
            if (detail::map_get(rounds_dropped, fac) > 0)
                line += " (" + std::to_string(rounds_dropped.at(fac)) + " a man dropped in the retreat)";
            if (detail::map_get(rounds_picked_up, fac) > 0)
                line += " (" + std::to_string(rounds_picked_up.at(fac)) + " a man picked up from the field)";
            report.map_changes.push_back(line);
        }
    }
    if (defender_reward > 0)
        report.change_reasons.push_back("Defended " + s.provinces.at(r.province_id).name + ": +5 Support");
    report.change_reasons.push_back(ownership_line);
    if (total_cart_spent || total_powder_spent || muskets_lost) {
        report.change_reasons.push_back("Battle ammunition at " + s.provinces.at(r.province_id).name + ": -" +
                                        std::to_string(total_cart_spent) + " cartridges, -" +
                                        std::to_string(total_powder_spent) + " powder, -" +
                                        std::to_string(muskets_lost) + " muskets");
    }
    if (scav_cart || scav_powder || scav_muskets) {
        report.change_reasons.push_back("Scavenged the field: +" + std::to_string(scav_cart) +
                                        " cartridges, +" + std::to_string(scav_powder) + " powder, +" +
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
    // Split the actual (stock-clamped) spend across the sides that drew on this town's magazine.
    std::map<std::string, int> raw_powder;
    for (const std::string& aid : {pb.attacker_army, pb.defender_army}) {
        auto it = in.armies.find(aid);
        if (it == in.armies.end()) continue;
        raw_powder[it->second.faction] =
            r.powder_spent.count(it->second.faction) ? std::max(0, r.powder_spent.at(it->second.faction)) : 0;
    }
    report.cartridges_spent = magazine_paid;
    report.powder_spent = allocate_spend(raw_powder, total_powder_spent);
    report.morale_delta = morale_delta;
    s.progress.last_battle_report = report;

    s.memory.push_back(
        {s.now.hour_index, r.winner, "Battle of " + s.provinces.at(r.province_id).name + " won by " + r.winner});
    s.pending_battle = PendingBattle{};
    detail::add_log(s, "battle at " + r.province_id + " won by " + r.winner);

    detail::pass_after_battle(s, in, r, pb);

    ++s.progress.battles_fought;
    ++s.progress.battles_won[r.winner];
    // Opening-war gate: first battle (Concord in this slice) unlocks aftermath correspondence.
    if (r.province_id == "concord" || r.province_id == "lexington" || s.progress.battles_fought == 1) {
        s.progress.lexington_fought = true;
        s.progress.aftermath_pending = true;
    }
    return s;
}

}  // namespace pa250
