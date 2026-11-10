#include "internal.hpp"

#include <cctype>
#include <nlohmann/json.hpp>
#include <sstream>

namespace pa250 {
namespace {

using json = nlohmann::json;

struct LogisticsTuning {
    int gold_per_powder = 4;
    int gold_per_cartridge = 1;
    int gold_per_musket = 8;
    int gold_per_bayonet = 3;
    int gold_per_provision = 2;
    int gold_per_lead = 1;
    int gold_per_paper = 1;
    int purchase_transit_hours = 8;
    int transfer_transit_hours = 6;
    int craft_powder = 1;
    int craft_lead = 1;
    int craft_paper = 1;
    int craft_cartridges = 10;
    int craft_hours = 4;
    int craft_workers = 2;
    int aftermath_gold = 40;
    int aftermath_powder = 10;
    int aftermath_muskets = 4;
    int aftermath_capacity_bonus_muskets = 2;
    int aftermath_transit = 4;
    bool recover_muskets_on_win = false;
    std::string continental_hq = "cambridge";
    std::string british_hq = "boston";
    int dev_farm_gold = 30;
    int dev_workshop_gold = 40;
    int dev_depot_gold = 35;
    int dev_fort_gold = 45;
    int dev_cost_growth = 20;
    int dev_max_level = 3;
    int farm_provisions_per_month = 3;
    int workshop_cartridges_per_level = 8;
    int farm_readiness_per_level = 2;
    int depot_readiness_per_level = 2;
};

LogisticsTuning g_tuning;
bool g_tuning_loaded = false;

const LogisticsTuning& tuning() { return g_tuning; }

bool debit_province(ProvinceStores& st, int powder, int cartridges, int muskets, int bayonets,
                    int provisions, int lead, int paper) {
    if (st.powder < powder || st.cartridges < cartridges || st.muskets < muskets ||
        st.bayonets < bayonets || st.provisions < provisions || st.lead < lead || st.paper < paper)
        return false;
    st.powder -= powder;
    st.cartridges -= cartridges;
    st.muskets -= muskets;
    st.bayonets -= bayonets;
    st.provisions -= provisions;
    st.lead -= lead;
    st.paper -= paper;
    return true;
}

int committed_workers(const CampaignState& s, const std::string& army_id, const std::string& unit_class) {
    int n = 0;
    for (const auto& pc : s.progress.personnel_commitments) {
        if (pc.army_id != army_id || pc.unit_class != unit_class || pc.count <= 0) continue;
        if (pc.release_on_hour > 0 && s.now.hour_index >= pc.release_on_hour) continue;
        n += pc.count;
    }
    return n;
}

std::string next_ship_id(CampaignState& s, const std::string& prefix) {
    return prefix + "_" + std::to_string(s.next_command_seq++);
}

void distribute_faction_stores_into_provinces(CampaignState& s) {
    auto place = [&](const std::string& faction, const FactionStores& fs, const std::string& primary,
                     const std::string& secondary, const std::string& tertiary) {
        if (fs.powder == 0 && fs.arms == 0 && fs.provisions == 0) return;
        auto& a = s.province_stores[primary];
        auto& b = s.province_stores[secondary];
        auto& c = s.province_stores[tertiary];
        // Deterministic split: half primary, quarter secondary, rest tertiary.
        a.powder += fs.powder / 2;
        b.powder += fs.powder / 4;
        c.powder += fs.powder - fs.powder / 2 - fs.powder / 4;
        a.muskets += fs.arms / 2;
        b.muskets += fs.arms / 4;
        c.muskets += fs.arms - fs.arms / 2 - fs.arms / 4;
        a.provisions += fs.provisions / 2;
        b.provisions += fs.provisions / 4;
        c.provisions += fs.provisions - fs.provisions / 2 - fs.provisions / 4;
        // Soft default cartridges from powder (gameplay tuning migrate).
        a.cartridges += (fs.powder / 2) * 2;
        b.cartridges += (fs.powder / 4) * 2;
        c.cartridges += (fs.powder - fs.powder / 2 - fs.powder / 4) * 2;
    };
    if (s.stores.count(kBritish))
        place(kBritish, s.stores.at(kBritish), "boston", "boston", "boston");
    if (s.stores.count(kContinental))
        place(kContinental, s.stores.at(kContinental), "concord", "lexington", "cambridge");
}

ProvinceStores& faction_depot(CampaignState& s, const std::string& faction) {
    return province_ledger(s, default_depot_province(s, faction));
}

void reset_clerk_day(CampaignState& s) {
    const int64_t day = s.now.hour_index / 24;
    if (s.staff.capacity_day_index != day) {
        s.staff.capacity_day_index = day;
        s.staff.letters_processed_today = 0;
    }
}

bool clerk_has_capacity(const CampaignState& s) {
    return s.staff.letters_processed_today < s.staff.capacity_per_day;
}

std::string next_letter_id(CampaignState& s, const std::string& prefix) {
    return prefix + "_" + std::to_string(s.next_command_seq++);
}

CorrespondenceItem* find_letter(CampaignState& s, const std::string& id) {
    for (auto& c : s.staff.queue)
        if (c.id == id) return &c;
    return nullptr;
}

CorrespondenceItem* find_awaiting(CampaignState& s) {
    for (auto& c : s.staff.queue)
        if (c.awaiting_choice && (c.status == "needs_approval" || c.status == "pending")) return &c;
    return nullptr;
}

void push_event(CampaignState& s, std::vector<Interrupt>* interrupts, const std::string& letter_id) {
    s.pending_event_id = letter_id;
    if (interrupts) interrupts->push_back(Interrupt::EventPopup);
}

uint64_t mix_seed(const CampaignState& s, uint64_t salt) {
    return detail::mix64(s.seed ^ detail::mix64(static_cast<uint64_t>(s.now.hour_index)) ^ salt);
}

// Deterministic outcome for assistance requests: full / partial / delay / refuse.
std::string roll_assistance(const CampaignState& s, CorrespondenceItem& letter) {
    const uint64_t r = mix_seed(s, detail::fnv1a(letter.id)) % 100;
    const int support = detail::clamp_int(s.local_support, 0, 100);
    if (r < static_cast<uint64_t>(10 + (100 - support) / 4)) {
        letter.status = "refused";
        letter.reason = "Committee stocks are committed elsewhere.";
        letter.committed_powder = 0;
        letter.committed_arms = 0;
        return "refused";
    }
    if (r < static_cast<uint64_t>(30 + (100 - support) / 5)) {
        letter.status = "delayed";
        letter.reason = "Wagons are short; expect a later shipment.";
        letter.resolve_on_hour = s.now.hour_index + 24;
        letter.committed_powder = letter.powder;
        letter.committed_arms = letter.arms;
        return "delayed";
    }
    if (r < 70u) {
        letter.status = "partial";
        letter.reason = "Only part of the request can be spared.";
        letter.committed_powder = std::max(1, letter.powder / 2);
        letter.committed_arms = std::max(0, letter.arms / 2);
        letter.resolve_on_hour = s.now.hour_index + 8;
        return "partial";
    }
    letter.status = "approved";
    letter.reason = "The Committee approves the full request.";
    letter.committed_powder = letter.powder;
    letter.committed_arms = letter.arms;
    letter.resolve_on_hour = s.now.hour_index + 6;
    return "approved";
}

void deliver_committed(CampaignState& s, CorrespondenceItem& letter) {
    if (letter.status == "delivered" || letter.status == "refused" || letter.status == "dismissed") return;
    // requisition_aftermath stores a support snapshot, never a powder shipment.
    if (letter.kind == "requisition_aftermath") return;
    if (letter.committed_powder <= 0 && letter.committed_arms <= 0 && letter.committed_provisions <= 0 &&
        letter.kind != "capacity_investment" && letter.kind != "knox_support") {
        letter.status = "delivered";
        return;
    }
    ProvinceStores& st = faction_depot(s, s.player_faction);
    // Never double-count: only add the unpaid remainder.
    const int add_p = letter.committed_powder - letter.received_powder;
    const int add_a = letter.committed_arms - letter.received_arms;
    const int add_v = letter.committed_provisions - letter.received_provisions;
    if (add_p > 0) {
        st.powder += add_p;
        letter.received_powder += add_p;
    }
    if (add_a > 0) {
        st.muskets += add_a;
        letter.received_arms += add_a;
    }
    if (add_v > 0) {
        st.provisions += add_v;
        letter.received_provisions += add_v;
    }
    if (letter.kind == "capacity_investment") {
        // Arms arrive through the committed/received remainder above (committed at choice
        // time), so a re-run of this delivery can never grant them twice.
        s.progress.capacity_level = 1;
        s.progress.capacity_ready_hour = s.now.hour_index;
        detail::add_log(s, "capacity investment ready: repair contact established");
    }
    if (letter.kind == "knox_support" && s.progress.knox.status == "underway") {
        s.progress.knox.status = "delivered";
        s.progress.knox.pieces = 59;
        detail::add_log(s, "knox project: artillery train reported delivered (campaign project)");
    }
    letter.status = "delivered";
    letter.awaiting_choice = false;
    if (add_p || add_a || add_v) {
        ResourceChange ch;
        ch.reason = "Delivered (" + letter.kind + "): +" + std::to_string(std::max(0, add_p)) + " powder, +" +
                    std::to_string(std::max(0, add_a)) + " muskets, +" + std::to_string(std::max(0, add_v)) +
                    " provisions";
        ch.powder = std::max(0, add_p);
        ch.muskets = std::max(0, add_a);
        ch.provisions = std::max(0, add_v);
        ch.province_id = default_depot_province(s, s.player_faction);
        detail::record_resource_change(s, ch);
    } else {
        detail::add_log(s, "delivery received for " + letter.id + " powder+" + std::to_string(std::max(0, add_p)) +
                               " arms+" + std::to_string(std::max(0, add_a)) + " prov+" +
                               std::to_string(std::max(0, add_v)));
    }
}

void seed_aftermath_letter(CampaignState& s, std::vector<Interrupt>* interrupts) {
    if (!s.progress.aftermath_pending) return;
    if (find_awaiting(s)) return;
    reset_clerk_day(s);
    CorrespondenceItem letter;
    letter.id = next_letter_id(s, "aftermath");
    letter.created_hour = s.now.hour_index;
    letter.recipient = "committee_of_safety";
    letter.kind = "request_assistance";
    letter.status = "needs_approval";
    letter.awaiting_choice = true;
    letter.fictionalized = true;
    letter.powder = 12;
    letter.arms = 6;
    letter.subject = "Powder and arms after the Concord clash";
    letter.body =
        "[FICTIONALIZED] Clerk Hale presents three paths: purchase from a merchant, petition the "
        "Committee of Safety, or requisition from local stocks. Recorded history of 19 April is "
        "uncertain on the first shot at Lexington; this campaign does not stage that shot as fact. "
        "Your role is a composite Massachusetts field commander under town authorities and the "
        "Provincial Congress — not Washington's headquarters.";
    s.staff.queue.push_back(letter);
    s.progress.aftermath_pending = false;
    push_event(s, interrupts, letter.id);
}

void seed_concord_followup(CampaignState& s) {
    if (s.progress.concord_followup_seeded) return;
    if (!s.progress.lexington_fought) return;
    s.progress.concord_followup_seeded = true;
    s.ai_objective = "concord";
    // Point British column toward Concord if still in the field.
    auto brit = s.armies.find("british_regulars");
    if (brit != s.armies.end() && brit->second.path.empty() && !s.pending_battle.active) {
        PathPlan plan = plan_path(s, brit->first, "concord");
        if (plan.ok && !plan.edges.empty()) {
            CommandPayload p;
            p.kind = CommandKind::MoveArmy;
            p.army_id = brit->first;
            p.dest_province = "concord";
            p.edge_path = plan.edges;
            s = enqueue_command(s, p);
        }
    }
    // Move militia toward Concord from wherever the first battle left it.
    auto mil = s.armies.find("continental_militia");
    if (mil != s.armies.end() && mil->second.province != "concord" && mil->second.path.empty() &&
        !s.pending_battle.active) {
        PathPlan plan = plan_path(s, mil->first, "concord");
        if (plan.ok && !plan.edges.empty()) {
            CommandPayload p;
            p.kind = CommandKind::MoveArmy;
            p.army_id = mil->first;
            p.dest_province = "concord";
            p.edge_path = plan.edges;
            s = enqueue_command(s, p);
        }
    }
    detail::add_log(s, "april follow-up: columns ordered toward Concord");
}

void maybe_commander_friction(CampaignState& s, std::vector<Interrupt>* interrupts) {
    if (s.progress.battles_fought < 1) return;
    DetachedCommander& cmd = s.progress.commander;
    if (cmd.pending_clarification) return;
    if (find_awaiting(s)) return;
    // After first battle, cautious commander asks to preserve force rather than contest Concord.
    if (s.progress.lexington_fought && s.progress.battles_fought == 1 &&
        cmd.last_request.empty()) {
        reset_clerk_day(s);
        if (!clerk_has_capacity(s)) return;
        CorrespondenceItem letter;
        letter.id = next_letter_id(s, "cmd");
        letter.created_hour = s.now.hour_index;
        letter.recipient = "town_selectmen";
        letter.kind = "commander_report";
        letter.status = "needs_approval";
        letter.awaiting_choice = true;
        letter.fictionalized = true;
        letter.subject = "Detached command: timing at Concord";
        letter.body =
            "[FICTIONALIZED] " + cmd.name +
            " prefers a narrower task — screen the stores and preserve the company — and asks "
            "whether to contest the North Bridge crossing now or hold back until powder arrives.";
        letter.reason = "commander_risk:" + cmd.risk;
        s.staff.queue.push_back(letter);
        cmd.pending_clarification = true;
        cmd.last_request = letter.id;
        ++s.staff.letters_processed_today;
        push_event(s, interrupts, letter.id);
    }
}

void maybe_chapter_gates(CampaignState& s, std::vector<Interrupt>* interrupts) {
    const YMDH y = to_ymdh(s.now);
    if (!s.progress.chapter_continental &&
        (y.y > 1775 || (y.y == 1775 && (y.m > 6 || (y.m == 6 && y.d >= 14))))) {
        s.progress.chapter_continental = true;
        s.progress.player_role = "continental_field_commander";
        CorrespondenceItem letter;
        letter.id = next_letter_id(s, "chapter");
        letter.created_hour = s.now.hour_index;
        letter.recipient = "congress";
        letter.kind = "commander_report";
        letter.status = "needs_approval";
        letter.awaiting_choice = true;
        letter.fictionalized = true;
        letter.subject = "Chapter: Continental Army established";
        letter.body =
            "[FICTIONALIZED briefing] Congress establishes a Continental Army (14 June 1775). "
            "Your perspective shifts from purely Provincial command toward Continental "
            "coordination. Washington is appointed 15 June and takes command 3 July — headquarters "
            "correspondence becomes available after that dated transition.";
        s.staff.queue.push_back(letter);
        push_event(s, interrupts, letter.id);
        detail::add_log(s, "chapter: continental army");
    }
    if (s.progress.chapter_continental && !s.progress.chapter_washington &&
        (y.y > 1775 || (y.y == 1775 && (y.m > 7 || (y.m == 7 && y.d >= 3))))) {
        s.progress.chapter_washington = true;
        s.progress.player_role = "washington_command_perspective";
        detail::add_log(s, "chapter: washington command perspective");
    }
    // Knox: offer continued support after Dec 17 1775 (letter already underway historically).
    if (s.progress.chapter_washington &&
        (s.progress.knox.status == "none" || s.progress.knox.status.empty()) &&
        (y.y > 1775 || (y.y == 1775 && y.m == 12 && y.d >= 17))) {
        CorrespondenceItem letter;
        letter.id = next_letter_id(s, "knox");
        letter.created_hour = s.now.hour_index;
        letter.recipient = "washington_hq";
        letter.kind = "knox_support";
        letter.status = "needs_approval";
        letter.awaiting_choice = true;
        letter.fictionalized = false;
        letter.subject = "Knox expedition: continued support";
        letter.body =
            "Knox's 17 December 1775 letter reports the Ticonderoga artillery expedition already "
            "underway (59 mixed pieces). Choose continued support (labor, transport, time) or "
            "decline competing commitments. Delivered guns still need crews, ammunition, and "
            "emplacement — this is a campaign project, not an automatic capture of Boston.";
        letter.gold_cost = 80;
        s.staff.queue.push_back(letter);
        s.progress.knox.status = "offered";
        s.progress.knox.fictionalized_authorization = false;
        push_event(s, interrupts, letter.id);
    }
}

void resolve_due_letters(CampaignState& s, std::vector<Interrupt>* interrupts) {
    for (auto& letter : s.staff.queue) {
        if (letter.status == "delivered" || letter.status == "refused" || letter.status == "dismissed")
            continue;
        if (letter.status == "needs_approval" || letter.awaiting_choice) continue;
        if (letter.resolve_on_hour > 0 && s.now.hour_index >= letter.resolve_on_hour) {
            if (letter.kind == "requisition_aftermath") {
                // Follow-up after emergency levy: cool townsfolk, or sour British attitude if
                // support was already low when the levy hit. The snapshot lives in decision_id.
                // letter.powder stays 0 so this cannot be delivered as powder.
                int support_at_levy = letter.powder;
                if (!letter.decision_id.empty()) {
                    bool digits = true;
                    int parsed = 0;
                    for (char ch : letter.decision_id) {
                        if (ch < '0' || ch > '9') {
                            digits = false;
                            break;
                        }
                        parsed = parsed * 10 + (ch - '0');
                    }
                    if (digits) support_at_levy = parsed;
                }
                if (support_at_levy <= 40) {
                    auto rit = s.relations.find(kBritish);
                    if (rit != s.relations.end()) rit->second = detail::clamp_int(rit->second - 5, -100, 100);
                    ResourceChange ch;
                    ch.reason = "Requisition fallout: British attitude −5 (towns already strained)";
                    detail::record_resource_change(s, ch);
                    letter.reason = ch.reason;
                } else {
                    const int before = s.local_support;
                    s.local_support = detail::clamp_int(s.local_support + 4, 0, 100);
                    ResourceChange ch;
                    ch.reason = "Townsfolk cool after the levy: +4 Support";
                    ch.local_support = s.local_support - before;
                    detail::record_resource_change(s, ch);
                    letter.reason = ch.reason;
                }
                letter.status = "delivered";
                letter.awaiting_choice = false;
                push_event(s, interrupts, letter.id);
                continue;
            }
            if (letter.status == "delayed") {
                // Delayed shipments convert to pending delivery a few hours later once.
                letter.status = "approved";
                letter.resolve_on_hour = s.now.hour_index + 4;
                continue;
            }
            deliver_committed(s, letter);
            push_event(s, interrupts, letter.id);
        }
    }
}

void rest_armies_hourly(CampaignState& s) {
    if (to_ymdh(s.now).h != 0) return;
    for (auto& kv : s.armies) {
        if (!kv.second.path.empty()) continue;
        kv.second.fatigue = std::max(0, kv.second.fatigue - 5);
        kv.second.readiness = detail::clamp_int(kv.second.readiness + 2, 0, 100);
    }
}

}  // namespace

void ensure_logistics_defaults(CampaignState& s) {
    for (const auto& kv : s.provinces) {
        if (s.province_stores.find(kv.first) == s.province_stores.end())
            s.province_stores[kv.first] = ProvinceStores{};
    }
    if (s.staff.clerk_name.empty()) s.staff.clerk_name = "Clerk Hale";
    if (s.staff.capacity_per_day <= 0) s.staff.capacity_per_day = 2;
    if (s.progress.player_role.empty()) s.progress.player_role = "massachusetts_field_commander";
    if (s.progress.commander.id.empty()) {
        s.progress.commander = DetachedCommander{};
    }
    if (s.progress.knox.id.empty()) s.progress.knox.id = "knox_ticonderoga";
    if (s.progress.knox.status.empty()) s.progress.knox.status = "none";
    // local_support and readiness use 0 as a real (exhausted) value; their defaults come
    // from the struct initializers and the JSON loaders, so never "repair" a real 0 here.
    // Donor reserves are seeded by opening-war correspondence (not every campaign load),
    // so the clock golden hash stays stable until that catalog path runs.
}

void migrate_campaign_state(CampaignState& s) {
    ensure_logistics_defaults(s);
    bool any_province_stock = false;
    for (const auto& kv : s.province_stores) {
        const ProvinceStores& st = kv.second;
        if (st.powder || st.cartridges || st.muskets || st.bayonets || st.provisions || st.lead ||
            st.paper) {
            any_province_stock = true;
            break;
        }
    }
    if (!any_province_stock && !s.stores.empty()) distribute_faction_stores_into_provinces(s);
    for (auto& sh : s.shipments) {
        if (sh.dest_province.empty()) {
            const std::string fac = sh.dest_faction.empty() ? s.player_faction : sh.dest_faction;
            sh.dest_province = default_depot_province(s, fac);
        }
        if (sh.muskets == 0 && sh.arms > 0) sh.muskets = sh.arms;
        if (sh.status == "committed") sh.status = "in_transit";
    }
}

void load_logistics_tuning(const std::string& json_text) {
    try {
        const json j = json::parse(json_text);
        LogisticsTuning t;
        if (j.contains("purchase")) {
            const json& p = j.at("purchase");
            t.gold_per_powder = p.value("gold_per_powder", t.gold_per_powder);
            t.gold_per_cartridge = p.value("gold_per_cartridge", t.gold_per_cartridge);
            t.gold_per_musket = p.value("gold_per_musket", t.gold_per_musket);
            t.gold_per_bayonet = p.value("gold_per_bayonet", t.gold_per_bayonet);
            t.gold_per_provision = p.value("gold_per_provision", t.gold_per_provision);
            t.gold_per_lead = p.value("gold_per_lead", t.gold_per_lead);
            t.gold_per_paper = p.value("gold_per_paper", t.gold_per_paper);
            t.purchase_transit_hours = p.value("transit_hours", t.purchase_transit_hours);
        }
        if (j.contains("transfer"))
            t.transfer_transit_hours = j.at("transfer").value("transit_hours", t.transfer_transit_hours);
        if (j.contains("cartridge_craft")) {
            const json& c = j.at("cartridge_craft");
            t.craft_powder = c.value("powder_per_batch", t.craft_powder);
            t.craft_lead = c.value("lead_per_batch", t.craft_lead);
            t.craft_paper = c.value("paper_per_batch", t.craft_paper);
            t.craft_cartridges = c.value("cartridges_per_batch", t.craft_cartridges);
            t.craft_hours = c.value("hours_per_batch", t.craft_hours);
            t.craft_workers = c.value("workers_per_batch", t.craft_workers);
        }
        if (j.contains("aftermath_purchase")) {
            const json& a = j.at("aftermath_purchase");
            t.aftermath_gold = a.value("gold_cost", t.aftermath_gold);
            t.aftermath_powder = a.value("powder", t.aftermath_powder);
            t.aftermath_muskets = a.value("muskets", t.aftermath_muskets);
            t.aftermath_capacity_bonus_muskets =
                a.value("capacity_bonus_muskets", t.aftermath_capacity_bonus_muskets);
            t.aftermath_transit = a.value("transit_hours", t.aftermath_transit);
        }
        if (j.contains("battle"))
            t.recover_muskets_on_win = j.at("battle").value("recover_muskets_on_win", false);
        if (j.contains("development")) {
            const json& d = j.at("development");
            t.dev_farm_gold = d.value("farm_gold", t.dev_farm_gold);
            t.dev_workshop_gold = d.value("workshop_gold", t.dev_workshop_gold);
            t.dev_depot_gold = d.value("depot_gold", t.dev_depot_gold);
            t.dev_fort_gold = d.value("fort_gold", t.dev_fort_gold);
            t.dev_cost_growth = d.value("cost_growth_per_level", t.dev_cost_growth);
            t.dev_max_level = d.value("max_level", t.dev_max_level);
            t.farm_provisions_per_month =
                d.value("farm_provisions_per_month", t.farm_provisions_per_month);
            t.workshop_cartridges_per_level =
                d.value("workshop_cartridges_per_level", t.workshop_cartridges_per_level);
            t.farm_readiness_per_level =
                d.value("farm_readiness_per_level", t.farm_readiness_per_level);
            t.depot_readiness_per_level =
                d.value("depot_readiness_per_level", t.depot_readiness_per_level);
        }
        if (j.contains("hq_province")) {
            const json& h = j.at("hq_province");
            t.continental_hq = h.value("continental", t.continental_hq);
            t.british_hq = h.value("british", t.british_hq);
        }
        g_tuning = t;
        g_tuning_loaded = true;
    } catch (...) {
        g_tuning = LogisticsTuning{};
        g_tuning_loaded = false;
    }
}

bool logistics_tuning_loaded() { return g_tuning_loaded; }
int workshop_cartridges_per_level() { return std::max(0, tuning().workshop_cartridges_per_level); }
int farm_readiness_per_level() { return std::max(0, tuning().farm_readiness_per_level); }
int depot_readiness_per_level() { return std::max(0, tuning().depot_readiness_per_level); }

ProvinceStores& province_ledger(CampaignState& s, const std::string& province_id) {
    return s.province_stores[province_id];
}

const ProvinceStores& province_ledger(const CampaignState& s, const std::string& province_id) {
    static const ProvinceStores kEmpty{};
    auto it = s.province_stores.find(province_id);
    return it == s.province_stores.end() ? kEmpty : it->second;
}

FactionStores rollup_faction_stores(const CampaignState& s, const std::string& faction) {
    FactionStores out;
    for (const auto& kv : s.provinces) {
        if (kv.second.owner != faction) continue;
        const ProvinceStores& st = province_ledger(s, kv.first);
        out.powder += st.powder;
        out.arms += st.muskets;
        out.provisions += st.provisions;
    }
    return out;
}

std::string default_depot_province(const CampaignState& s, const std::string& faction) {
    const std::string prefer =
        faction == kBritish ? tuning().british_hq : tuning().continental_hq;
    if (s.provinces.count(prefer)) return prefer;
    for (const auto& kv : s.provinces)
        if (kv.second.owner == faction) return kv.first;
    return prefer;
}

int province_development(const Province& p) {
    return 1 + std::max(0, p.farm_level) + std::max(0, p.workshop_level) +
           std::max(0, p.depot_level) + std::max(0, p.fort_level);
}

int effective_supply_value(const Province& p) {
    return std::max(0, p.supply_value) + std::max(0, p.farm_level);
}

int development_max_level() { return std::max(1, tuning().dev_max_level); }

int development_cost(const std::string& track, int current_level) {
    int base = -1;
    if (track == "farm") base = tuning().dev_farm_gold;
    if (track == "workshop") base = tuning().dev_workshop_gold;
    if (track == "depot") base = tuning().dev_depot_gold;
    if (track == "fort") base = tuning().dev_fort_gold;
    if (base < 0) return -1;
    return base + std::max(0, current_level) * std::max(0, tuning().dev_cost_growth);
}

CampaignState develop_province(const CampaignState& state, const std::string& province_id,
                               const std::string& track, std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
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
    int* level = nullptr;
    if (track == "farm") level = &pit->second.farm_level;
    if (track == "workshop") level = &pit->second.workshop_level;
    if (track == "depot") level = &pit->second.depot_level;
    if (track == "fort") level = &pit->second.fort_level;
    if (!level) {
        error = "unknown_track";
        return state;
    }
    if (*level >= development_max_level()) {
        error = "max_level";
        return state;
    }
    const int cost = development_cost(track, *level);
    if (s.player_gold < cost) {
        error = "insufficient_gold";
        return state;
    }
    s.player_gold -= cost;
    ++(*level);
    {
        std::string track_label = track;
        if (!track_label.empty()) track_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(track_label[0])));
        ResourceChange ch;
        ch.reason = track_label + ", " + pit->second.name + ": -" + std::to_string(cost) + " gold";
        ch.gold = -cost;
        ch.province_id = province_id;
        detail::record_resource_change(s, ch);
    }
    return s;
}

