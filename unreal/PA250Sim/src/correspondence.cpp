#include "internal.hpp"

#include <algorithm>
#include <mutex>
#include <sstream>

#include "nlohmann/json.hpp"

namespace pa250 {
namespace {

using json = nlohmann::json;

std::mutex g_catalog_mu;
bool g_catalog_loaded = false;
std::vector<CorrespondenceCatalogEntry> g_entries;
std::vector<DonorReserve> g_donor_defaults;
std::vector<CommanderLetterChoice> g_commander_choices;
std::string g_commander_province;

CorrespondenceChoiceEffects effects_from_json(const json& fx) {
    CorrespondenceChoiceEffects e;
    e.gold = fx.value("gold", 0);
    e.local_support = fx.value("local_support", 0);
    e.british_attitude = fx.value("british_attitude", 0);
    e.provisions = fx.value("provisions", 0);
    e.powder = fx.value("powder", 0);
    e.delivery_hours = fx.value("delivery_hours", 0);
    e.army_morale = fx.value("army_morale", 0);
    e.army_readiness = fx.value("army_readiness", 0);
    e.clerk_load = fx.value("clerk_load", 0);
    e.favors = fx.value("favors", 0);
    if (fx.contains("factor")) {
        const json& f = fx.at("factor");
        e.factor_province = f.value("province", "");
        e.factor_label = f.value("label", "");
        e.factor_value = f.value("value", 0);
        e.factor_days = f.value("days", 0);
    }
    e.provisions_now = fx.value("provisions_now", 0);
    if (fx.contains("hold_units")) {
        const json& h = fx.at("hold_units");
        e.hold_army = h.value("army_id", "");
        e.hold_class = h.value("unit_class", "");
        e.hold_reason = h.value("reason", "");
        e.hold_count = h.value("count", 0);
        e.hold_hours = h.value("hours", 0);
    }
    if (fx.contains("donor_transfer")) {
        const json& d = fx.at("donor_transfer");
        e.donor_reserve = d.value("reserve", "");
        e.donor_powder = d.value("powder", 0);
        e.donor_hours = d.value("hours", 0);
    }
    e.postpone_offensive = fx.value("postpone_offensive", false);
    return e;
}

// A battle factor a letter leaves at a town: on the pre-battle card with the letter as its source.
void add_letter_factor(CampaignState& s, const std::string& province, const std::string& label, int value, int days,
                       const std::string& source) {
    if (province.empty() || label.empty() || value == 0) return;
    TownFactor f;
    f.province = province;
    f.faction = s.player_faction;
    f.label = label;
    f.value = value;
    f.source = source;
    f.until_hour = days > 0 ? s.now.hour_index + 24LL * days : 0;
    f.one_battle = days <= 0;
    s.pass.town_factors.push_back(f);
}

bool contains_id(const std::vector<std::string>& v, const std::string& id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

bool ymdh_reached(const YMDH& now, const YMDH& gate) {
    if (now.y != gate.y) return now.y > gate.y;
    if (now.m != gate.m) return now.m > gate.m;
    if (now.d != gate.d) return now.d > gate.d;
    return now.h >= gate.h;
}

CorrespondenceCatalogEntry entry_from_json(const json& j) {
    CorrespondenceCatalogEntry e;
    e.source_id = j.value("source_id", "");
    e.event_id = j.value("event_id", "");
    e.mode = j.value("mode", "historical_reference");
    e.author = j.value("author", "");
    e.recipient = j.value("recipient", "");
    e.document_date = j.value("document_date", "");
    e.reported_event_date = j.value("reported_event_date", "");
    e.source_type = j.value("source_type", "");
    e.institutional_url = j.value("institutional_url", "");
    e.uncertainty_notes = j.value("uncertainty_notes", "");
    e.evidence_summary = j.value("evidence_summary", "");
    e.text_kind = j.value("text_kind", "paraphrase_from_report");
    if (j.contains("eligible_from")) {
        const json& y = j.at("eligible_from");
        e.eligible_from = YMDH{y.value("y", 1775), static_cast<uint8_t>(y.value("m", 1)),
                               static_cast<uint8_t>(y.value("d", 1)), static_cast<uint8_t>(y.value("h", 0))};
    }
    e.faction_visibility = j.value("faction_visibility", std::vector<std::string>{});
    e.requires_lexington = j.value("requires_lexington", false);
    e.requires_chapter_continental = j.value("requires_chapter_continental", false);
    e.requires_resolved_events = j.value("requires_resolved_events", std::vector<std::string>{});
    e.activate_decisions = j.value("activate_decisions", false);
    e.adaptation_relay = j.value("adaptation_relay", "");
    e.adaptation_dispatch = j.value("adaptation_dispatch", "");
    e.adaptation_label = j.value("adaptation_label", "Game adaptation—not a historical quotation.");
    if (j.contains("choices")) {
        for (const auto& c : j.at("choices")) {
            CorrespondenceChoiceDef ch;
            ch.id = c.value("id", "");
            ch.label = c.value("label", "");
            ch.effects_summary = c.value("effects_summary", "");
            if (c.contains("effects")) ch.effects = effects_from_json(c.at("effects"));
            e.choices.push_back(ch);
        }
    }
    return e;
}

bool already_received(const CampaignState& s, const std::string& source_id) {
    return contains_id(s.progress.received_source_ids, source_id);
}

bool event_resolved(const CampaignState& s, const std::string& event_id) {
    return contains_id(s.progress.resolved_event_ids, event_id);
}

bool entry_eligible(const CampaignState& s, const CorrespondenceCatalogEntry& e) {
    if (e.source_id.empty() || e.event_id.empty()) return false;
    if (already_received(s, e.source_id)) return false;
    if (!e.faction_visibility.empty() && !contains_id(e.faction_visibility, s.player_faction)) return false;
    if (e.requires_lexington && !s.progress.lexington_fought) return false;
    if (e.requires_chapter_continental && !s.progress.chapter_continental) return false;
    for (const auto& req : e.requires_resolved_events)
        if (!event_resolved(s, req)) return false;
    // Never deliver May/June correspondence during the April 19 contact window.
    if (s.pending_battle.active) {
        const YMDH y = to_ymdh(s.now);
        if (y.y == 1775 && y.m == 4 && y.d == 19) return false;
    }
    return ymdh_reached(to_ymdh(s.now), e.eligible_from);
}

std::string format_historical_body(const CorrespondenceCatalogEntry& e) {
    std::ostringstream ss;
    ss << "HISTORICAL DOCUMENT\n"
       << "Author: " << e.author << "\n"
       << "Recipient: " << e.recipient << "\n"
       << "Document date: " << e.document_date << "\n";
    if (!e.reported_event_date.empty()) ss << "Reported-event date: " << e.reported_event_date << "\n";
    ss << "Source type: " << e.source_type << "\n"
       << "Text kind: " << e.text_kind << " (not a verified verbatim excerpt)\n"
       << "Institutional source: " << e.institutional_url << "\n\n"
       << "Evidence summary:\n"
       << e.evidence_summary << "\n\n"
       << "Uncertainty / editorial notes:\n"
       << e.uncertainty_notes;
    if (e.mode == "retrospective_exhibit") {
        ss << "\n\n[Retrospective historical exhibit — not live intelligence. "
              "Viewing this source never grants rewards or changes campaign state.]";
    }
    return ss.str();
}

std::string format_adaptation_body(const CorrespondenceCatalogEntry& e) {
    std::ostringstream ss;
    ss << e.adaptation_label << "\n"
       << "Relay: " << e.adaptation_relay << "\n\n"
       << e.adaptation_dispatch << "\n\n"
       << "Choices:";
    for (const auto& ch : e.choices) {
        ss << "\n• " << ch.label << " — " << ch.effects_summary;
    }
    return ss.str();
}

CorrespondenceItem* find_awaiting_letter(CampaignState& s) {
    for (auto& c : s.staff.queue)
        if (c.awaiting_choice && (c.status == "needs_approval" || c.status == "pending")) return &c;
    return nullptr;
}

// A source letter goes on top of the desk; it never stops the clock.
void put_on_desk(CampaignState& s, const std::string& letter_id) { s.pending_event_id = letter_id; }

}  // namespace

void load_correspondence_catalog(const std::string& json_text) {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    const json root = json::parse(json_text);
    g_entries.clear();
    g_donor_defaults.clear();
    if (root.contains("entries"))
        for (const auto& e : root.at("entries")) g_entries.push_back(entry_from_json(e));
    if (root.contains("donor_reserve_defaults")) {
        for (const auto& d : root.at("donor_reserve_defaults")) {
            DonorReserve r;
            r.id = d.value("id", "");
            r.label = d.value("label", "");
            r.powder = d.value("powder", 0);
            r.arms = d.value("arms", 0);
            r.provisions = d.value("provisions", 0);
            if (!r.id.empty()) g_donor_defaults.push_back(r);
        }
    }
    g_commander_choices.clear();
    g_commander_province.clear();
    if (root.contains("commander_letter")) {
        const json& cl = root.at("commander_letter");
        g_commander_province = cl.value("province", "");
        for (const auto& c : cl.at("choices")) {
            CommanderLetterChoice ch;
            ch.id = c.value("id", "");
            ch.aliases = c.value("aliases", std::vector<std::string>{});
            ch.label = c.value("label", "");
            ch.effects_summary = c.value("effects_summary", "");
            if (c.contains("effects")) ch.effects = effects_from_json(c.at("effects"));
            if (c.contains("factors"))
                for (const auto& f : c.at("factors")) ch.factors.push_back({f.value("label", ""), f.value("value", 0)});
            g_commander_choices.push_back(ch);
        }
    }
    g_catalog_loaded = true;
}

std::vector<CommanderLetterChoice> commander_letter_choices() {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    return g_commander_choices;
}
std::string commander_letter_province() {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    return g_commander_province;
}

bool correspondence_catalog_loaded() {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    return g_catalog_loaded;
}

const CorrespondenceCatalogEntry* find_correspondence_entry(const std::string& source_id) {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    for (const auto& e : g_entries)
        if (e.source_id == source_id) return &e;
    return nullptr;
}

std::vector<CorrespondenceCatalogEntry> correspondence_catalog_entries() {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    return g_entries;
}

void ensure_donor_reserve_defaults(CampaignState& s) {
    std::lock_guard<std::mutex> lock(g_catalog_mu);
    for (const auto& d : g_donor_defaults) {
        if (s.donor_reserves.find(d.id) == s.donor_reserves.end()) s.donor_reserves[d.id] = d;
    }
}

void release_due_personnel_commitments(CampaignState& s) {
    auto& v = s.progress.personnel_commitments;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const PersonnelCommitment& c) {
                               if (c.release_on_hour > 0 && s.now.hour_index >= c.release_on_hour) {
                                   detail::add_log(s, "personnel commitment released: " + c.id);
                                   return true;
                               }
                               return false;
                           }),
            v.end());
}

