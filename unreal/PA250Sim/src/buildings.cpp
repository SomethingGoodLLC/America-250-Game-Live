#include "internal.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

namespace pa250 {
namespace {

using json = nlohmann::json;

struct BuildingDef {
    std::string id;
    std::string name;
    std::string kind;  // track | discrete
    bool stackable = false;
    int gold_cost = 0;
    int build_time_days = 1;
    int upkeep_gold = 0;
    int max_level = 1;
    int support_on_complete = 0;
    int support_per_month = 0;
    int battle_morale = 0;
    std::string campaign_effect;
    std::string battle_effect;
    std::string description;
};

struct RecruitDef {
    std::string id;
    std::string name;
    std::string unit_class;
    int men = 0;
    int gold = 0;
    int muskets = 0;
    int provisions = 0;
    int days = 1;
    std::string requires_building;  // must stand in the town (level >= 1)
    std::string description;
    std::string faction;           // who may raise it; empty = any player
    std::string requires_chapter;  // "continental" = not before Congress adopts the army (14 Jun 1775)
    std::string role;              // battlefield summary for the player (display only)
};

struct EconomyTuning {
    int levy_per_supply = 15;      // gold per point of a town's supply value (as before this pass)
    int pay_militia = 1;           // gold per man per month
    int pay_regular = 2;           // every other class
    int depot_trade_gold = 8;      // per Depot level per month
    int workshop_muskets = 1;      // muskets per Workshop level per month, into that town's magazine
    int loan_step = 100;
    int loan_interest_pct = 5;     // per month
    int credit_per_town = 60;      // credit limit = this x towns held
    int unpaid_morale = 8;         // each army, each unpaid month
    int unpaid_support = 3;
    int sea_subsidy_gold = 40;     // per month to a side that controls the harbour and holds its HQ town
    int sea_provisions = 12;       // landed in that town's magazine each month
    int enemy_reserve_gold = 30;   // the other side keeps this back before raising a company
};

struct BuildingsCatalog {
    int demolish_refund_pct = 10;  // salvage share of the level-0 cost, per level
    std::map<std::string, BuildingDef> by_id;
    std::map<std::string, RecruitDef> recruits;
    EconomyTuning economy;
    std::map<std::string, int> manpower;  // size key -> men a town can raise per month at Support 50
};

BuildingsCatalog g_catalog;
bool g_buildings_catalog_loaded = false;
std::map<std::string, std::string> g_province_size;  // province_id -> size key
std::map<std::string, int> g_size_slots;             // size key -> slot count
bool g_sizes_loaded = false;

void ensure_default_recruits(BuildingsCatalog& cat);

void ensure_default_catalog() {
    if (!g_catalog.by_id.empty()) return;
    g_catalog.demolish_refund_pct = 10;
    auto add = [&](BuildingDef d) { g_catalog.by_id[d.id] = d; };
    add({"farm", "Farm", "track", false, 30, 1, 0, 3, 0, 0, 0,
         "+3 provisions/month; +supply income",
         "+2 readiness/level; faster reinforcement",
         "Fields and barns that feed the magazine."});
    add({"workshop", "Workshop", "track", false, 40, 1, 2, 3, 0, 0, 0,
         "Faster cartridge craft; +1 musket/month per level",
         "+8 cartridge budget/level (militia tickets)",
         "A flintlock shop for cartridges."});
    add({"depot", "Depot", "track", false, 35, 1, 2, 3, 0, 0, 0,
         "Shorter shipment transit into this town; +8 gold/month trade per level",
         "+2 readiness/level; faster reinforcement",
         "Storehouse and wagons."});
    add({"fort", "Fort", "track", false, 45, 1, 3, 3, 0, 0, 0,
         "Lets the town enlist provincial regulars",
         "Defender holds a post at start; +3 morale/+2 readiness per level; extra cover",
         "Earthworks that stiffen the defense."});
    add({"church", "Meeting House", "discrete", false, 50, 2, 1, 1, 5, 1, 2,
         "+5 Support on completion; +1 Support/month", "Defender morale +2",
         "The parish meeting house that steadies local Support."});
    add({"militia_muster", "Militia Muster", "discrete", false, 40, 1, 2, 1, 0, 0, 0,
         "Lets the town raise militia companies", "Extra defender militia muster waves",
         "Drill ground for minutemen."});
    ensure_default_recruits(g_catalog);
}

void ensure_default_recruits(BuildingsCatalog& cat) {
    if (cat.recruits.empty()) {
        cat.recruits["militia_company"] = {"militia_company", "Militia company", "militia", 6, 20, 0, 3, 2,
                                           "militia_muster",
                                           "Townsmen who turn out with their own firelocks."};
        cat.recruits["provincial_regulars"] = {"provincial_regulars", "Provincial regulars", "line_infantry",
                                               4, 40, 4, 4, 4, "fort",
                                               "Enlisted men drilled in the works and armed from the magazine."};
        cat.recruits["rifle_company"] = {"rifle_company", "Rifle company", "rifleman", 3, 45, 0, 3, 5,
                                         "militia_muster",
                                         "Frontier riflemen with their own long rifles.", kContinental,
                                         "continental",
                                         "Marksmen. Long range, slow to load, no bayonet."};
        cat.recruits["field_officer"] = {"field_officer", "Company officer", "officer", 1, 30, 0, 1, 2,
                                         "militia_muster",
                                         "A captain or lieutenant to steady the men and order their fire.", "",
                                         "", "Leader. Men near him aim steadier and load faster."};
        // Dragoons (GAME INVENTION for 1775): ride to the fight, dismount and fight on foot with carbines.
        cat.recruits["light_horse"] = {"light_horse", "Troop of light horse", "dragoon", 3, 60, 3, 5, 6, "farm",
                                       "Farmers' sons on their own horses, mounted from the town's farms.",
                                       kContinental, "", "Dragoons. Ride, dismount, fight on foot; remount to move."};
        cat.recruits["dragoon_troop"] = {"dragoon_troop", "Troop of light dragoons", "dragoon", 3, 70, 3, 5, 6, "fort",
                                         "A troop of light dragoons with carbines, pistols and sabres.", kBritish, "",
                                         "Dragoons. Ride, dismount, fight on foot; remount to move."};
        cat.recruits["militia_company"].faction = kContinental;
        cat.recruits["provincial_regulars"].faction = kContinental;
    }
    if (cat.manpower.empty()) cat.manpower = {{"small_town", 6}, {"town", 12}, {"city", 18}};
}

void ensure_default_sizes() {
    if (!g_size_slots.empty()) return;
    g_size_slots = {{"small_town", 3}, {"town", 4}, {"city", 6}};
    g_province_size = {{"lexington", "small_town"}, {"lincoln", "small_town"},
                       {"menotomy", "small_town"},  {"brookline", "small_town"},
                       {"chelsea", "small_town"},   {"concord", "town"},
                       {"cambridge", "town"},       {"charlestown", "town"},
                       {"roxbury", "town"},         {"watertown", "town"},
                       {"medford", "town"},         {"dorchester", "town"},
                       {"boston", "city"}};
}

const BuildingDef* find_def(const std::string& id) {
    ensure_default_catalog();
    auto it = g_catalog.by_id.find(id);
    return it == g_catalog.by_id.end() ? nullptr : &it->second;
}

bool is_track(const BuildingDef& d) { return d.kind == "track"; }

int* track_level_ptr(Province& p, const std::string& id) {
    if (id == "farm") return &p.farm_level;
    if (id == "workshop") return &p.workshop_level;
    if (id == "depot") return &p.depot_level;
    if (id == "fort") return &p.fort_level;
    return nullptr;
}

const int* track_level_cptr(const Province& p, const std::string& id) {
    if (id == "farm") return &p.farm_level;
    if (id == "workshop") return &p.workshop_level;
    if (id == "depot") return &p.depot_level;
    if (id == "fort") return &p.fort_level;
    return nullptr;
}

std::string next_job_id(CampaignState& s) {
    return "build_" + std::to_string(s.next_command_seq++);
}

const ConstructionJob* find_job(const CampaignState& s, const std::string& province_id,
                                const std::string& building_id) {
    for (const auto& j : s.progress.construction_jobs) {
        if (j.status == "underway" && j.province_id == province_id && j.building_id == building_id)
            return &j;
    }
    return nullptr;
}

// Single source of truth for "may this build/upgrade start?". Returns an error code ("" = ok).
// start_construction, building_preview and city_view_json all use it, so the greyed-out reason
// the player reads can never disagree with what the sim will actually accept.
std::string construction_block(const CampaignState& s, const std::string& province_id,
                               const std::string& building_id) {
    const BuildingDef* d = find_def(building_id);
    if (!d) return "unknown_building";
    auto pit = s.provinces.find(province_id);
    if (pit == s.provinces.end()) return "unknown_province";
    const Province& p = pit->second;
    if (p.owner != s.player_faction) return "not_your_province";
    if (s.pending_battle.active) return "battle_pending";
    if (find_job(s, province_id, building_id)) return "already_building";
    const int level = building_level(p, building_id);
    if (is_track(*d)) {
        if (level >= std::max(1, d->max_level)) return "max_level";
    } else if (level > 0) {
        return "already_present";
    }
    // Upgrades stay in their slot; only a new building needs a free one.
    if (level == 0 && province_slots_occupied(s, province_id) >= province_slot_count(province_id))
        return "no_free_slot";
    const int cost = building_cost(building_id, level);
    if (cost < 0) return "unknown_building";
    if (s.player_gold < cost) return "insufficient_gold";
    return "";
}

std::string block_reason_text(const CampaignState& s, const std::string& province_id,
                              const std::string& building_id, const std::string& code) {
    if (code == "unknown_building") return "Unknown building.";
    if (code == "unknown_province") return "Unknown province.";
    if (code == "not_your_province") return "Unavailable: not your province.";
    if (code == "battle_pending") return "Unavailable: battle pending.";
    if (code == "already_building") return "Unavailable: already under construction.";
    if (code == "already_present") return "Unavailable: already built.";
    if (code == "max_level") return "Unavailable: already at maximum level.";
    if (code == "no_free_slot") {
        return "Unavailable: no free building slots (" +
               std::to_string(province_slots_occupied(s, province_id)) + "/" +
               std::to_string(province_slot_count(province_id)) + ").";
    }
    if (code == "insufficient_gold") {
        const int level = building_level(s.provinces.at(province_id), building_id);
        return "Unavailable: need " + std::to_string(building_cost(building_id, level)) +
               " gold (treasury " + std::to_string(s.player_gold) + ").";
    }
    return "Unavailable: " + code + ".";
}

const RecruitDef* find_recruit(const std::string& id) {
    ensure_default_catalog();
    auto it = g_catalog.recruits.find(id);
    return it == g_catalog.recruits.end() ? nullptr : &it->second;
}

const RecruitJob* find_muster(const CampaignState& s, const std::string& province_id) {
    for (const auto& j : s.progress.recruit_jobs)
        if (j.province_id == province_id) return &j;
    return nullptr;
}

// One check for "may this company be raised here now?", shared by start, preview and snapshot.
std::string recruit_block(const CampaignState& s, const std::string& province_id,
                          const std::string& unit_id) {
    const RecruitDef* d = find_recruit(unit_id);
    if (!d) return "unknown_unit";
    if (!d->faction.empty() && d->faction != s.player_faction) return "unknown_unit";
    auto pit = s.provinces.find(province_id);
    if (pit == s.provinces.end()) return "unknown_province";
    const Province& p = pit->second;
    if (p.owner != s.player_faction) return "not_your_province";
    if (s.pending_battle.active) return "battle_pending";
    if (find_muster(s, province_id)) return "already_mustering";
    if (d->requires_chapter == "continental" && !s.progress.chapter_continental) return "not_yet";
    if (!d->requires_building.empty() && !building_present(p, d->requires_building))
        return "needs_building";
    if (std::max(0, p.manpower_used) + d->men > province_manpower_cap(s, province_id))
        return "no_manpower";
    if (s.player_gold < d->gold) return "insufficient_gold";
    const ProvinceStores& st = province_ledger(s, province_id);
    if (st.muskets < d->muskets) return "insufficient_muskets";
    if (st.provisions < d->provisions) return "insufficient_provisions";
    return "";
}

std::string recruit_reason_text(const CampaignState& s, const std::string& province_id,
                                const std::string& unit_id, const std::string& code) {
    const RecruitDef* d = find_recruit(unit_id);
    if (code == "unknown_unit") return "Unknown company.";
    if (code == "unknown_province") return "Unknown province.";
    if (code == "not_your_province") return "Unavailable: not your town.";
    if (code == "battle_pending") return "Unavailable: battle pending.";
    if (code == "already_mustering") return "Unavailable: a company is already mustering here.";
    if (code == "not_yet")
        return "Unavailable: not before Congress adopts the army (14 June 1775).";
    if (code == "needs_building") {
        const BuildingDef* b = d ? find_def(d->requires_building) : nullptr;
        return "Unavailable: needs a " + (b ? b->name : std::string("building")) + " in this town.";
    }
    if (code == "no_manpower") {
        const int cap = province_manpower_cap(s, province_id);
        const int left = std::max(0, cap - std::max(0, s.provinces.at(province_id).manpower_used));
        return "Unavailable: only " + std::to_string(left) + " men left to raise here this month.";
    }
    const ProvinceStores& st = province_ledger(s, province_id);
    if (code == "insufficient_gold")
        return "Unavailable: need " + std::to_string(d->gold) + " gold (treasury " +
               std::to_string(s.player_gold) + ").";
    if (code == "insufficient_muskets")
        return "Unavailable: need " + std::to_string(d->muskets) + " muskets in this magazine (" +
               std::to_string(st.muskets) + " here).";
    if (code == "insufficient_provisions")
        return "Unavailable: need " + std::to_string(d->provisions) + " provisions in this magazine (" +
               std::to_string(st.provisions) + " here).";
    return "Unavailable: " + code + ".";
}

// A scripted army (letters and treaties name it) outlives a company the player raised.
bool survivor_before(const std::string& a, const std::string& b) {
    const bool ra = a.rfind("raised_", 0) == 0;
    const bool rb = b.rfind("raised_", 0) == 0;
    if (ra != rb) return !ra;
    return a < b;
}

std::string idle_player_army_at(const CampaignState& s, const std::string& province_id) {
    std::string best;
    for (const auto& kv : s.armies) {
        const Army& a = kv.second;
        if (a.faction != s.player_faction || a.province != province_id || !a.path.empty()) continue;
        if (best.empty() || survivor_before(kv.first, best)) best = kv.first;
    }
    return best;
}

constexpr int kRecruitMorale = 60;  // green troops

// One month's budget as the settlement will apply it. settle_monthly_budget and treasury_json both
// read this, so the projection the player sees is the settlement that happens.
struct BudgetLine {
    std::string label;
    int gold = 0;  // + income, - expense
};
struct Budget {
    std::vector<BudgetLine> income;
    std::vector<BudgetLine> expenses;
    int income_total = 0;
    int expense_total = 0;  // positive number
    int muskets = 0;        // workshop output (resources, not gold)
    int provisions = 0;     // farm harvest
    int interest = 0;
    int pay = 0;
    int upkeep = 0;
    int trade = 0;
    int levy = 0;
    int subsidy = 0;        // landed by sea (a side that controls the harbour)
};

// True when the side is supplied by sea: it controls the harbour and still holds its HQ town.
bool supplied_by_sea(const CampaignState& s, const std::string& faction) {
    auto fit = s.factions.find(faction);
    if (fit == s.factions.end() || !fit->second.controls_harbor) return false;
    auto hq = s.provinces.find(default_depot_province(s, faction));
    return hq != s.provinces.end() && hq->second.owner == faction;
}

Budget monthly_budget(const CampaignState& s, const std::string& faction) {
    ensure_default_catalog();
    const EconomyTuning& t = g_catalog.economy;
    const bool player = faction == s.player_faction;
    Budget b;
    int towns = 0;
    for (const auto& pk : s.provinces) {
        const Province& p = pk.second;
        if (p.owner != faction) continue;
        ++towns;
        b.levy += town_levy_gold(p);
        b.trade += std::max(0, p.depot_level) * t.depot_trade_gold;
        b.muskets += std::max(0, p.workshop_level) * t.workshop_muskets;
        b.provisions += std::max(0, p.farm_level) * 3;  // clock.cpp's farm harvest
        for (const auto& kv : g_catalog.by_id)
            if (building_present(p, kv.first)) b.upkeep += std::max(0, kv.second.upkeep_gold);
    }
    int militia = 0, regulars = 0;
    for (const auto& ak : s.armies) {
        if (ak.second.faction != faction) continue;
        for (const auto& u : ak.second.units) {
            if (u.second <= 0) continue;
            (u.first == "militia" ? militia : regulars) += u.second;
        }
    }
    b.pay = militia * t.pay_militia + regulars * t.pay_regular;
    const int debt = player ? std::max(0, s.progress.treasury_debt) : 0;  // only the player has credit
    b.interest = (debt * t.loan_interest_pct + 99) / 100;  // rounded up: a debt always costs something
    b.subsidy = supplied_by_sea(s, faction) ? t.sea_subsidy_gold : 0;
    if (b.subsidy > 0) b.provisions += t.sea_provisions;
    if (b.levy > 0) b.income.push_back({"Town levies (" + std::to_string(towns) + " towns)", b.levy});
    if (b.trade > 0) b.income.push_back({"Depot trade", b.trade});
    if (b.subsidy > 0) b.income.push_back({"Subsidy landed by sea", b.subsidy});
    if (b.pay > 0)
        b.expenses.push_back({"Army pay (" + std::to_string(militia) + " militia, " +
                                  std::to_string(regulars) + " regulars)",
                              -b.pay});
    if (b.upkeep > 0) b.expenses.push_back({"Building upkeep", -b.upkeep});
    if (b.interest > 0)
        b.expenses.push_back({"Interest on " + std::to_string(debt) + " gold of debt", -b.interest});
    b.income_total = b.levy + b.trade + b.subsidy;
    b.expense_total = b.pay + b.upkeep + b.interest;
    return b;
}

// Salvage from demolishing (timber, nails, brick sold off): a small % of the level-0 cost, times
// the level. Deliberately far below the cost: demolition frees a plot, it does not pay. Uses
// building_cost so tracks read the same logistics_tuning base the player was charged from.
int demolish_refund(const std::string& building_id, int level) {
    ensure_default_catalog();
    const int base = std::max(0, building_cost(building_id, 0));
    return (base * std::max(0, level) * std::max(0, g_catalog.demolish_refund_pct)) / 100;
}

}  // namespace

void load_buildings_catalog(const std::string& json_text) {
    try {
        const json j = json::parse(json_text);
        BuildingsCatalog cat;
        cat.demolish_refund_pct = j.value("demolish_refund_pct", 10);
        if (j.contains("buildings")) {
            for (const auto& b : j.at("buildings")) {
                BuildingDef d;
                d.id = b.value("id", "");
                if (d.id.empty()) continue;
                d.name = b.value("name", d.id);
                d.kind = b.value("kind", "discrete");
                d.stackable = b.value("stackable", false);
                d.gold_cost = b.value("gold_cost", 0);
                d.build_time_days = std::max(1, b.value("build_time_days", 1));
                d.upkeep_gold = b.value("upkeep_gold", 0);
                d.max_level = std::max(1, b.value("max_level", 1));
                d.support_on_complete = b.value("support_on_complete", 0);
                d.support_per_month = b.value("support_per_month", 0);
                d.battle_morale = b.value("battle_morale", 0);
                d.campaign_effect = b.value("campaign_effect", "");
                d.battle_effect = b.value("battle_effect", "");
                d.description = b.value("description", "");
                cat.by_id[d.id] = d;
            }
        }
        if (j.contains("recruits")) {
            for (const auto& r : j.at("recruits")) {
                RecruitDef d;
                d.id = r.value("id", "");
                d.unit_class = r.value("unit_class", "");
                if (d.id.empty() || d.unit_class.empty()) continue;
                d.name = r.value("name", d.id);
                d.men = std::max(1, r.value("men", 1));
                d.gold = std::max(0, r.value("gold", 0));
                d.muskets = std::max(0, r.value("muskets", 0));
                d.provisions = std::max(0, r.value("provisions", 0));
                d.days = std::max(1, r.value("days", 1));
                d.requires_building = r.value("requires_building", "");
                d.description = r.value("description", "");
                d.faction = r.value("faction", "");
                d.requires_chapter = r.value("requires_chapter", "");
                d.role = r.value("role", "");
                cat.recruits[d.id] = d;
            }
        }
        if (j.contains("economy")) {
            const auto& e = j.at("economy");
            EconomyTuning& t = cat.economy;
            t.levy_per_supply = std::max(0, e.value("levy_per_supply", t.levy_per_supply));
            t.pay_militia = std::max(0, e.value("pay_militia", t.pay_militia));
            t.pay_regular = std::max(0, e.value("pay_regular", t.pay_regular));
            t.depot_trade_gold = std::max(0, e.value("depot_trade_gold", t.depot_trade_gold));
            t.workshop_muskets = std::max(0, e.value("workshop_muskets", t.workshop_muskets));
            t.loan_step = std::max(1, e.value("loan_step", t.loan_step));
            t.loan_interest_pct = std::max(0, e.value("loan_interest_pct", t.loan_interest_pct));
            t.credit_per_town = std::max(0, e.value("credit_per_town", t.credit_per_town));
            t.unpaid_morale = std::max(0, e.value("unpaid_morale", t.unpaid_morale));
            t.unpaid_support = std::max(0, e.value("unpaid_support", t.unpaid_support));
            t.sea_subsidy_gold = std::max(0, e.value("sea_subsidy_gold", t.sea_subsidy_gold));
            t.sea_provisions = std::max(0, e.value("sea_provisions", t.sea_provisions));
            t.enemy_reserve_gold = std::max(0, e.value("enemy_reserve_gold", t.enemy_reserve_gold));
        }
        if (j.contains("manpower")) {
            for (auto it = j.at("manpower").begin(); it != j.at("manpower").end(); ++it)
                cat.manpower[it.key()] = std::max(0, it.value().get<int>());
        }
        ensure_default_recruits(cat);
        if (!cat.by_id.empty()) {
            g_catalog = cat;
            g_buildings_catalog_loaded = true;
            return;
        }
    } catch (...) {
    }
    g_catalog = BuildingsCatalog{};
    ensure_default_catalog();
    g_buildings_catalog_loaded = false;
}

void load_province_sizes(const std::string& json_text) {
    try {
        const json j = json::parse(json_text);
        std::map<std::string, int> sizes;
        std::map<std::string, std::string> provinces;
        if (j.contains("sizes")) {
            for (auto it = j.at("sizes").begin(); it != j.at("sizes").end(); ++it)
                sizes[it.key()] = it.value().get<int>();
        }
        if (j.contains("provinces")) {
            for (auto it = j.at("provinces").begin(); it != j.at("provinces").end(); ++it)
                provinces[it.key()] = it.value().get<std::string>();
        }
        if (!sizes.empty() && !provinces.empty()) {
            g_size_slots = sizes;
            g_province_size = provinces;
            g_sizes_loaded = true;
            return;
        }
    } catch (...) {
    }
    g_size_slots.clear();
    g_province_size.clear();
    ensure_default_sizes();
    g_sizes_loaded = false;
}

bool buildings_catalog_loaded() { return g_buildings_catalog_loaded; }
bool province_sizes_loaded() { return g_sizes_loaded; }

int province_slot_count(const std::string& province_id) {
    ensure_default_sizes();
    auto sit = g_province_size.find(province_id);
    if (sit == g_province_size.end()) return 3;
    auto cit = g_size_slots.find(sit->second);
    return cit == g_size_slots.end() ? 3 : std::max(1, cit->second);
}

int building_level(const Province& p, const std::string& building_id) {
    if (const int* lvl = track_level_cptr(p, building_id)) return std::max(0, *lvl);
    if (building_id == "church") return p.church ? 1 : 0;
    if (building_id == "militia_muster") return p.militia_muster ? 1 : 0;
    return 0;
}

bool building_present(const Province& p, const std::string& building_id) {
    return building_level(p, building_id) > 0;
}

int province_slots_occupied(const CampaignState& state, const std::string& province_id) {
    auto pit = state.provinces.find(province_id);
    if (pit == state.provinces.end()) return 0;
    const Province& p = pit->second;
    int n = 0;
    ensure_default_catalog();
    for (const auto& kv : g_catalog.by_id) {
        if (building_present(p, kv.first)) ++n;
    }
    for (const auto& job : state.progress.construction_jobs) {
        if (job.status != "underway" || job.province_id != province_id) continue;
        if (job.is_upgrade) continue;  // upgrade keeps the same slot
        if (building_present(p, job.building_id)) continue;
        ++n;
    }
    return n;
}

int building_cost(const std::string& building_id, int current_level) {
    const BuildingDef* d = find_def(building_id);
    if (!d) return -1;
    if (is_track(*d)) {
        // Align with logistics_tuning development costs when available.
        const int tuned = development_cost(building_id, current_level);
        if (tuned >= 0) return tuned;
        return d->gold_cost + std::max(0, current_level) * 20;
    }
    return d->gold_cost;
}

int building_build_hours(const std::string& building_id) {
    const BuildingDef* d = find_def(building_id);
    if (!d) return 24;
    return std::max(1, d->build_time_days) * 24;
}

std::string building_preview(const CampaignState& state, const std::string& province_id,
                             const std::string& building_id) {
    const std::string code = construction_block(state, province_id, building_id);
    if (!code.empty()) return block_reason_text(state, province_id, building_id, code);
    const BuildingDef* d = find_def(building_id);
    const int level = building_level(state.provinces.at(province_id), building_id);
    const int cost = building_cost(building_id, level);
    const int days = std::max(1, d->build_time_days);
    std::ostringstream oss;
    oss << (level > 0 ? "Upgrade" : "Build") << " " << d->name;
    if (is_track(*d)) oss << " to L" << (level + 1);
    oss << ": " << cost << " gold, " << days << " day" << (days == 1 ? "" : "s") << ". ";
    oss << d->campaign_effect;
    if (!d->battle_effect.empty()) oss << " Battle: " << d->battle_effect;
    return oss.str();
}

CampaignState start_construction(const CampaignState& state, const std::string& province_id,
                                 const std::string& building_id, std::string& error) {
    error = construction_block(state, province_id, building_id);
    if (!error.empty()) return state;
    CampaignState s = state;
    ensure_logistics_defaults(s);
    const BuildingDef* d = find_def(building_id);
    auto pit = s.provinces.find(province_id);
    const int level = building_level(pit->second, building_id);
    const bool upgrading = level > 0;
    const int cost = building_cost(building_id, level);
    s.player_gold -= cost;
    ConstructionJob job;
    job.id = next_job_id(s);
    job.province_id = province_id;
    job.building_id = building_id;
    job.is_upgrade = upgrading;
    job.hours_total = building_build_hours(building_id);
    job.hours_remaining = job.hours_total;
    job.paid_gold = cost;
    job.status = "underway";
    s.progress.construction_jobs.push_back(job);
    {
        ResourceChange ch;
        ch.reason = (upgrading ? "Upgrade " : "Build ") + d->name + ", " + pit->second.name + ": -" +
                    std::to_string(cost) + " gold";
        ch.gold = -cost;
        ch.province_id = province_id;
        detail::record_resource_change(s, ch);
    }
    return s;
}

CampaignState cancel_construction(const CampaignState& state, const std::string& province_id,
                                  const std::string& building_id, std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
    ConstructionJob* job = nullptr;
    for (auto& j : s.progress.construction_jobs) {
        if (j.status == "underway" && j.province_id == province_id && j.building_id == building_id) {
            job = &j;
            break;
        }
    }
    if (!job) {
        error = "no_construction";
        return state;
    }
    auto pit = s.provinces.find(province_id);
    if (pit == s.provinces.end()) {
        error = "unknown_province";
        return state;
    }
    if (pit->second.owner != s.player_faction) {
        error = "not_your_province";
        return state;
    }
    const int refund = job->paid_gold;
    s.player_gold += refund;
    // Finished records are dropped, not kept: nothing reads them and the save would grow forever.
    s.progress.construction_jobs.erase(s.progress.construction_jobs.begin() +
                                       (job - s.progress.construction_jobs.data()));
    {
        const BuildingDef* d = find_def(building_id);
        ResourceChange ch;
        ch.reason = "Cancel " + (d ? d->name : building_id) + ", " + pit->second.name + ": +" +
                    std::to_string(refund) + " gold";
        ch.gold = refund;
        ch.province_id = province_id;
        detail::record_resource_change(s, ch);
    }
    return s;
}

CampaignState demolish_building(const CampaignState& state, const std::string& province_id,
                                const std::string& building_id, std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
    ensure_default_catalog();
    const BuildingDef* d = find_def(building_id);
    if (!d) {
        error = "unknown_building";
        return state;
    }
    auto pit = s.provinces.find(province_id);
    if (pit == s.provinces.end()) {
        error = "unknown_province";
        return state;
    }
    if (pit->second.owner != s.player_faction) {
        error = "not_your_province";
        return state;
    }
    if (s.pending_battle.active) {
        error = "battle_pending";
        return state;
    }
    if (find_job(s, province_id, building_id)) {
        error = "under_construction";
        return state;
    }
    const int level = building_level(pit->second, building_id);
    if (level <= 0) {
        error = "not_present";
        return state;
    }
    const int refund = demolish_refund(building_id, level);
    if (int* lvl = track_level_ptr(pit->second, building_id)) {
        *lvl = 0;
    } else if (building_id == "church") {
        pit->second.church = false;
    } else if (building_id == "militia_muster") {
        pit->second.militia_muster = false;
    } else {
        error = "unknown_building";
        return state;
    }
    s.player_gold += refund;
    // Pulling a building down takes back the Support its completion gave. Without this,
    // demolish + rebuild of a Meeting House would mint Support for a few gold each cycle.
    const int support_before = s.local_support;
    if (d->support_on_complete > 0)
        s.local_support = detail::clamp_int(s.local_support - d->support_on_complete, 0, 100);
    const int support_delta = s.local_support - support_before;
    {
        ResourceChange ch;
        // Not a refund: the works are gone. What comes back is salvage sold off.
        ch.reason = "Demolish " + d->name + ", " + pit->second.name + ": +" + std::to_string(refund) +
                    " gold salvage (timber, nails, brick)";
        if (support_delta != 0) ch.reason += ", " + std::to_string(support_delta) + " Support";
        ch.gold = refund;
        ch.local_support = support_delta;
        ch.province_id = province_id;
        detail::record_resource_change(s, ch);
    }
    return s;
}

void tick_construction_jobs(CampaignState& s) {
    ensure_default_catalog();
    auto& jobs = s.progress.construction_jobs;
    for (auto& job : jobs) {
        if (job.status != "underway") continue;
        auto pit = s.provinces.find(job.province_id);
        if (pit == s.provinces.end()) {
            job.status = "cancelled";
            continue;
        }
        Province& p = pit->second;
        const BuildingDef* d = find_def(job.building_id);
        const std::string name = d ? d->name : job.building_id;
        // The town changed hands mid-build: the works are abandoned and the gold is lost. Without
        // this the job would finish for the new owner (a free Fort for the garrison that took it).
        if (p.owner != s.player_faction) {
            job.status = "cancelled";
            ResourceChange ch;
            ch.reason = name + " abandoned, " + p.name + ": town lost";
            ch.province_id = job.province_id;
            detail::record_resource_change(s, ch);
            continue;
        }
        if (job.hours_remaining > 0) --job.hours_remaining;
        if (job.hours_remaining > 0) continue;
        if (int* lvl = track_level_ptr(p, job.building_id)) {
            *lvl = std::min(*lvl + 1, d ? std::max(1, d->max_level) : development_max_level());
        } else if (job.building_id == "church") {
            p.church = true;
            if (d && d->support_on_complete != 0) {
                const int before = s.local_support;
                s.local_support = detail::clamp_int(s.local_support + d->support_on_complete, 0, 100);
                ResourceChange ch;
                ch.reason = "Meeting House complete, " + p.name + ": +" +
                            std::to_string(s.local_support - before) + " Support";
                ch.local_support = s.local_support - before;
                ch.province_id = job.province_id;
                detail::record_resource_change(s, ch);
            }
        } else if (job.building_id == "militia_muster") {
            p.militia_muster = true;
        }
        job.status = "complete";
        ResourceChange ch;
        ch.reason = name + " complete, " + p.name;
        ch.province_id = job.province_id;
        detail::record_resource_change(s, ch);
    }
    // Drop finished records (see cancel_construction).
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                              [](const ConstructionJob& j) { return j.status != "underway"; }),
               jobs.end());
}