std::string development_preview(const CampaignState& state, const std::string& province_id,
                                const std::string& track) {
    auto pit = state.provinces.find(province_id);
    if (pit == state.provinces.end()) return "Unknown province.";
    int level = 0;
    if (track == "farm") level = pit->second.farm_level;
    else if (track == "workshop") level = pit->second.workshop_level;
    else if (track == "depot") level = pit->second.depot_level;
    else if (track == "fort") level = pit->second.fort_level;
    else return "Unknown track.";
    if (level >= development_max_level()) return "Already at maximum level.";
    const int cost = development_cost(track, level);
    std::ostringstream oss;
    oss << "Cost " << cost << " gold (treasury " << state.player_gold << "). ";
    if (track == "farm")
        oss << "Effect: +" << tuning().farm_provisions_per_month
            << " provisions/month; +supply income; battle readiness +"
            << tuning().farm_readiness_per_level << "/level; faster reinforcement.";
    else if (track == "workshop")
        oss << "Effect: faster cartridge craft; battle +"
            << tuning().workshop_cartridges_per_level << " cartridge budget/level (raises militia tickets).";
    else if (track == "depot")
        oss << "Effect: shorter shipment transit; battle readiness +"
            << tuning().depot_readiness_per_level << "/level; faster reinforcement.";
    else if (track == "fort")
        oss << "Effect: defender +3 morale/+2 readiness per level; starts holding a capture point + extra cover.";
    return oss.str();
}