void deliver_due_shipments(CampaignState& s, std::vector<Interrupt>* interrupts) {
    for (auto& sh : s.shipments) {
        if (sh.status == "delivered" || sh.status == "lost") continue;
        if (sh.deliver_on_hour > 0 && s.now.hour_index >= sh.deliver_on_hour) {
            std::string dest = sh.dest_province;
            if (dest.empty()) {
                const std::string fac = sh.dest_faction.empty() ? s.player_faction : sh.dest_faction;
                dest = default_depot_province(s, fac);
                sh.dest_province = dest;
            }
            ProvinceStores& st = province_ledger(s, dest);
            const int add_p = std::max(0, sh.powder);
            const int add_c = std::max(0, sh.cartridges);
            const int add_m = std::max(0, sh.muskets) + std::max(0, sh.arms);
            const int add_v = std::max(0, sh.provisions);
            st.powder += add_p;
            st.cartridges += add_c;
            st.muskets += add_m;
            st.bayonets += std::max(0, sh.bayonets);
            st.provisions += add_v;
            st.lead += std::max(0, sh.lead);
            st.paper += std::max(0, sh.paper);
            st.guns += std::max(0, sh.guns);
            sh.status = "delivered";
            ResourceChange ch;
            ch.reason = "Stores arrive at " + (s.provinces.count(dest) ? s.provinces.at(dest).name : std::string("the depot")) +
                        ": +" + std::to_string(add_p) + " powder, +" + std::to_string(add_c) + " cartridges, +" +
                        std::to_string(add_m) + " muskets, +" + std::to_string(add_v) + " provisions" +
                        (sh.guns > 0 ? ", +" + std::to_string(sh.guns) + (sh.guns == 1 ? " field gun" : " field guns") : std::string());
            ch.powder = add_p;
            ch.cartridges = add_c;
            ch.muskets = add_m;
            ch.provisions = add_v;
            ch.province_id = dest;
            detail::record_resource_change(s, ch);
            if (interrupts) {
                (void)interrupts;
            }
        } else if (sh.status == "committed") {
            sh.status = "in_transit";
        }
    }
}