void apply_building_monthly(CampaignState& s) {
    ensure_default_catalog();
    for (auto& pk : s.provinces) pk.second.manpower_used = 0;  // a new month's men come of age
    int support_gain = 0;
    for (const auto& pk : s.provinces) {
        if (pk.second.owner != s.player_faction) continue;
        for (const auto& kv : g_catalog.by_id) {
            if (!building_present(pk.second, kv.first)) continue;
            if (kv.second.support_per_month > 0) support_gain += kv.second.support_per_month;
        }
    }
    settle_monthly_budget(s);  // trade, workshop output, pay, upkeep, interest
    settle_enemy_budget(s);    // the other side keeps the same accounts, and reinforces when it can
    if (support_gain > 0) {
        const int before = s.local_support;
        s.local_support = detail::clamp_int(s.local_support + support_gain, 0, 100);
        ResourceChange ch;
        ch.reason = "Meeting House monthly: +" + std::to_string(s.local_support - before) + " Support";
        ch.local_support = s.local_support - before;
        detail::record_resource_change(s, ch);
    }
}

std::string city_view_json(const CampaignState& state, const std::string& province_id) {
    ensure_default_catalog();
    ensure_default_sizes();
    json j;
    auto pit = state.provinces.find(province_id);
    if (pit == state.provinces.end()) {
        j["ok"] = false;
        j["error"] = "unknown_province";
        return j.dump();
    }
    const Province& p = pit->second;
    const int slots = province_slot_count(province_id);
    const int occupied = province_slots_occupied(state, province_id);
    j["ok"] = true;
    j["province_id"] = province_id;
    j["name"] = p.name;
    j["owner"] = p.owner;
    j["yours"] = p.owner == state.player_faction;
    {
        auto sz = g_province_size.find(province_id);
        const std::string key = sz == g_province_size.end() ? "small_town" : sz->second;
        j["size_label"] = key == "city" ? "City" : key == "town" ? "Town" : "Small town";
    }
    j["supply_value"] = effective_supply_value(p);
    j["battle_pending"] = state.pending_battle.active;
    j["slot_count"] = slots;
    j["slots_occupied"] = occupied;
    j["player_gold"] = state.player_gold;
    j["local_support"] = state.local_support;
    const ProvinceStores& st = province_ledger(state, province_id);
    j["stores"] = {{"powder", st.powder},
                   {"cartridges", st.cartridges},
                   {"muskets", st.muskets},
                   {"provisions", st.provisions}};
    j["development"] = province_development(p);
    j["defense_bonus"] = p.fort_level * 3;
    json filled = json::array();
    json constructing = json::array();
    for (const auto& kv : g_catalog.by_id) {
        const int level = building_level(p, kv.first);
        const ConstructionJob* job = find_job(state, province_id, kv.first);
        if (level > 0) {
            filled.push_back({{"building_id", kv.first},
                              {"name", kv.second.name},
                              {"level", level},
                              {"max_level", std::max(1, kv.second.max_level)},
                              {"description", kv.second.description},
                              {"upgrade_days", std::max(1, kv.second.build_time_days)},
                              {"kind", kv.second.kind},
                              {"campaign_effect", kv.second.campaign_effect},
                              {"battle_effect", kv.second.battle_effect},
                              {"can_upgrade", is_track(kv.second) && level < kv.second.max_level},
                              {"upgrade_cost",
                               is_track(kv.second) ? building_cost(kv.first, level) : -1},
                              {"upgrade_available",
                               construction_block(state, province_id, kv.first).empty()},
                              {"upgrade_preview", building_preview(state, province_id, kv.first)},
                              {"demolish_refund", demolish_refund(kv.first, level)}});
        }
        if (job) {
            constructing.push_back(
                {{"building_id", kv.first},
                 {"name", kv.second.name},
                 {"is_upgrade", job->is_upgrade},
                 {"target_level", level + 1},
                 {"campaign_effect", kv.second.campaign_effect},
                 {"battle_effect", kv.second.battle_effect},
                 {"description", kv.second.description},
                 {"hours_remaining", job->hours_remaining},
                 {"hours_total", job->hours_total},
                 {"days_remaining", (job->hours_remaining + 23) / 24},
                 {"progress", job->hours_total > 0
                                  ? 1.0 - static_cast<double>(job->hours_remaining) /
                                                static_cast<double>(job->hours_total)
                                  : 1.0},
                 {"paid_gold", job->paid_gold}});
        }
    }
    j["filled"] = filled;
    j["constructing"] = constructing;
    json options = json::array();
    for (const auto& kv : g_catalog.by_id) {
        const int level = building_level(p, kv.first);
        const bool can_new = level == 0;
        const bool can_up = is_track(kv.second) && level > 0 && level < kv.second.max_level;
        if (!can_new && !can_up) continue;
        const std::string err = construction_block(state, province_id, kv.first);
        options.push_back({{"building_id", kv.first},
                           {"name", kv.second.name},
                           {"kind", kv.second.kind},
                           {"is_upgrade", can_up},
                           {"cost", building_cost(kv.first, level)},
                           {"build_time_days", kv.second.build_time_days},
                           {"campaign_effect", kv.second.campaign_effect},
                           {"battle_effect", kv.second.battle_effect},
                           {"description", kv.second.description},
                           {"available", err.empty()},
                           {"reason", err.empty() ? std::string()
                                                    : block_reason_text(state, province_id, kv.first, err)}});
    }
    j["options"] = options;
    json sources = json::array();
    if (p.farm_level > 0)
        sources.push_back({{"id", "farm"},
                           {"label", "Farm L" + std::to_string(p.farm_level)},
                           {"provisions_per_month", p.farm_level * 3},
                           {"income_gold", 15 * p.farm_level}});
    if (p.workshop_level > 0)
        sources.push_back({{"id", "workshop"},
                           {"label", "Workshop L" + std::to_string(p.workshop_level)},
                           {"cartridge_budget", p.workshop_level * workshop_cartridges_per_level()}});
    if (p.depot_level > 0)
        sources.push_back(
            {{"id", "depot"}, {"label", "Depot L" + std::to_string(p.depot_level)}, {"readiness", p.depot_level * 2}});
    if (p.fort_level > 0)
        sources.push_back({{"id", "fort"},
                           {"label", "Fort L" + std::to_string(p.fort_level)},
                           {"defense_bonus", p.fort_level * 3}});
    if (p.church) {
        const BuildingDef* church_def = find_def("church");
        sources.push_back({{"id", "church"},
                           {"label", "Meeting House"},
                           {"support_per_month", church_def ? church_def->support_per_month : 0}});
    }
    if (p.militia_muster)
        sources.push_back({{"id", "militia_muster"}, {"label", "Militia Muster"}, {"battle", "extra muster"}});
    j["sources"] = sources;
    j["empty_slots"] = std::max(0, slots - occupied);
    j["terrain"] = p.terrain;
    j["stores"]["bayonets"] = st.bayonets;
    j["stores"]["lead"] = st.lead;
    j["stores"]["paper"] = st.paper;
    j["battle_here"] = state.pending_battle.active && state.pending_battle.province == province_id;
    // Forces in the town. An enemy's strength is not the player's to read.
    json forces = json::array();
    for (const auto& kv : state.armies) {
        const Army& a = kv.second;
        if (a.province != province_id) continue;
        const bool yours = a.faction == state.player_faction;
        json f = {{"army_id", a.id}, {"name", a.name}, {"yours", yours}, {"marching", !a.path.empty()}};
        if (yours) {
            int sick = 0, wounded = 0;
            for (const auto& u : a.sick) sick += u.second;
            for (const auto& u : a.wounded) wounded += u.second;
            f["fit"] = detail::total_units(a);
            f["sick"] = sick;
            f["wounded"] = wounded;
            f["morale"] = a.morale;
            f["readiness"] = a.readiness;
            f["fatigue"] = a.fatigue;
            f["units"] = a.units;
        }
        forces.push_back(f);
    }
    j["forces"] = forces;
    j["enemy_interest"] = state.ai_objective == province_id;
    // What deserters and townsfolk would tell: whether the garrison's pay has stopped, and whether
    // ships still supply the place. Never the size of the chest.
    const bool enemy_town = !p.owner.empty() && p.owner == enemy_faction(state);
    j["garrison_unpaid_months"] = enemy_town ? state.progress.enemy_arrears_months : 0;
    j["supplied_by_sea"] = enemy_town && supplied_by_sea(state, p.owner) &&
                           default_depot_province(state, p.owner) == province_id;  // an unconfirmed report, as before
    j["manpower_cap"] = province_manpower_cap(state, province_id);
    j["manpower_used"] = std::max(0, p.manpower_used);
    if (const RecruitJob* m = find_muster(state, province_id)) {
        const RecruitDef* d = find_recruit(m->unit_id);
        j["mustering"] = {{"unit_id", m->unit_id},
                          {"name", d ? d->name : m->unit_id},
                          {"men", m->men},
                          {"days_remaining", (m->hours_remaining + 23) / 24},
                          {"progress", m->hours_total > 0
                                           ? 1.0 - static_cast<double>(m->hours_remaining) /
                                                         static_cast<double>(m->hours_total)
                                           : 1.0},
                          {"paid_gold", m->paid_gold}};
    }
    json recruits = json::array();
    for (const auto& kv : g_catalog.recruits) {
        const RecruitDef& d = kv.second;
        if (!d.faction.empty() && d.faction != state.player_faction) continue;  // not this side's troops
        const std::string err = recruit_block(state, province_id, d.id);
        const BuildingDef* need = d.requires_building.empty() ? nullptr : find_def(d.requires_building);
        recruits.push_back({{"unit_id", d.id},
                            {"name", d.name},
                            {"unit_class", d.unit_class},
                            {"role", d.role},
                            {"men", d.men},
                            {"gold", d.gold},
                            {"muskets", d.muskets},
                            {"provisions", d.provisions},
                            {"days", d.days},
                            {"requires", need ? need->name : std::string()},
                            {"description", d.description},
                            {"available", err.empty()},
                            {"reason", err.empty() ? std::string()
                                                   : recruit_reason_text(state, province_id, d.id, err)}});
    }
    j["recruits"] = recruits;
    return j.dump();
}