std::string aftermath_choice_preview(const CampaignState& state, const std::string& choice) {
    std::ostringstream oss;
    if (choice == "purchase") {
        const int hours = tuning().aftermath_transit;
        oss << "Cost " << tuning().aftermath_gold << " gold now. "
            << "In " << hours << "h: +" << tuning().aftermath_powder << " powder, +"
            << tuning().aftermath_muskets << " muskets at the depot"
            << (state.progress.capacity_level >= 1
                    ? " (+" + std::to_string(tuning().aftermath_capacity_bonus_muskets) + " from capacity)."
                    : ".");
    } else if (choice == "request_assistance" || choice == "request") {
        oss << "No gold cost. Risk: refused / delayed / partial. "
            << "On success: up to +12 powder / +6 muskets after 6–24h (local support only affects odds).";
    } else if (choice == "requisition") {
        oss << "Immediate: powder and +2 muskets from town stocks; provisions spent. "
            << "Cost: Support −12. In 24h: townsfolk cool (+4 Support) or attitude −5 if Support was already low.";
    } else if (choice == "capacity_investment" || choice == "capacity") {
        oss << "Cost 60 gold and 6 provisions now. In 48h: capacity level 1 and +4 muskets; later purchases gain +2 muskets.";
    } else if (choice == "dismiss") {
        oss << "No stores change. Follow-up marches toward Concord still fire.";
    } else {
        oss << "Staff action.";
    }
    return oss.str();
}

