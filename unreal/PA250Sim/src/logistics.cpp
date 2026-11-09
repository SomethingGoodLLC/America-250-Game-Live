#include "internal.hpp"

#include <sstream>

namespace pa250 {
namespace {

FactionStores& faction_stores(CampaignState& s, const std::string& faction) {
    return s.stores[faction];
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
    if (letter.committed_powder <= 0 && letter.committed_arms <= 0 && letter.committed_provisions <= 0 &&
        letter.kind != "capacity_investment" && letter.kind != "knox_support") {
        letter.status = "delivered";
        return;
    }
    FactionStores& st = faction_stores(s, s.player_faction);
    // Never double-count: only add the unpaid remainder.
    const int add_p = letter.committed_powder - letter.received_powder;
    const int add_a = letter.committed_arms - letter.received_arms;
    const int add_v = letter.committed_provisions - letter.received_provisions;
    if (add_p > 0) {
        st.powder += add_p;
        letter.received_powder += add_p;
    }
    if (add_a > 0) {
        st.arms += add_a;
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
    detail::add_log(s, "delivery received for " + letter.id + " powder+" + std::to_string(std::max(0, add_p)) +
                           " arms+" + std::to_string(std::max(0, add_a)) + " prov+" +
                           std::to_string(std::max(0, add_v)));
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
    letter.subject = "Powder and arms after the Lexington clash";
    letter.body =
        "[FICTIONALIZED] Clerk Hale presents three paths: purchase from a merchant, petition the "
        "Committee of Safety, or requisition from local stocks. Recorded history of 19 April is "
        "uncertain on the first shot at Lexington; this letter does not assert who fired. "
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
    if (s.stores.find(kContinental) == s.stores.end())
        s.stores[kContinental] = FactionStores{8, 10, 20};
    if (s.stores.find(kBritish) == s.stores.end()) s.stores[kBritish] = FactionStores{40, 30, 40};
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
                detail::add_log(s, "knox project underway");
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
        const int cost = 40;
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
        letter->committed_powder = 10;
        // A ready repair contact returns extra serviceable arms with each wagon.
        letter->committed_arms = s.progress.capacity_level >= 1 ? 6 : 4;
        letter->resolve_on_hour = s.now.hour_index + 4 + (clerk_backlogged ? 24 : 0);
        letter->reason = clerk_backlogged ? "Clerk's desk is full; the merchant wagon slips a day."
                                          : "Merchant wagon hired; goods in transit.";
        detail::add_log(s, "purchase committed: -" + std::to_string(cost) + " gold");
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
        detail::add_log(s, "assistance request: " + letter->status);
    } else if (choice == "requisition") {
        letter->kind = "requisition";
        letter->recipient = "town_selectmen";
        FactionStores& st = faction_stores(s, s.player_faction);
        // Immediate relief; exhausts local stocks and risks cooperation.
        const int take_p = std::min(8, std::max(2, st.provisions / 2));
        st.powder += take_p;
        st.arms += 2;
        st.provisions = std::max(0, st.provisions - take_p - 4);
        s.local_support = detail::clamp_int(s.local_support - 12, 0, 100);
        letter->status = "delivered";
        letter->received_powder = take_p;
        letter->received_arms = 2;
        letter->committed_powder = take_p;
        letter->committed_arms = 2;
        letter->reason = "Emergency requisition filled from town stocks; local support falls.";
        detail::add_log(s, "requisition: powder+" + std::to_string(take_p) + " support=" +
                               std::to_string(s.local_support));
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
        FactionStores& st = faction_stores(s, s.player_faction);
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
        letter->committed_arms = 4;  // serviceable arms the bench yields when ready
        letter->status = "pending";
        letter->resolve_on_hour = s.now.hour_index + 48;  // two days of work — not during a fight
        letter->reason = "Repair contact and procurement channel begun; no instant weapon upgrade.";
        s.progress.capacity_ready_hour = letter->resolve_on_hour;
        detail::add_log(s, "capacity investment started");
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
        s.local_support = detail::clamp_int(s.local_support - 5, 0, 100);
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
    deliver_due_shipments(s, &interrupts);
    resolve_due_letters(s, &interrupts);
    rest_armies_hourly(s);
    seed_aftermath_letter(s, &interrupts);
    maybe_commander_friction(s, &interrupts);
    maybe_chapter_gates(s, &interrupts);
    seed_opening_war_correspondence(s, &interrupts);
}

}  // namespace pa250