void buildings_monthly_tick(CampaignState& s) { apply_building_monthly(s); }

int building_battle_morale(const Province& p) {
    ensure_default_catalog();
    if (!p.church) return 0;
    if (const BuildingDef* d = find_def("church")) return d->battle_morale;
    return 0;
}

int province_manpower_cap(const CampaignState& state, const std::string& province_id) {
    ensure_default_catalog();
    ensure_default_sizes();
    auto sz = g_province_size.find(province_id);
    const std::string key = sz == g_province_size.end() ? "small_town" : sz->second;
    auto mp = g_catalog.manpower.find(key);
    const int base = mp == g_catalog.manpower.end() ? 6 : mp->second;
    // A town that favours the cause turns out more men: the base holds at Support 50.
    return (base * detail::clamp_int(state.local_support, 25, 100)) / 50;
}

std::string recruitment_preview(const CampaignState& state, const std::string& province_id,
                                const std::string& unit_id) {
    const std::string code = recruit_block(state, province_id, unit_id);
    if (!code.empty()) return recruit_reason_text(state, province_id, unit_id, code);
    const RecruitDef* d = find_recruit(unit_id);
    std::ostringstream oss;
    oss << "Raise " << d->name << ": " << d->men << " men, " << d->gold << " gold";
    if (d->muskets > 0) oss << ", " << d->muskets << " muskets";
    if (d->provisions > 0) oss << ", " << d->provisions << " provisions";
    oss << ", " << d->days << " day" << (d->days == 1 ? "" : "s") << ".";
    return oss.str();
}