CampaignState approve_purchase(const CampaignState& state, const std::string& dest_province,
                               int powder, int cartridges, int muskets, int bayonets, int provisions,
                               int lead, int paper, std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
    if (!s.provinces.count(dest_province)) {
        error = "unknown_province";
        return state;
    }
    if (s.provinces.at(dest_province).owner != s.player_faction) {
        error = "not_your_province";
        return state;
    }
    powder = std::max(0, powder);
    cartridges = std::max(0, cartridges);
    muskets = std::max(0, muskets);
    bayonets = std::max(0, bayonets);
    provisions = std::max(0, provisions);
    lead = std::max(0, lead);
    paper = std::max(0, paper);
    if (powder + cartridges + muskets + bayonets + provisions + lead + paper <= 0) {
        error = "empty_order";
        return state;
    }
    const int cost = powder * tuning().gold_per_powder + cartridges * tuning().gold_per_cartridge +
                     muskets * tuning().gold_per_musket + bayonets * tuning().gold_per_bayonet +
                     provisions * tuning().gold_per_provision + lead * tuning().gold_per_lead +
                     paper * tuning().gold_per_paper;
    if (s.player_gold < cost) {
        error = "insufficient_gold";
        return state;
    }
    const int before_powder = province_ledger(s, dest_province).powder;
    const int before_cart = province_ledger(s, dest_province).cartridges;
    s.player_gold -= cost;
    Shipment sh;
    sh.id = next_ship_id(s, "buy");
    sh.source_province.clear();
    sh.dest_province = dest_province;
    sh.dest_faction = s.provinces.at(dest_province).owner;
    sh.powder = powder;
    sh.cartridges = cartridges;
    sh.muskets = muskets;
    sh.bayonets = bayonets;
    sh.provisions = provisions;
    sh.lead = lead;
    sh.paper = paper;
    sh.commit_hour = s.now.hour_index;
    sh.deliver_on_hour = s.now.hour_index + std::max(1, tuning().purchase_transit_hours -
                                                             s.provinces.at(dest_province).depot_level);
    sh.status = "in_transit";
    s.shipments.push_back(sh);
    // Approving an order is not delivering it.
    if (province_ledger(s, dest_province).powder != before_powder ||
        province_ledger(s, dest_province).cartridges != before_cart) {
        error = "purchase_mutated_destination";
        return state;
    }
    {
        ResourceChange ch;
        ch.reason = "Purchase order " + sh.id + ": -" + std::to_string(cost) + " gold (in transit to " +
                    dest_province + ")";
        ch.gold = -cost;
        ch.province_id = dest_province;
        detail::record_resource_change(s, ch);
    }
    return s;
}