void seed_opening_war_correspondence(CampaignState& s, std::vector<Interrupt>* interrupts) {
    if (!correspondence_catalog_loaded()) return;
    ensure_donor_reserve_defaults(s);
    if (find_awaiting_letter(s)) return;  // one desk item at a time for player choice

    std::vector<CorrespondenceCatalogEntry> entries = correspondence_catalog_entries();
    // Deterministic order: catalog order.
    for (const auto& e : entries) {
        if (!entry_eligible(s, e)) continue;

        CorrespondenceItem letter;
        letter.id = "src_" + e.source_id + "_" + std::to_string(s.next_command_seq++);
        letter.created_hour = s.now.hour_index;
        letter.source_id = e.source_id;
        letter.event_id = e.event_id;
        letter.fictionalized = true;
        letter.recipient = "committee_of_safety";
        letter.subject = e.source_id + " — " + e.author + " (" + e.document_date + ")";

        const bool playable = e.activate_decisions && !e.choices.empty();
        if (playable) {
            letter.kind = "source_decision";
            letter.status = "needs_approval";
            letter.awaiting_choice = true;
            // The question comes first; the document it is drawn from follows for reading.
            letter.body = "PLAYABLE ADAPTATION\n" + format_adaptation_body(e) + "\n\n----\n\n" + format_historical_body(e);
        } else {
            // A source with no decision attached is filed for reading: nothing to answer,
            // no rewards, and it never takes the screen.
            letter.kind = "historical_notice";
            letter.status = "delivered";
            letter.awaiting_choice = false;
            letter.decision_id = "filed";
            letter.decision_applied = true;
            letter.reason = "Filed for reading. Viewing this source does not change campaign state.";
            if (!contains_id(s.progress.resolved_event_ids, e.event_id))
                s.progress.resolved_event_ids.push_back(e.event_id);
            letter.body = format_historical_body(e);
            if (e.source_id == "L2" && s.player_faction == kContinental) {
                letter.subject = e.source_id + " — Crown draft (retrospective exhibit)";
                letter.reason =
                    "Confidential Crown planning shown only as a retrospective historical exhibit. "
                    "Not live Patriot intelligence. Viewing grants no reward.";
            }
        }

        s.staff.queue.push_back(letter);
        s.progress.received_source_ids.push_back(e.source_id);
        if (playable) put_on_desk(s, letter.id);
        detail::add_log(s, "correspondence received: " + e.source_id + " (" + e.mode + ")");
        return;  // one new letter per tick
    }
}