CampaignState start_recruitment(const CampaignState& state, const std::string& province_id,
                                const std::string& unit_id, std::string& error) {
    error = recruit_block(state, province_id, unit_id);
    if (!error.empty()) return state;
    CampaignState s = state;
    ensure_logistics_defaults(s);
    const RecruitDef* d = find_recruit(unit_id);
    Province& p = s.provinces.at(province_id);
    ProvinceStores& st = province_ledger(s, province_id);
    s.player_gold -= d->gold;
    st.muskets -= d->muskets;
    st.provisions -= d->provisions;
    p.manpower_used = std::max(0, p.manpower_used) + d->men;
    RecruitJob job;
    job.id = "muster_" + std::to_string(s.next_command_seq++);
    job.province_id = province_id;
    job.unit_id = d->id;
    job.unit_class = d->unit_class;
    job.men = d->men;
    job.hours_total = d->days * 24;
    job.hours_remaining = job.hours_total;
    job.paid_gold = d->gold;
    job.paid_muskets = d->muskets;
    job.paid_provisions = d->provisions;
    s.progress.recruit_jobs.push_back(job);
    ResourceChange ch;
    ch.reason = "Muster " + d->name + ", " + p.name + ": -" + std::to_string(d->gold) + " gold";
    if (d->muskets > 0) ch.reason += ", -" + std::to_string(d->muskets) + " muskets";
    if (d->provisions > 0) ch.reason += ", -" + std::to_string(d->provisions) + " provisions";
    ch.gold = -d->gold;
    ch.muskets = -d->muskets;
    ch.provisions = -d->provisions;
    ch.province_id = province_id;
    detail::record_resource_change(s, ch);
    return s;
}