CampaignState approve_transfer(const CampaignState& state, const std::string& source_province,
                               const std::string& dest_province, int powder, int cartridges, int muskets,
                               int bayonets, int provisions, int lead, int paper, std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
    if (!s.provinces.count(source_province) || !s.provinces.count(dest_province)) {
        error = "unknown_province";
        return state;
    }
    if (source_province == dest_province) {
        error = "same_province";
        return state;
    }
    if (s.provinces.at(source_province).owner != s.player_faction ||
        s.provinces.at(dest_province).owner != s.player_faction) {
        error = "not_your_province";
        return state;
    }
    powder = std::max(0, powder);
    cartridges = std::max(0, cartridges);
    muskets = std::max(0, muskets);
    bayonets = std::max(0, bayonets);
    provisions = std::max(0, provisions);
    lead = std::max(0, lead);
    paper = std::max(0, paper);
    if (powder + cartridges + muskets + bayonets + provisions + lead + paper <= 0) {
        error = "empty_order";
        return state;
    }
    ProvinceStores& src = province_ledger(s, source_province);
    if (!debit_province(src, powder, cartridges, muskets, bayonets, provisions, lead, paper)) {
        error = "insufficient_stores";
        return state;
    }
    const int dest_before = province_ledger(s, dest_province).powder +
                            province_ledger(s, dest_province).cartridges +
                            province_ledger(s, dest_province).muskets;
    Shipment sh;
    sh.id = next_ship_id(s, "xfer");
    sh.source_province = source_province;
    sh.dest_province = dest_province;
    sh.dest_faction = s.provinces.at(dest_province).owner;
    sh.powder = powder;
    sh.cartridges = cartridges;
    sh.muskets = muskets;
    sh.bayonets = bayonets;
    sh.provisions = provisions;
    sh.lead = lead;
    sh.paper = paper;
    sh.commit_hour = s.now.hour_index;
    sh.deliver_on_hour = s.now.hour_index + std::max(1, tuning().transfer_transit_hours -
                                                             s.provinces.at(dest_province).depot_level);
    sh.status = "in_transit";
    s.shipments.push_back(sh);
    const int dest_after = province_ledger(s, dest_province).powder +
                           province_ledger(s, dest_province).cartridges +
                           province_ledger(s, dest_province).muskets;
    if (dest_after != dest_before) {
        error = "transfer_credited_early";
        return state;
    }
    {
        ResourceChange ch;
        ch.reason = "Transfer departed " + sh.id + ": " + source_province + " → " + dest_province;
        ch.powder = -powder;
        ch.cartridges = -cartridges;
        ch.muskets = -muskets;
        ch.provisions = -provisions;
        ch.province_id = source_province;
        detail::record_resource_change(s, ch);
    }
    return s;
}