CampaignState apply_source_decision(const CampaignState& in, const std::string& letter_id,
                                    const std::string& choice) {
    CampaignState s = in;
    CorrespondenceItem* letter = nullptr;
    for (auto& c : s.staff.queue)
        if (c.id == letter_id) {
            letter = &c;
            break;
        }
    if (!letter) {
        // Fall back to first awaiting source letter.
        for (auto& c : s.staff.queue)
            if (c.awaiting_choice && (c.kind == "source_decision" || c.kind == "historical_notice")) {
                letter = &c;
                break;
            }
    }
    if (!letter) return s;
    if (letter->decision_applied) {
        if (s.pending_event_id == letter->id) s.pending_event_id.clear();
        return s;
    }
    if (!letter->awaiting_choice && letter->status != "needs_approval") return s;

    const CorrespondenceCatalogEntry* entry = find_correspondence_entry(letter->source_id);
    if (!entry) {
        letter->awaiting_choice = true;
        return s;
    }

    // historical_notice: acknowledge only — never apply rewards.
    if (letter->kind == "historical_notice" || !entry->activate_decisions) {
        letter->awaiting_choice = false;
        letter->status = "dismissed";
        letter->decision_id = "acknowledge";
        letter->decision_applied = true;
        if (!contains_id(s.progress.resolved_event_ids, letter->event_id) && !letter->event_id.empty())
            s.progress.resolved_event_ids.push_back(letter->event_id);
        letter->reason = "Acknowledged. Source viewing does not change campaign state.";
        if (s.pending_event_id == letter->id) s.pending_event_id.clear();
        return s;
    }

    if (choice == "dismiss") {
        letter->awaiting_choice = false;
        letter->status = "dismissed";
        letter->decision_id = "dismiss";
        letter->decision_applied = true;
        if (!contains_id(s.progress.resolved_event_ids, letter->event_id))
            s.progress.resolved_event_ids.push_back(letter->event_id);
        letter->reason = "Set aside without a logistics commitment.";
        if (s.pending_event_id == letter->id) s.pending_event_id.clear();
        return s;
    }

    bool known = false;
    const CorrespondenceChoiceDef* chosen = nullptr;
    for (const auto& ch : entry->choices)
        if (ch.id == choice) {
            known = true;
            chosen = &ch;
        }
    if (!known) {
        letter->awaiting_choice = true;
        detail::add_log(s, "unknown source choice: " + choice);
        return s;
    }

    letter->awaiting_choice = false;
    letter->decision_id = choice;
    letter->decision_applied = true;
    if (!contains_id(s.progress.resolved_event_ids, letter->event_id))
        s.progress.resolved_event_ids.push_back(letter->event_id);

    const std::string sid = letter->source_id;

    if (chosen && chosen->effects.any()) {
        // Every consequence comes from the catalog (GAMEPLAY TUNING lives in the catalog JSON); no
        // letter has a branch of its own here.
        const CorrespondenceChoiceEffects& fx = chosen->effects;
        auto refuse = [&](const std::string& why) {
            letter->status = "refused";
            letter->reason = why;
            if (s.pending_event_id == letter->id) s.pending_event_id.clear();
        };
        // What cannot be carried out is refused before anything is spent.
        if (fx.gold < 0 && s.player_gold < -fx.gold) {
            refuse("The treasury cannot spare " + std::to_string(-fx.gold) + " gold for this.");
            return s;
        }
        int donor_take = 0;
        if (fx.donor_powder > 0) {
            ensure_donor_reserve_defaults(s);
            const auto it = s.donor_reserves.find(fx.donor_reserve);
            if (it == s.donor_reserves.end()) {
                refuse("No donor reserve on the books.");
                return s;
            }
            donor_take = std::min(fx.donor_powder, std::max(0, it->second.powder));
            if (donor_take <= 0) {
                refuse(it->second.label + " is empty. Try procurement, or conserve.");
                return s;
            }
        }
        ResourceChange ch;
        ch.reason = sid + " — " + chosen->label;
        s.player_gold += fx.gold;
        ch.gold = fx.gold;
        if (fx.gold < 0) letter->gold_cost = -fx.gold;
        const int support_before = s.local_support;
        s.local_support = detail::clamp_int(s.local_support + fx.local_support, 0, 100);
        ch.local_support = s.local_support - support_before;
        if (fx.british_attitude != 0) {
            auto rit = s.relations.find(kBritish);
            if (rit != s.relations.end())
                rit->second = detail::clamp_int(rit->second + fx.british_attitude, -100, 100);
        }
        for (auto& kv : s.armies) {
            if (kv.second.faction != s.player_faction) continue;
            kv.second.morale = detail::clamp_int(kv.second.morale + fx.army_morale, 0, 100);
            kv.second.readiness = detail::clamp_int(kv.second.readiness + fx.army_readiness, 0, 100);
        }
        s.staff.letters_processed_today += std::max(0, fx.clerk_load);
        s.pass.favors = std::max(0, s.pass.favors + fx.favors);
        add_letter_factor(s, fx.factor_province, fx.factor_label, fx.factor_value, fx.factor_days, "Letter " + sid + ": " + chosen->label);
        if (fx.hold_count > 0) {
            // Kept from the field, not disarmed: build_battle_setup leaves them out until released.
            PersonnelCommitment pc;
            pc.id = "pc_" + letter->id;
            pc.army_id = fx.hold_army;
            pc.unit_class = fx.hold_class;
            pc.count = fx.hold_count;
            pc.reason = fx.hold_reason;
            pc.release_on_hour = s.now.hour_index + fx.hold_hours;
            s.progress.personnel_commitments.push_back(pc);
        }
        const std::string depot = default_depot_province(s, s.player_faction);
        if (donor_take > 0) {
            // The donor is short at once; the depot is credited only by the shipment, exactly once.
            s.donor_reserves.at(fx.donor_reserve).powder -= donor_take;
            Shipment sh;
            sh.id = "ship_" + letter->id;
            sh.donor_reserve_id = fx.donor_reserve;
            sh.dest_faction = s.player_faction;
            sh.dest_province = depot;
            sh.powder = donor_take;
            sh.commit_hour = s.now.hour_index;
            sh.deliver_on_hour = s.now.hour_index + fx.donor_hours;
            sh.status = "in_transit";
            s.shipments.push_back(sh);
        }
        if (fx.postpone_offensive) s.progress.offensive_postponed = true;
        // Stores: what moves now, and what is committed now and received later (resolve_due_letters
        // credits the depot exactly once).
        const bool later = fx.delivery_hours > 0 && (fx.provisions > 0 || fx.powder > 0);
        const int provisions_now = fx.provisions_now + (later ? 0 : fx.provisions);
        const int powder_now = later ? 0 : fx.powder;
        if (provisions_now != 0 || powder_now != 0) {
            ProvinceStores& st = province_ledger(s, depot);
            const int prov_before = st.provisions;
            const int powder_before = st.powder;
            st.provisions = std::max(0, st.provisions + provisions_now);
            st.powder = std::max(0, st.powder + powder_now);
            ch.provisions = st.provisions - prov_before;
            ch.powder = st.powder - powder_before;
            if (ch.provisions || ch.powder) ch.province_id = depot;
        }
        if (later) {
            letter->committed_provisions = std::max(0, fx.provisions);
            letter->committed_powder = std::max(0, fx.powder);
            letter->resolve_on_hour = s.now.hour_index + fx.delivery_hours;
            letter->status = "approved";
        } else {
            letter->status = "delivered";
        }
        letter->reason = chosen->label + ". " + chosen->effects_summary;
        if (later) letter->reason += " In transit: " + std::to_string(fx.delivery_hours) + "h.";
        if (donor_take > 0 && donor_take < fx.donor_powder)
            letter->reason += " The reserve could spare only " + std::to_string(donor_take) + " powder.";
        if (donor_take > 0) letter->reason += " On the road: " + std::to_string(fx.donor_hours) + "h.";
        detail::record_resource_change(s, ch);
        detail::add_log(s, sid + " decision: " + choice);
    } else {
        // A choice with no listed consequences (a deliberate "wait and see").
        letter->status = "delivered";
        letter->reason = chosen ? chosen->label + ". " + chosen->effects_summary
                                : std::string("No playable adaptation for this source.");
    }

    if (s.pending_event_id == letter->id) s.pending_event_id.clear();
    return s;
}

}  // namespace pa250