CampaignState cancel_recruitment(const CampaignState& state, const std::string& province_id,
                                 std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
    auto& jobs = s.progress.recruit_jobs;
    auto it = std::find_if(jobs.begin(), jobs.end(),
                           [&](const RecruitJob& j) { return j.province_id == province_id; });
    if (it == jobs.end()) {
        error = "no_muster";
        return state;
    }
    auto pit = s.provinces.find(province_id);
    if (pit == s.provinces.end() || pit->second.owner != s.player_faction) {
        error = "not_your_province";
        return state;
    }
    // The men go home with nothing spent: purse, arms, rations and the town's manpower all return.
    const RecruitJob job = *it;
    jobs.erase(it);
    s.player_gold += job.paid_gold;
    ProvinceStores& st = province_ledger(s, province_id);
    st.muskets += job.paid_muskets;
    st.provisions += job.paid_provisions;
    pit->second.manpower_used = std::max(0, pit->second.manpower_used - job.men);
    ResourceChange ch;
    ch.reason = "Muster dismissed, " + pit->second.name + ": +" + std::to_string(job.paid_gold) + " gold";
    ch.gold = job.paid_gold;
    ch.muskets = job.paid_muskets;
    ch.provisions = job.paid_provisions;
    ch.province_id = province_id;
    detail::record_resource_change(s, ch);
    return s;
}