CampaignState start_cartridge_job(const CampaignState& state, const std::string& province_id,
                                  const std::string& army_id, int workers, int powder, int lead, int paper,
                                  std::string& error) {
    error.clear();
    CampaignState s = state;
    ensure_logistics_defaults(s);
    if (!s.provinces.count(province_id)) {
        error = "unknown_province";
        return state;
    }
    auto ait = s.armies.find(army_id);
    if (ait == s.armies.end()) {
        error = "unknown_army";
        return state;
    }
    if (ait->second.province != province_id) {
        error = "army_not_in_province";
        return state;
    }
    workers = std::max(0, workers);
    powder = std::max(0, powder);
    lead = std::max(0, lead);
    paper = std::max(0, paper);
    if (workers <= 0 || powder <= 0) {
        error = "invalid_job";
        return state;
    }
    // Lead or paper required (plan: powder, lead or paper, workers).
    if (lead <= 0 && paper <= 0) {
        error = "need_lead_or_paper";
        return state;
    }
    const std::string cls = "militia";
    const int fit = detail::map_get(ait->second.units, cls);
    const int held = committed_workers(s, army_id, cls);
    if (workers > fit - held) {
        error = "workers_unavailable";
        return state;
    }
    ProvinceStores& st = province_ledger(s, province_id);
    if (!debit_province(st, powder, 0, 0, 0, 0, lead, paper)) {
        error = "insufficient_stores";
        return state;
    }
    // Batches scale with the scarcest input relative to the tuning recipe.
    int batches = powder / std::max(1, tuning().craft_powder);
    if (lead > 0) batches = std::min(batches, lead / std::max(1, tuning().craft_lead));
    if (paper > 0) batches = std::min(batches, paper / std::max(1, tuning().craft_paper));
    batches = std::max(1, batches);
    const int workshop = s.provinces.count(province_id) ? s.provinces.at(province_id).workshop_level : 0;
    const int hours = std::max(1, (std::max(1, tuning().craft_hours) * batches) / (1 + std::max(0, workshop)));
    CartridgeJob job;
    job.id = next_ship_id(s, "cart");
    job.province_id = province_id;
    job.army_id = army_id;
    job.unit_class = cls;
    job.workers = workers;
    job.powder = powder;
    job.lead = lead;
    job.paper = paper;
    job.cartridges_out = batches * std::max(1, tuning().craft_cartridges);
    job.started_hour = s.now.hour_index;
    job.complete_on_hour = s.now.hour_index + hours;
    job.status = "underway";
    s.progress.cartridge_jobs.push_back(job);
    PersonnelCommitment pc;
    pc.id = "pc_" + job.id;
    pc.army_id = army_id;
    pc.unit_class = cls;
    pc.count = workers;
    pc.reason = "cartridge_making";
    pc.release_on_hour = job.complete_on_hour;
    s.progress.personnel_commitments.push_back(pc);
    detail::add_log(s, "cartridge job started: " + job.id + " out=" + std::to_string(job.cartridges_out));
    return s;
}

void tick_cartridge_jobs(CampaignState& s) {
    for (auto& job : s.progress.cartridge_jobs) {
        if (job.status != "underway") continue;
        if (job.complete_on_hour > 0 && s.now.hour_index >= job.complete_on_hour) {
            const int made = std::max(0, job.cartridges_out);
            province_ledger(s, job.province_id).cartridges += made;
            job.status = "complete";
            ResourceChange ch;
            ch.reason = "Cartridge job complete " + job.id + ": +" + std::to_string(made) + " cartridges";
            ch.cartridges = made;
            ch.province_id = job.province_id;
            detail::record_resource_change(s, ch);
        }
    }
}

std::string scenario_preset_for_province(const std::string& province_id) {
    if (province_id == "lexington") return "lexington_common";
    if (province_id == "concord") return "concord_bridge";
    if (province_id == "lincoln" || province_id == "menotomy") return "battle_road";
    return "lexington_common";
}