void tick_recruitment_jobs(CampaignState& s) {
    ensure_default_catalog();
    auto& jobs = s.progress.recruit_jobs;
    std::vector<RecruitJob> keep;
    for (RecruitJob job : jobs) {
        auto pit = s.provinces.find(job.province_id);
        if (pit == s.provinces.end()) continue;
        Province& p = pit->second;
        const RecruitDef* d = find_recruit(job.unit_id);
        const std::string name = d ? d->name : job.unit_id;
        if (p.owner != s.player_faction) {
            // The town fell before the company formed: the men scatter and the outlay is lost.
            ResourceChange ch;
            ch.reason = name + " dispersed, " + p.name + ": town lost";
            ch.province_id = job.province_id;
            detail::record_resource_change(s, ch);
            continue;
        }
        if (job.hours_remaining > 0) --job.hours_remaining;
        if (job.hours_remaining > 0) {
            keep.push_back(job);
            continue;
        }
        std::string army_id = idle_player_army_at(s, job.province_id);
        ResourceChange ch;
        ch.province_id = job.province_id;
        if (army_id.empty()) {
            Army a;
            a.id = "raised_" + job.province_id + "_" + std::to_string(s.next_command_seq++);
            a.name = p.name + " Company";
            a.faction = s.player_faction;
            a.province = job.province_id;
            a.previous_province = job.province_id;
            a.units[job.unit_class] = job.men;
            a.morale = kRecruitMorale;
            a.readiness = 80;
            s.armies[a.id] = a;
            ch.reason = name + " mustered, " + p.name + ": " + std::to_string(job.men) +
                        " men form the " + a.name;
        } else {
            Army& a = s.armies.at(army_id);
            const int before = std::max(0, detail::total_units(a));
            a.units[job.unit_class] += job.men;
            // New men dilute or lift the army's temper in proportion to their number.
            a.morale = detail::clamp_int(
                (a.morale * before + kRecruitMorale * job.men) / std::max(1, before + job.men), 0, 100);
            ch.reason = name + " mustered, " + p.name + ": " + std::to_string(job.men) + " men join " +
                        a.name;
        }
        detail::record_resource_change(s, ch);
    }
    jobs = keep;
    merge_idle_player_armies(s);
}

void merge_idle_player_armies(CampaignState& s) {
    if (s.pending_battle.active) return;
    std::map<std::string, std::string> survivor_at;  // province -> army id
    std::vector<std::pair<std::string, std::string>> merges;  // gone -> survivor
    for (const auto& kv : s.armies) {
        const Army& a = kv.second;
        if (a.faction != s.player_faction || !a.path.empty()) continue;
        auto it = survivor_at.find(a.province);
        if (it == survivor_at.end()) survivor_at[a.province] = kv.first;
        else if (survivor_before(kv.first, it->second)) it->second = kv.first;
    }
    for (const auto& kv : s.armies) {
        const Army& a = kv.second;
        if (a.faction != s.player_faction || !a.path.empty()) continue;
        const std::string& keep = survivor_at.at(a.province);
        // Only companies the player raised are absorbed. Two scripted armies in one town (the
        // defected garrison beside the militia) stay separate: letters and treaties name them.
        if (keep != kv.first && kv.first.rfind("raised_", 0) == 0) merges.push_back({kv.first, keep});
    }
    for (const auto& m : merges) {
        const Army gone = s.armies.at(m.first);
        Army& keep = s.armies.at(m.second);
        const int a = std::max(0, detail::total_units(keep));
        const int b = std::max(0, detail::total_units(gone));
        for (const auto& u : gone.units) keep.units[u.first] += u.second;
        for (const auto& u : gone.sick) keep.sick[u.first] += u.second;
        for (const auto& u : gone.wounded) keep.wounded[u.first] += u.second;
        if (a + b > 0) {
            keep.morale = (keep.morale * a + gone.morale * b) / (a + b);
            keep.readiness = (keep.readiness * a + gone.readiness * b) / (a + b);
            keep.fatigue = (keep.fatigue * a + gone.fatigue * b) / (a + b);
        }
        // Anything that named the absorbed army now names the one it joined.
        for (auto& pc : s.progress.personnel_commitments)
            if (pc.army_id == m.first) pc.army_id = m.second;
        for (auto& cj : s.progress.cartridge_jobs)
            if (cj.army_id == m.first) cj.army_id = m.second;
        const std::string where = s.provinces.count(keep.province) ? s.provinces.at(keep.province).name
                                                                    : keep.province;
        detail::add_log(s, gone.name + " joins " + keep.name + " at " + where);
        s.armies.erase(m.first);
    }
}

int town_levy_gold(const Province& p) {
    ensure_default_catalog();
    return g_catalog.economy.levy_per_supply * effective_supply_value(p);
}

int treasury_credit_limit(const CampaignState& state) {
    ensure_default_catalog();
    int towns = 0;
    for (const auto& pk : state.provinces) towns += pk.second.owner == state.player_faction ? 1 : 0;
    return towns * g_catalog.economy.credit_per_town;
}

int monthly_net_gold(const CampaignState& state) {
    const Budget b = monthly_budget(state, state.player_faction);
    return b.income_total - b.expense_total;
}

std::string treasury_json(const CampaignState& state) {
    const Budget b = monthly_budget(state, state.player_faction);
    const EconomyTuning& t = g_catalog.economy;
    json j;
    j["gold"] = state.player_gold;
    j["debt"] = std::max(0, state.progress.treasury_debt);
    j["credit_limit"] = treasury_credit_limit(state);
    j["loan_step"] = t.loan_step;
    j["loan_interest_pct"] = t.loan_interest_pct;
    j["arrears_months"] = state.progress.pay_arrears_months;
    json inc = json::array(), exp = json::array();
    for (const auto& l : b.income) inc.push_back({{"label", l.label}, {"gold", l.gold}});
    for (const auto& l : b.expenses) exp.push_back({{"label", l.label}, {"gold", l.gold}});
    j["income"] = inc;
    j["expenses"] = exp;
    j["income_total"] = b.income_total;
    j["expense_total"] = b.expense_total;
    j["net"] = b.income_total - b.expense_total;
    j["muskets"] = b.muskets;
    j["provisions"] = b.provisions;
    std::string e1, e2;
    take_loan(state, e1);
    repay_loan(state, e2);
    j["can_borrow"] = e1.empty();
    j["can_repay"] = e2.empty();
    return j.dump();
}

CampaignState take_loan(const CampaignState& state, std::string& error) {
    error.clear();
    ensure_default_catalog();
    const EconomyTuning& t = g_catalog.economy;
    if (state.pending_battle.active) {
        error = "battle_pending";
        return state;
    }
    if (std::max(0, state.progress.treasury_debt) + t.loan_step > treasury_credit_limit(state)) {
        error = "credit_exhausted";
        return state;
    }
    CampaignState s = state;
    s.player_gold += t.loan_step;
    s.progress.treasury_debt = std::max(0, s.progress.treasury_debt) + t.loan_step;
    ResourceChange ch;
    ch.reason = "Bills of credit issued: +" + std::to_string(t.loan_step) + " gold (debt " +
                std::to_string(s.progress.treasury_debt) + ")";
    ch.gold = t.loan_step;
    detail::record_resource_change(s, ch);
    return s;
}