CampaignState choose_logistics(const CampaignState& in, const std::string& letter_id,
                               const std::string& choice) {
    CampaignState s = in;
    ensure_logistics_defaults(s);
    reset_clerk_day(s);
    CorrespondenceItem* letter = nullptr;
    if (!letter_id.empty()) letter = find_letter(s, letter_id);
    if (!letter) letter = find_awaiting(s);
    if (!letter) {
        detail::add_log(s, "logistics choice ignored: no awaiting letter");
        return s;
    }
    // A terminal letter (delivered / refused / dismissed) is only a notification: any
    // choice on it just clears the popup. It must never re-charge or re-deliver.
    if (letter->status == "delivered" || letter->status == "refused" || letter->status == "dismissed") {
        if (s.pending_event_id == letter->id) s.pending_event_id.clear();
        detail::add_log(s, "logistics notice cleared: " + letter->id);
        return s;
    }
    if (!letter->awaiting_choice && letter->status != "needs_approval") {
        detail::add_log(s, "logistics choice ignored: letter not awaiting choice");
        return s;
    }
    const bool clerk_backlogged = !clerk_has_capacity(s);

    const std::string id = letter->id;

    if (letter->kind == "source_decision" || letter->kind == "historical_notice") {
        return apply_source_decision(s, id, choice);
    }

    letter->awaiting_choice = false;

    if (choice == "dismiss") {
        if (letter->kind == "commander_report" && s.progress.commander.last_request == id)
            return resolve_commander_request(s, "dismiss");
        letter->status = "dismissed";
        letter->reason = "Player set the letter aside.";
        if (letter->kind == "knox_support") s.progress.knox.status = "declined";
        if (s.pending_event_id == id) s.pending_event_id.clear();
        if (letter->kind != "commander_report" && letter->kind != "knox_support") seed_concord_followup(s);
        return s;
    }

    if (letter->kind == "commander_report") {
        if (letter->subject.find("Detached") != std::string::npos) {
            CampaignState resolved = resolve_commander_request(s, choice);
            // resolve_* ignores choices that are not commander orders; keep the letter
            // awaiting a real order instead of stranding it half-consumed.
            for (const auto& q : resolved.staff.queue)
                if (q.id == id && q.status == "needs_approval") {
                    for (auto& qq : resolved.staff.queue)
                        if (qq.id == id) qq.awaiting_choice = true;
                }
            return resolved;
        }
        // Chapter / briefing letters: acknowledge and clear.
        letter->status = (choice == "dismiss") ? "dismissed" : "approved";
        letter->reason = "Acknowledged.";
        if (s.pending_event_id == id) s.pending_event_id.clear();
        return s;
    }

    if (letter->kind == "knox_support" || choice == "knox_support") {
        if (choice == "knox_support" || choice == "approve" || choice == "support") {
            if (s.player_gold < letter->gold_cost) {
                letter->status = "refused";
                letter->reason = "Treasury cannot spare the transport commitment.";
                s.progress.knox.status = "declined";
            } else {
                s.player_gold -= letter->gold_cost;
                letter->status = "pending";
                letter->resolve_on_hour = s.now.hour_index + 24 * 40;  // weeks of winter haul
                s.progress.knox.status = "underway";
                s.progress.knox.start_hour = s.now.hour_index;
                s.progress.knox.ready_hour = letter->resolve_on_hour;
                s.progress.knox.transport_committed = letter->gold_cost;
                ResourceChange ch;
                ch.reason = "Support Knox: -" + std::to_string(letter->gold_cost) + " gold";
                ch.gold = -letter->gold_cost;
                detail::record_resource_change(s, ch);
            }
        } else {
            letter->status = "dismissed";
            s.progress.knox.status = "declined";
            letter->reason = "Competing commitments; expedition not reinforced.";
        }
        if (s.pending_event_id == id) s.pending_event_id.clear();
        return s;
    }

    if (choice == "purchase") {
        const int cost = tuning().aftermath_gold;
        if (s.player_gold < cost) {
            letter->status = "refused";
            letter->reason = "Not enough gold to purchase powder and arms.";
            if (s.pending_event_id == id) s.pending_event_id.clear();
            return s;
        }
        s.player_gold -= cost;
        letter->kind = "purchase";
        letter->recipient = "merchant";
        letter->gold_cost = cost;
        letter->status = "approved";
        letter->committed_powder = tuning().aftermath_powder;
        // A ready repair contact returns extra serviceable muskets with each wagon.
        letter->committed_arms =
            tuning().aftermath_muskets +
            (s.progress.capacity_level >= 1 ? tuning().aftermath_capacity_bonus_muskets : 0);
        letter->resolve_on_hour =
            s.now.hour_index + tuning().aftermath_transit + (clerk_backlogged ? 24 : 0);
        letter->reason = clerk_backlogged ? "Clerk's desk is full; the merchant wagon slips a day."
                                          : "Merchant wagon hired; goods in transit.";
        ResourceChange ch;
        ch.reason = "Purchase (aftermath): -" + std::to_string(cost) + " gold; delivery in " +
                    std::to_string(letter->resolve_on_hour - s.now.hour_index) + "h";
        ch.gold = -cost;
        detail::record_resource_change(s, ch);
    } else if (choice == "request_assistance" || choice == "request") {
        letter->kind = "request_assistance";
        letter->recipient = "committee_of_safety";
        letter->powder = 12;
        letter->arms = 6;
        roll_assistance(s, *letter);
        if (clerk_backlogged && letter->status != "refused") letter->resolve_on_hour += 24;
        if (letter->status == "refused") {
            // no delivery scheduled
        } else if (letter->status != "delayed" && letter->status != "partial" &&
                   letter->status != "approved") {
            letter->status = "pending";
        }
        detail::add_log(s, "assistance request: " + letter->status +
                               (letter->resolve_on_hour > 0
                                    ? (" — reply in " +
                                       std::to_string(std::max<int64_t>(0, letter->resolve_on_hour - s.now.hour_index)) +
                                       "h")
                                    : ""));
    } else if (choice == "requisition") {
        letter->kind = "requisition";
        letter->recipient = "town_selectmen";
        ProvinceStores& st = faction_depot(s, s.player_faction);
        // Immediate relief; exhausts local stocks and risks cooperation.
        const int take_p = std::min(8, std::max(2, st.provisions / 2));
        const int prov_spent = take_p + 4;
        const int support_before = s.local_support;
        st.powder += take_p;
        st.muskets += 2;
        st.provisions = std::max(0, st.provisions - prov_spent);
        s.local_support = detail::clamp_int(s.local_support - 12, 0, 100);
        letter->status = "delivered";
        letter->received_powder = take_p;
        letter->received_arms = 2;
        letter->committed_powder = take_p;
        letter->committed_arms = 2;
        letter->reason = "Emergency requisition filled from town stocks; local support falls.";
        {
            ResourceChange ch;
            ch.reason = "Requisition: +" + std::to_string(take_p) + " powder, +2 muskets, −" +
                        std::to_string(prov_spent) + " provisions, Support −12";
            ch.powder = take_p;
            ch.muskets = 2;
            ch.provisions = -prov_spent;
            ch.local_support = s.local_support - support_before;
            ch.province_id = default_depot_province(s, s.player_faction);
            detail::record_resource_change(s, ch);
        }
        // Tracked 24h follow-up (cool townsfolk, or attitude risk if support was already low).
        {
            CorrespondenceItem follow;
            follow.id = next_letter_id(s, "req_follow");
            follow.created_hour = s.now.hour_index;
            follow.resolve_on_hour = s.now.hour_index + 24;
            follow.recipient = "town_selectmen";
            follow.kind = "requisition_aftermath";
            follow.status = "pending";
            follow.awaiting_choice = false;
            follow.fictionalized = true;
            follow.subject = "Townsfolk after the levy";
            follow.body = "[FICTIONALIZED] The selectmen report how the emergency levy settled overnight.";
            follow.reason = "In 24h: +4 Support, or British attitude −5 if Support was already ≤40.";
            // decision_id holds the support snapshot. powder stays 0 so delivery paths cannot
            // credit it as a powder shipment.
            follow.powder = 0;
            follow.committed_powder = 0;
            follow.decision_id = std::to_string(support_before);
            s.staff.queue.push_back(follow);
        }
    } else if (choice == "capacity_investment" || choice == "capacity") {
        const int cost = 60;
        if (s.player_gold < cost) {
            letter->status = "refused";
            letter->reason = "Not enough gold for a longer-term repair/procurement contact.";
            if (s.pending_event_id == id) s.pending_event_id.clear();
            return s;
        }
        bool capacity_busy = s.progress.capacity_level > 0;
        for (const auto& q : s.staff.queue)
            if (q.kind == "capacity_investment" &&
                (q.status == "pending" || q.status == "approved" || q.status == "delayed" || q.status == "partial"))
                capacity_busy = true;
        if (capacity_busy) {
            letter->status = "refused";
            letter->reason = "A capacity contact is already established or underway.";
            if (s.pending_event_id == id) s.pending_event_id.clear();
            return s;
        }
        ProvinceStores& st = faction_depot(s, s.player_faction);
        if (st.provisions < 6) {
            letter->status = "refused";
            letter->reason = "Skilled labor needs provisions to stay at work.";
            if (s.pending_event_id == id) s.pending_event_id.clear();
            return s;
        }
        s.player_gold -= cost;
        st.provisions -= 6;
        letter->kind = "capacity_investment";
        letter->gold_cost = cost;
        letter->committed_arms = 4;  // serviceable muskets the bench yields when ready
        letter->status = "pending";
        letter->resolve_on_hour = s.now.hour_index + 48;  // two days of work — not during a fight
        letter->reason = "Repair contact and procurement channel begun; no instant weapon upgrade.";
        s.progress.capacity_ready_hour = letter->resolve_on_hour;
        ResourceChange ch;
        ch.reason = "Invest capacity: -60 gold, -6 provisions; ready in 48h";
        ch.gold = -cost;
        ch.provisions = -6;
        ch.province_id = default_depot_province(s, s.player_faction);
        detail::record_resource_change(s, ch);
    } else {
        letter->awaiting_choice = true;
        letter->status = "needs_approval";
        detail::add_log(s, "unknown logistics choice: " + choice);
        return s;
    }

    ++s.staff.letters_processed_today;
    if (s.pending_event_id == id) s.pending_event_id.clear();
    seed_concord_followup(s);
    return s;
}