CampaignState repay_loan(const CampaignState& state, std::string& error) {
    error.clear();
    ensure_default_catalog();
    const int debt = std::max(0, state.progress.treasury_debt);
    if (debt <= 0) {
        error = "no_debt";
        return state;
    }
    const int pay = std::min(debt, g_catalog.economy.loan_step);
    if (state.player_gold < pay) {
        error = "insufficient_gold";
        return state;
    }
    CampaignState s = state;
    s.player_gold -= pay;
    s.progress.treasury_debt = debt - pay;
    ResourceChange ch;
    ch.reason = "Bills of credit redeemed: -" + std::to_string(pay) + " gold (debt " +
                std::to_string(s.progress.treasury_debt) + ")";
    ch.gold = -pay;
    detail::record_resource_change(s, ch);
    return s;
}

void settle_monthly_budget(CampaignState& s) {
    const Budget b = monthly_budget(s, s.player_faction);
    const EconomyTuning& t = g_catalog.economy;
    auto log_gold = [&s](const std::string& reason, int gold) {
        ResourceChange ch;
        ch.reason = reason;
        ch.gold = gold;
        detail::record_resource_change(s, ch);
    };
    // Income first. The town levy itself is credited by the clock (monthly_supply) just before this.
    if (b.trade > 0) {
        s.player_gold += b.trade;
        log_gold("Depot trade: +" + std::to_string(b.trade) + " gold", b.trade);
    }
    if (b.subsidy > 0) {
        s.player_gold += b.subsidy;
        log_gold("Subsidy landed by sea: +" + std::to_string(b.subsidy) + " gold", b.subsidy);
        province_ledger(s, default_depot_province(s, s.player_faction)).provisions += t.sea_provisions;
    }
    if (t.workshop_muskets > 0) {
        // Every workshop works for whoever holds its town; only the player's are in the ledger.
        for (const auto& pk : s.provinces) {
            const Province& p = pk.second;
            if (p.owner.empty() || p.workshop_level <= 0) continue;
            const int made = p.workshop_level * t.workshop_muskets;
            province_ledger(s, pk.first).muskets += made;
            if (p.owner != s.player_faction) continue;
            ResourceChange ch;
            ch.reason = "Workshop, " + p.name + ": +" + std::to_string(made) + " muskets";
            ch.muskets = made;
            ch.province_id = pk.first;
            detail::record_resource_change(s, ch);
        }
    }
    // Expenses: interest, upkeep, then pay. What the purse cannot meet is borrowed up to the
    // credit limit; past that the soldiers go unpaid.
    int due = b.expense_total;
    if (due <= 0) {
        s.progress.pay_arrears_months = 0;
        return;
    }
    int short_by = std::max(0, due - s.player_gold);
    if (short_by > 0) {
        const int room = std::max(0, treasury_credit_limit(s) - std::max(0, s.progress.treasury_debt));
        const int borrowed = std::min(short_by, room);
        if (borrowed > 0) {
            s.player_gold += borrowed;
            s.progress.treasury_debt = std::max(0, s.progress.treasury_debt) + borrowed;
            log_gold("Borrowed to meet the month's bills: +" + std::to_string(borrowed) + " gold (debt " +
                         std::to_string(s.progress.treasury_debt) + ")",
                     borrowed);
            short_by -= borrowed;
        }
    }
    const int paid = std::min(due, s.player_gold);
    s.player_gold -= paid;
    if (b.interest > 0) log_gold("Interest on debt: -" + std::to_string(b.interest) + " gold", -b.interest);
    if (b.upkeep > 0) log_gold("Building upkeep: -" + std::to_string(b.upkeep) + " gold", -b.upkeep);
    if (b.pay > 0) log_gold("Army pay: -" + std::to_string(b.pay) + " gold", -b.pay);
    if (short_by > 0) {
        ++s.progress.pay_arrears_months;
        for (auto& ak : s.armies) {
            if (ak.second.faction != s.player_faction) continue;
            ak.second.morale = detail::clamp_int(ak.second.morale - t.unpaid_morale, 0, 100);
        }
        const int before = s.local_support;
        s.local_support = detail::clamp_int(s.local_support - t.unpaid_support, 0, 100);
        ResourceChange ch;
        ch.reason = "Pay in arrears by " + std::to_string(short_by) + " gold: the army's morale falls (-" +
                    std::to_string(t.unpaid_morale) + "), Support " + std::to_string(s.local_support - before);
        ch.local_support = s.local_support - before;
        ch.gold = short_by;  // the logged expense lines above overstate what left the purse by this much
        detail::record_resource_change(s, ch);
    } else {
        s.progress.pay_arrears_months = 0;
    }
}

std::string enemy_faction(const CampaignState& state) {
    return state.player_faction == kBritish ? kContinental : kBritish;
}

int enemy_monthly_net_gold(const CampaignState& state) {
    const Budget b = monthly_budget(state, enemy_faction(state));
    return b.income_total - b.expense_total;
}

void settle_enemy_budget(CampaignState& s) {
    ensure_default_catalog();
    const EconomyTuning& t = g_catalog.economy;
    const std::string faction = enemy_faction(s);
    const Budget b = monthly_budget(s, faction);
    bool present = b.income_total > 0 || b.expense_total > 0;
    if (!present) return;  // no towns and no army: nothing to account for
    const std::string hq = default_depot_province(s, faction);
    int& chest = s.progress.enemy_gold;
    chest = std::max(0, chest) + b.income_total;
    if (b.subsidy > 0) province_ledger(s, hq).provisions += t.sea_provisions;
    // No credit: what the chest cannot meet goes unpaid, and the army's temper pays for it.
    const int paid = std::min(chest, b.expense_total);
    chest -= paid;
    if (paid < b.expense_total) {
        ++s.progress.enemy_arrears_months;
        for (auto& ak : s.armies) {
            if (ak.second.faction != faction) continue;
            ak.second.morale = detail::clamp_int(ak.second.morale - t.unpaid_morale, 0, 100);
        }
        detail::add_log(s, faction + " army unpaid (" + std::to_string(b.expense_total - paid) +
                               " gold short): morale falls");
        return;
    }
    s.progress.enemy_arrears_months = 0;
    // Gold to spare buys men: the cheapest company this side may raise in a town it holds that has
    // the building, the arms and the rations for it. One a month, and never below the reserve.
    const RecruitDef* pick = nullptr;
    std::string where;
    for (const auto& rk : g_catalog.recruits) {
        const RecruitDef& d = rk.second;
        if (d.faction != faction || !d.requires_chapter.empty()) continue;
        if (chest - d.gold < t.enemy_reserve_gold) continue;
        if (pick && d.gold >= pick->gold) continue;
        for (const auto& pk : s.provinces) {
            const Province& p = pk.second;
            if (p.owner != faction) continue;
            if (!d.requires_building.empty() && !building_present(p, d.requires_building)) continue;
            const ProvinceStores& st = province_ledger(static_cast<const CampaignState&>(s), pk.first);
            if (st.muskets < d.muskets || st.provisions < d.provisions) continue;
            pick = &d;
            where = pk.first;
            break;
        }
    }
    if (!pick) return;
    chest -= pick->gold;
    ProvinceStores& st = province_ledger(s, where);
    st.muskets -= pick->muskets;
    st.provisions -= pick->provisions;
    std::string army_id;
    for (const auto& ak : s.armies) {
        if (ak.second.faction == faction && ak.second.province == where) {
            army_id = ak.first;
            break;
        }
    }
    const std::string town = s.provinces.at(where).name;
    if (army_id.empty()) {
        Army a;
        a.id = "raised_" + where + "_" + std::to_string(s.next_command_seq++);
        a.name = town + " Garrison";
        a.faction = faction;
        a.province = where;
        a.previous_province = where;
        a.units[pick->unit_class] = pick->men;
        a.morale = kRecruitMorale;
        a.readiness = 80;
        s.armies[a.id] = a;
    } else {
        s.armies.at(army_id).units[pick->unit_class] += pick->men;
    }
    detail::add_log(s, faction + " raise a " + pick->name + " at " + town + " (+" +
                           std::to_string(pick->men) + " men)");
}

}  // namespace pa250