CampaignState resolve_commander_request(const CampaignState& in, const std::string& action) {
    CampaignState s = in;
    ensure_logistics_defaults(s);
    DetachedCommander& cmd = s.progress.commander;
    // Only the commander's own letter can be answered here. A chapter or staff letter
    // that happens to share the commander_report kind is not his to consume.
    CorrespondenceItem* letter = cmd.last_request.empty() ? nullptr : find_letter(s, cmd.last_request);
    if (!letter || letter->kind != "commander_report" ||
        (!letter->awaiting_choice && letter->status != "needs_approval"))
        return s;
    const bool known = action == "clarify" || action == "preserve_force" || action == "counterorder" ||
                       action == "contest_crossing" || action == "escalate" || action == "dismiss";
    if (!known) return s;  // unknown action: letter stays awaiting a real order

    letter->awaiting_choice = false;
    cmd.pending_clarification = false;
    Army* cmd_army = nullptr;
    auto ait = s.armies.find(cmd.army_id);
    if (ait != s.armies.end()) cmd_army = &ait->second;
    if (action == "clarify" || action == "preserve_force") {
        cmd.objective = "preserve_force";
        cmd.risk = "cautious";
        letter->status = "approved";
        letter->reason = "Orders clarified: preserve the company; screen only.";
        if (cmd_army) cmd_army->readiness = detail::clamp_int(cmd_army->readiness + 10, 0, 100);
    } else if (action == "counterorder" || action == "contest_crossing") {
        cmd.objective = "contest_crossing";
        cmd.risk = "balanced";
        letter->status = "approved";
        letter->reason = "Counterorder: contest the crossing when powder allows.";
        if (cmd_army) {
            cmd_army->morale = detail::clamp_int(cmd_army->morale + 5, 0, 100);
            cmd_army->readiness = detail::clamp_int(cmd_army->readiness - 5, 0, 100);
        }
    } else if (action == "escalate") {
        cmd.objective = "hold_stores";
        cmd.risk = "aggressive";
        const int support_before = s.local_support;
        s.local_support = detail::clamp_int(s.local_support - 5, 0, 100);
        {
            ResourceChange ch;
            ch.reason = "Escalate: Support " + std::to_string(s.local_support - support_before);
            ch.local_support = s.local_support - support_before;
            detail::record_resource_change(s, ch);
        }
        letter->status = "approved";
        letter->reason =
            "Political escalation through town authorities. If this company loses the field, the town "
            "is forfeit even without the post. Boston stays outside that risk.";
        if (cmd_army) cmd_army->morale = detail::clamp_int(cmd_army->morale + 5, 0, 100);
    } else {
        letter->status = "dismissed";
        letter->reason = "No new orders sent.";
    }
    if (s.pending_event_id == letter->id) s.pending_event_id.clear();
    detail::add_log(s, "commander request resolved: " + action);
    return s;
}

void tick_logistics(CampaignState& s, std::vector<Interrupt>& interrupts) {
    ensure_logistics_defaults(s);
    reset_clerk_day(s);
    release_due_personnel_commitments(s);
    tick_cartridge_jobs(s);
    deliver_due_shipments(s, &interrupts);
    resolve_due_letters(s, &interrupts);
    rest_armies_hourly(s);
    seed_aftermath_letter(s, &interrupts);
    maybe_commander_friction(s, &interrupts);
    maybe_chapter_gates(s, &interrupts);
    seed_opening_war_correspondence(s, &interrupts);
}

}  // namespace pa250
