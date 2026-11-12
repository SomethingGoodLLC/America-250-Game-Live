// Scenario loading, canonical save format, state hash, and JSON bridges.
#include <deque>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include "internal.hpp"
#include "nlohmann/json.hpp"

namespace pa250 {

using json = nlohmann::json;

void rebuild_adjacency(CampaignState& s) {
    for (auto& kv : s.provinces) kv.second.adjacent.clear();
    for (const auto& ek : s.edges) {
        const Edge& e = ek.second;
        if (!s.provinces.count(e.a) || !s.provinces.count(e.b)) continue;
        s.provinces[e.a].adjacent.push_back(e.b);
        s.provinces[e.b].adjacent.push_back(e.a);
    }
    for (auto& kv : s.provinces) {
        auto& adj = kv.second.adjacent;
        std::sort(adj.begin(), adj.end());
        adj.erase(std::unique(adj.begin(), adj.end()), adj.end());
    }
}

std::vector<std::string> validate_scenario_graph(const CampaignState& s) {
    std::vector<std::string> errors;
    std::set<int> indices;
    int max_idx = 0;
    for (const auto& kv : s.provinces) {
        if (kv.second.map_index < 1) errors.push_back("map_index < 1 for " + kv.first);
        if (!indices.insert(kv.second.map_index).second) errors.push_back("duplicate map_index " + kv.first);
        max_idx = std::max(max_idx, kv.second.map_index);
    }
    if (static_cast<int>(indices.size()) != static_cast<int>(s.provinces.size()))
        errors.push_back("map_index count mismatch");
    for (int i = 1; i <= max_idx; ++i)
        if (!indices.count(i)) errors.push_back("map_index gap at " + std::to_string(i));
    if (max_idx != static_cast<int>(s.provinces.size())) errors.push_back("map_index not contiguous 1..N");

    std::set<std::string> edge_ids;
    for (const auto& ek : s.edges) {
        const Edge& e = ek.second;
        if (e.id != ek.first) errors.push_back("edge key/id mismatch " + ek.first);
        if (!edge_ids.insert(e.id).second) errors.push_back("duplicate edge id " + e.id);
        if (!s.provinces.count(e.a) || !s.provinces.count(e.b))
            errors.push_back("edge endpoint missing: " + e.id);
    }

    // Connectivity via BFS on adjacency.
    if (!s.provinces.empty()) {
        std::string start = s.provinces.begin()->first;
        std::set<std::string> seen{start};
        std::deque<std::string> q{start};
        while (!q.empty()) {
            std::string cur = q.front();
            q.pop_front();
            for (const auto& n : s.provinces.at(cur).adjacent)
                if (seen.insert(n).second) q.push_back(n);
        }
        if (seen.size() != s.provinces.size()) errors.push_back("province graph not connected");
    }
    return errors;
}

namespace {

json pts_to_json(const std::vector<Point>& v) {
    json a = json::array();
    for (const auto& p : v) a.push_back(json::array({p.x, p.y}));
    return a;
}
std::vector<Point> pts_from_json(const json& a) {
    std::vector<Point> v;
    for (const auto& p : a) v.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
    return v;
}
json treaty_to_json(const Treaty& t) {
    return {{"type", t.type},
            {"hours_remaining", t.hours_remaining},
            {"army_id", t.army_id},
            {"province_id", t.province_id}};
}
Treaty treaty_from_json(const json& j) {
    Treaty t;
    t.type = j.at("type").get<std::string>();
    t.hours_remaining = j.at("hours_remaining").get<int>();
    t.army_id = j.value("army_id", "");
    t.province_id = j.value("province_id", "");
    return t;
}
json army_to_json(const Army& a) {
    return {{"id", a.id},
            {"name", a.name},
            {"faction", a.faction},
            {"province", a.province},
            {"previous_province", a.previous_province},
            {"units", a.units},
            {"sick", a.sick},
            {"wounded", a.wounded},
            {"morale", a.morale},
            {"readiness", a.readiness},
            {"fatigue", a.fatigue},
            {"path", a.path},
            {"hours_into_edge", a.hours_into_edge}};
}
Army army_from_json(const json& j) {
    Army a;
    a.id = j.at("id").get<std::string>();
    a.name = j.value("name", a.id);
    a.faction = j.at("faction").get<std::string>();
    a.province = j.at("province").get<std::string>();
    a.previous_province = j.value("previous_province", "");
    a.units = j.at("units").get<std::map<std::string, int>>();
    a.sick = j.value("sick", std::map<std::string, int>{});
    a.wounded = j.value("wounded", std::map<std::string, int>{});
    a.morale = j.value("morale", 70);
    a.readiness = j.value("readiness", 100);
    a.fatigue = j.value("fatigue", 0);
    a.path = j.value("path", std::vector<std::string>{});
    a.hours_into_edge = j.value("hours_into_edge", 0);
    return a;
}
json stores_to_json(const FactionStores& st) {
    return {{"powder", st.powder}, {"arms", st.arms}, {"provisions", st.provisions}};
}
FactionStores stores_from_json(const json& j) {
    FactionStores st;
    st.powder = j.value("powder", 0);
    st.arms = j.value("arms", 0);
    st.provisions = j.value("provisions", 0);
    return st;
}
json province_stores_to_json(const ProvinceStores& st) {
    return {{"powder", st.powder},
            {"cartridges", st.cartridges},
            {"muskets", st.muskets},
            {"bayonets", st.bayonets},
            {"provisions", st.provisions},
            {"lead", st.lead},
            {"paper", st.paper}};
}
ProvinceStores province_stores_from_json(const json& j) {
    ProvinceStores st;
    st.powder = j.value("powder", 0);
    st.cartridges = j.value("cartridges", 0);
    st.muskets = j.value("muskets", j.value("arms", 0));
    st.bayonets = j.value("bayonets", 0);
    st.provisions = j.value("provisions", 0);
    st.lead = j.value("lead", 0);
    st.paper = j.value("paper", 0);
    return st;
}
json letter_to_json(const CorrespondenceItem& c) {
    return {{"id", c.id},
            {"created_hour", c.created_hour},
            {"resolve_on_hour", c.resolve_on_hour},
            {"recipient", c.recipient},
            {"kind", c.kind},
            {"status", c.status},
            {"powder", c.powder},
            {"arms", c.arms},
            {"provisions", c.provisions},
            {"gold_cost", c.gold_cost},
            {"committed_powder", c.committed_powder},
            {"committed_arms", c.committed_arms},
            {"committed_provisions", c.committed_provisions},
            {"received_powder", c.received_powder},
            {"received_arms", c.received_arms},
            {"received_provisions", c.received_provisions},
            {"fictionalized", c.fictionalized},
            {"subject", c.subject},
            {"body", c.body},
            {"reason", c.reason},
            {"awaiting_choice", c.awaiting_choice},
            {"source_id", c.source_id},
            {"event_id", c.event_id},
            {"decision_id", c.decision_id},
            {"decision_applied", c.decision_applied}};
}
CorrespondenceItem letter_from_json(const json& j) {
    CorrespondenceItem c;
    c.id = j.value("id", "");
    c.created_hour = j.value("created_hour", int64_t{0});
    c.resolve_on_hour = j.value("resolve_on_hour", int64_t{0});
    c.recipient = j.value("recipient", "");
    c.kind = j.value("kind", "");
    c.status = j.value("status", "");
    c.powder = j.value("powder", 0);
    c.arms = j.value("arms", 0);
    c.provisions = j.value("provisions", 0);
    c.gold_cost = j.value("gold_cost", 0);
    c.committed_powder = j.value("committed_powder", 0);
    c.committed_arms = j.value("committed_arms", 0);
    c.committed_provisions = j.value("committed_provisions", 0);
    c.received_powder = j.value("received_powder", 0);
    c.received_arms = j.value("received_arms", 0);
    c.received_provisions = j.value("received_provisions", 0);
    c.fictionalized = j.value("fictionalized", true);
    c.subject = j.value("subject", "");
    c.body = j.value("body", "");
    c.reason = j.value("reason", "");
    c.awaiting_choice = j.value("awaiting_choice", false);
    c.source_id = j.value("source_id", "");
    c.event_id = j.value("event_id", "");
    c.decision_id = j.value("decision_id", "");
    c.decision_applied = j.value("decision_applied", false);
    return c;
}
json personnel_to_json(const PersonnelCommitment& p) {
    return {{"id", p.id},
            {"army_id", p.army_id},
            {"unit_class", p.unit_class},
            {"count", p.count},
            {"reason", p.reason},
            {"release_on_hour", p.release_on_hour}};
}
PersonnelCommitment personnel_from_json(const json& j) {
    PersonnelCommitment p;
    p.id = j.value("id", "");
    p.army_id = j.value("army_id", "");
    p.unit_class = j.value("unit_class", "");
    p.count = j.value("count", 0);
    p.reason = j.value("reason", "");
    p.release_on_hour = j.value("release_on_hour", int64_t{0});
    return p;
}
json donor_to_json(const DonorReserve& d) {
    return {{"id", d.id},
            {"label", d.label},
            {"powder", d.powder},
            {"arms", d.arms},
            {"provisions", d.provisions}};
}
DonorReserve donor_from_json(const json& j) {
    DonorReserve d;
    d.id = j.value("id", "");
    d.label = j.value("label", "");
    d.powder = j.value("powder", 0);
    d.arms = j.value("arms", 0);
    d.provisions = j.value("provisions", 0);
    return d;
}
json shipment_to_json(const Shipment& sh) {
    return {{"id", sh.id},
            {"donor_reserve_id", sh.donor_reserve_id},
            {"source_province", sh.source_province},
            {"dest_province", sh.dest_province},
            {"dest_faction", sh.dest_faction},
            {"powder", sh.powder},
            {"cartridges", sh.cartridges},
            {"muskets", sh.muskets},
            {"bayonets", sh.bayonets},
            {"arms", sh.arms},
            {"provisions", sh.provisions},
            {"lead", sh.lead},
            {"paper", sh.paper},
            {"commit_hour", sh.commit_hour},
            {"deliver_on_hour", sh.deliver_on_hour},
            {"status", sh.status}};
}
Shipment shipment_from_json(const json& j) {
    Shipment sh;
    sh.id = j.value("id", "");
    sh.donor_reserve_id = j.value("donor_reserve_id", "");
    sh.source_province = j.value("source_province", "");
    sh.dest_province = j.value("dest_province", "");
    sh.dest_faction = j.value("dest_faction", "");
    sh.powder = j.value("powder", 0);
    sh.cartridges = j.value("cartridges", 0);
    sh.muskets = j.value("muskets", 0);
    sh.bayonets = j.value("bayonets", 0);
    sh.arms = j.value("arms", 0);
    if (sh.muskets == 0 && sh.arms > 0) sh.muskets = sh.arms;
    sh.provisions = j.value("provisions", 0);
    sh.lead = j.value("lead", 0);
    sh.paper = j.value("paper", 0);
    sh.commit_hour = j.value("commit_hour", int64_t{0});
    sh.deliver_on_hour = j.value("deliver_on_hour", int64_t{0});
    sh.status = j.value("status", "");
    return sh;
}
json cartridge_job_to_json(const CartridgeJob& job) {
    return {{"id", job.id},
            {"province_id", job.province_id},
            {"army_id", job.army_id},
            {"unit_class", job.unit_class},
            {"workers", job.workers},
            {"powder", job.powder},
            {"lead", job.lead},
            {"paper", job.paper},
            {"cartridges_out", job.cartridges_out},
            {"started_hour", job.started_hour},
            {"complete_on_hour", job.complete_on_hour},
            {"status", job.status}};
}
CartridgeJob cartridge_job_from_json(const json& j) {
    CartridgeJob job;
    job.id = j.value("id", "");
    job.province_id = j.value("province_id", "");
    job.army_id = j.value("army_id", "");
    job.unit_class = j.value("unit_class", "militia");
    job.workers = j.value("workers", 0);
    job.powder = j.value("powder", 0);
    job.lead = j.value("lead", 0);
    job.paper = j.value("paper", 0);
    job.cartridges_out = j.value("cartridges_out", 0);
    job.started_hour = j.value("started_hour", int64_t{0});
    job.complete_on_hour = j.value("complete_on_hour", int64_t{0});
    job.status = j.value("status", "");
    return job;
}
json construction_job_to_json(const ConstructionJob& job) {
    return {{"id", job.id},
            {"province_id", job.province_id},
            {"building_id", job.building_id},
            {"is_upgrade", job.is_upgrade},
            {"hours_remaining", job.hours_remaining},
            {"hours_total", job.hours_total},
            {"paid_gold", job.paid_gold},
            {"status", job.status}};
}
ConstructionJob construction_job_from_json(const json& j) {
    ConstructionJob job;
    job.id = j.value("id", "");
    job.province_id = j.value("province_id", "");
    job.building_id = j.value("building_id", "");
    job.is_upgrade = j.value("is_upgrade", false);
    job.hours_remaining = j.value("hours_remaining", 0);
    job.hours_total = j.value("hours_total", 0);
    job.paid_gold = j.value("paid_gold", 0);
    job.status = j.value("status", "");
    return job;
}
json recruit_job_to_json(const RecruitJob& job) {
    return {{"id", job.id},
            {"province_id", job.province_id},
            {"unit_id", job.unit_id},
            {"unit_class", job.unit_class},
            {"men", job.men},
            {"hours_remaining", job.hours_remaining},
            {"hours_total", job.hours_total},
            {"paid_gold", job.paid_gold},
            {"paid_muskets", job.paid_muskets},
            {"paid_provisions", job.paid_provisions}};
}
RecruitJob recruit_job_from_json(const json& j) {
    RecruitJob job;
    job.id = j.value("id", "");
    job.province_id = j.value("province_id", "");
    job.unit_id = j.value("unit_id", "");
    job.unit_class = j.value("unit_class", "militia");
    job.men = j.value("men", 0);
    job.hours_remaining = j.value("hours_remaining", 0);
    job.hours_total = j.value("hours_total", 0);
    job.paid_gold = j.value("paid_gold", 0);
    job.paid_muskets = j.value("paid_muskets", 0);
    job.paid_provisions = j.value("paid_provisions", 0);
    return job;
}
json resource_change_to_json(const ResourceChange& c) {
    return {{"hour_index", c.hour_index},
            {"reason", c.reason},
            {"gold", c.gold},
            {"powder", c.powder},
            {"cartridges", c.cartridges},
            {"muskets", c.muskets},
            {"provisions", c.provisions},
            {"local_support", c.local_support},
            {"province_id", c.province_id}};
}
ResourceChange resource_change_from_json(const json& j) {
    ResourceChange c;
    c.hour_index = j.value("hour_index", int64_t{0});
    c.reason = j.value("reason", "");
    c.gold = j.value("gold", 0);
    c.powder = j.value("powder", 0);
    c.cartridges = j.value("cartridges", 0);
    c.muskets = j.value("muskets", 0);
    c.provisions = j.value("provisions", 0);
    c.local_support = j.value("local_support", 0);
    c.province_id = j.value("province_id", "");
    return c;
}
json battle_report_to_json(const BattleReport& r) {
    return {{"province_id", r.province_id},
            {"winner", r.winner},
            {"province_owner_before", r.province_owner_before},
            {"province_owner_after", r.province_owner_after},
            {"local_support_before", r.local_support_before},
            {"local_support_after", r.local_support_after},
            {"gold_before", r.gold_before},
            {"gold_after", r.gold_after},
            {"objective_holder", r.objective_holder},
            {"objectives_held", r.objectives_held},
            {"losses", r.losses},
            {"wounded", r.wounded},
            {"cartridges_spent", r.cartridges_spent},
            {"powder_spent", r.powder_spent},
            {"morale_delta", r.morale_delta},
            {"ownership_reason", r.ownership_reason},
            {"change_reasons", r.change_reasons},
            {"defender_reward_support", r.defender_reward_support},
            {"personal_kills", r.personal_kills},
            {"retreat_to", r.retreat_to},
            {"loser_fate", r.loser_fate},
            {"scavenged_cartridges", r.scavenged_cartridges},
            {"scavenged_powder", r.scavenged_powder},
            {"scavenged_muskets", r.scavenged_muskets}};
}
BattleReport battle_report_from_json(const json& j) {
    BattleReport r;
    r.province_id = j.value("province_id", "");
    r.winner = j.value("winner", "");
    r.province_owner_before = j.value("province_owner_before", "");
    r.province_owner_after = j.value("province_owner_after", "");
    r.local_support_before = j.value("local_support_before", 0);
    r.local_support_after = j.value("local_support_after", 0);
    r.gold_before = j.value("gold_before", 0);
    r.gold_after = j.value("gold_after", 0);
    r.objective_holder = j.value("objective_holder", "");
    r.objectives_held = j.value("objectives_held", std::map<std::string, std::string>{});
    r.losses = j.value("losses", std::map<std::string, std::map<std::string, int>>{});
    r.wounded = j.value("wounded", std::map<std::string, std::map<std::string, int>>{});
    r.cartridges_spent = j.value("cartridges_spent", std::map<std::string, int>{});
    r.powder_spent = j.value("powder_spent", std::map<std::string, int>{});
    r.morale_delta = j.value("morale_delta", std::map<std::string, int>{});
    r.ownership_reason = j.value("ownership_reason", "");
    r.change_reasons = j.value("change_reasons", std::vector<std::string>{});
    r.defender_reward_support = j.value("defender_reward_support", 0);
    r.personal_kills = j.value("personal_kills", 0);
    r.retreat_to = j.value("retreat_to", "");
    r.loser_fate = j.value("loser_fate", "");
    r.scavenged_cartridges = j.value("scavenged_cartridges", 0);
    r.scavenged_powder = j.value("scavenged_powder", 0);
    r.scavenged_muskets = j.value("scavenged_muskets", 0);
    return r;
}
json staff_to_json(const StaffState& st) {
    json q = json::array();
    for (const auto& c : st.queue) q.push_back(letter_to_json(c));
    return {{"clerk_name", st.clerk_name},
            {"capacity_per_day", st.capacity_per_day},
            {"letters_processed_today", st.letters_processed_today},
            {"capacity_day_index", st.capacity_day_index},
            {"queue", q}};
}
StaffState staff_from_json(const json& j) {
    StaffState st;
    st.clerk_name = j.value("clerk_name", "Clerk Hale");
    st.capacity_per_day = j.value("capacity_per_day", 2);
    st.letters_processed_today = j.value("letters_processed_today", 0);
    st.capacity_day_index = j.value("capacity_day_index", int64_t{-1});
    if (j.contains("queue"))
        for (const auto& c : j.at("queue")) st.queue.push_back(letter_from_json(c));
    return st;
}
json progress_to_json(const CampaignProgress& p) {
    json pcs = json::array();
    for (const auto& pc : p.personnel_commitments) pcs.push_back(personnel_to_json(pc));
    json jobs = json::array();
    for (const auto& job : p.cartridge_jobs) jobs.push_back(cartridge_job_to_json(job));
    json builds = json::array();
    for (const auto& job : p.construction_jobs) builds.push_back(construction_job_to_json(job));
    json musters = json::array();
    for (const auto& job : p.recruit_jobs) musters.push_back(recruit_job_to_json(job));
    return {{"battles_fought", p.battles_fought},
            {"lexington_fought", p.lexington_fought},
            {"aftermath_pending", p.aftermath_pending},
            {"concord_followup_seeded", p.concord_followup_seeded},
            {"chapter_continental", p.chapter_continental},
            {"chapter_washington", p.chapter_washington},
            {"capacity_level", p.capacity_level},
            {"capacity_ready_hour", p.capacity_ready_hour},
            {"player_role", p.player_role},
            {"personnel_commitments", pcs},
            {"cartridge_jobs", jobs},
            {"construction_jobs", builds},
            {"recruit_jobs", musters},
            {"treasury_debt", p.treasury_debt},
            {"pay_arrears_months", p.pay_arrears_months},
            {"enemy_gold", p.enemy_gold},
            {"enemy_arrears_months", p.enemy_arrears_months},
            {"last_battle_report", battle_report_to_json(p.last_battle_report)},
            {"resolved_event_ids", p.resolved_event_ids},
            {"received_source_ids", p.received_source_ids},
            {"offensive_postponed", p.offensive_postponed},
            {"knox",
             {{"id", p.knox.id},
              {"status", p.knox.status},
              {"start_hour", p.knox.start_hour},
              {"ready_hour", p.knox.ready_hour},
              {"pieces", p.knox.pieces},
              {"transport_committed", p.knox.transport_committed},
              {"fictionalized_authorization", p.knox.fictionalized_authorization}}},
            {"commander",
             {{"id", p.commander.id},
              {"name", p.commander.name},
              {"army_id", p.commander.army_id},
              {"objective", p.commander.objective},
              {"risk", p.commander.risk},
              {"pending_clarification", p.commander.pending_clarification},
              {"last_request", p.commander.last_request}}}};
}
CampaignProgress progress_from_json(const json& j) {
    CampaignProgress p;
    p.battles_fought = j.value("battles_fought", 0);
    p.lexington_fought = j.value("lexington_fought", false);
    p.aftermath_pending = j.value("aftermath_pending", false);
    p.concord_followup_seeded = j.value("concord_followup_seeded", false);
    p.chapter_continental = j.value("chapter_continental", false);
    p.chapter_washington = j.value("chapter_washington", false);
    p.capacity_level = j.value("capacity_level", 0);
    p.capacity_ready_hour = j.value("capacity_ready_hour", int64_t{0});
    p.player_role = j.value("player_role", "massachusetts_field_commander");
    p.resolved_event_ids = j.value("resolved_event_ids", std::vector<std::string>{});
    p.received_source_ids = j.value("received_source_ids", std::vector<std::string>{});
    p.offensive_postponed = j.value("offensive_postponed", false);
    if (j.contains("personnel_commitments"))
        for (const auto& pc : j.at("personnel_commitments"))
            p.personnel_commitments.push_back(personnel_from_json(pc));
    if (j.contains("cartridge_jobs"))
        for (const auto& job : j.at("cartridge_jobs"))
            p.cartridge_jobs.push_back(cartridge_job_from_json(job));
    if (j.contains("construction_jobs"))
        for (const auto& job : j.at("construction_jobs"))
            p.construction_jobs.push_back(construction_job_from_json(job));
    p.treasury_debt = j.value("treasury_debt", 0);
    p.pay_arrears_months = j.value("pay_arrears_months", 0);
    p.enemy_gold = j.value("enemy_gold", 60);
    p.enemy_arrears_months = j.value("enemy_arrears_months", 0);
    if (j.contains("recruit_jobs"))
        for (const auto& job : j.at("recruit_jobs")) p.recruit_jobs.push_back(recruit_job_from_json(job));
    if (j.contains("last_battle_report"))
        p.last_battle_report = battle_report_from_json(j.at("last_battle_report"));
    if (j.contains("knox")) {
        const json& k = j.at("knox");
        p.knox.id = k.value("id", "knox_ticonderoga");
        p.knox.status = k.value("status", "none");
        p.knox.start_hour = k.value("start_hour", int64_t{0});
        p.knox.ready_hour = k.value("ready_hour", int64_t{0});
        p.knox.pieces = k.value("pieces", 0);
        p.knox.transport_committed = k.value("transport_committed", 0);
        p.knox.fictionalized_authorization = k.value("fictionalized_authorization", false);
    }
    if (j.contains("commander")) {
        const json& c = j.at("commander");
        p.commander.id = c.value("id", "parker");
        p.commander.name = c.value("name", "Capt. Parker");
        p.commander.army_id = c.value("army_id", "continental_militia");
        p.commander.objective = c.value("objective", "preserve_force");
        p.commander.risk = c.value("risk", "cautious");
        p.commander.pending_clarification = c.value("pending_clarification", false);
        p.commander.last_request = c.value("last_request", "");
    }
    return p;
}
json faction_to_json(const FactionInfo& f) {
    return {{"display_name", f.display_name},
            {"envoy_name", f.envoy_name},
            {"envoy_title", f.envoy_title},
            {"allowed_clause_types", f.allowed_clause_types},
            {"protected_provinces", f.protected_provinces},
            {"max_ceasefire_days", f.max_ceasefire_days},
            {"max_safe_passage_days", f.max_safe_passage_days},
            {"controls_harbor", f.controls_harbor},
            {"envoy_loyalty", f.envoy_loyalty},
            {"envoy_defected", f.envoy_defected},
            {"envoy_title_granted", f.envoy_title_granted},
            {"grievance", f.grievance},
            {"snub_until_hour", f.snub_until_hour}};
}
FactionInfo faction_from_json(const json& j) {
    FactionInfo f;
    f.display_name = j.value("display_name", "");
    f.envoy_name = j.value("envoy_name", "");
    f.envoy_title = j.value("envoy_title", "");
    f.allowed_clause_types = j.value("allowed_clause_types", std::vector<std::string>{});
    f.protected_provinces = j.value("protected_provinces", std::vector<std::string>{});
    f.max_ceasefire_days = j.value("max_ceasefire_days", j.value("max_ceasefire_turns", 365));
    f.max_safe_passage_days = j.value("max_safe_passage_days", j.value("max_safe_passage_turns", 90));
    f.controls_harbor = j.value("controls_harbor", false);
    f.envoy_loyalty = j.value("envoy_loyalty", 0);
    f.envoy_defected = j.value("envoy_defected", false);
    f.envoy_title_granted = j.value("envoy_title_granted", "");
    f.grievance = j.value("grievance", 0);
    f.snub_until_hour = j.value("snub_until_hour", int64_t{0});
    return f;
}
json province_to_json(const Province& p) {
    return {{"id", p.id},
            {"name", p.name},
            {"owner", p.owner},
            {"map_index", p.map_index},
            {"terrain", p.terrain},
            {"adjacent", p.adjacent},
            {"polygon", pts_to_json(p.polygon)},
            {"label", json::array({p.label.x, p.label.y})},
            {"supply_value", p.supply_value},
            {"farm_level", p.farm_level},
            {"workshop_level", p.workshop_level},
            {"depot_level", p.depot_level},
            {"fort_level", p.fort_level},
            {"church", p.church},
            {"militia_muster", p.militia_muster},
            {"manpower_used", p.manpower_used}};
}
Province province_from_json(const json& j) {
    Province p;
    p.id = j.at("id").get<std::string>();
    p.name = j.value("name", p.id);
    p.owner = j.at("owner").get<std::string>();
    p.map_index = j.value("map_index", 0);
    p.terrain = j.value("terrain", std::vector<std::string>{});
    p.adjacent = j.value("adjacent", std::vector<std::string>{});
    std::sort(p.adjacent.begin(), p.adjacent.end());
    p.polygon = pts_from_json(j.value("polygon", json::array()));
    if (j.contains("label")) p.label = {j.at("label").at(0).get<double>(), j.at("label").at(1).get<double>()};
    p.supply_value = j.value("supply_value", 1);
    p.farm_level = j.value("farm_level", 0);
    p.workshop_level = j.value("workshop_level", 0);
    p.depot_level = j.value("depot_level", 0);
    p.fort_level = j.value("fort_level", 0);
    p.church = j.value("church", false);
    p.militia_muster = j.value("militia_muster", false);
    p.manpower_used = j.value("manpower_used", 0);
    return p;
}
Edge edge_from_json(const json& j) {
    Edge e;
    e.id = j.at("id").get<std::string>();
    e.a = j.at("a").get<std::string>();
    e.b = j.at("b").get<std::string>();
    e.type = j.at("type").get<std::string>();
    e.march_hours = j.value("march_hours", 1);
    e.can_dismantle = j.value("can_dismantle", false);
    e.note = j.value("note", "");
    return e;
}
json edge_to_json(const Edge& e) {
    json j{{"id", e.id}, {"a", e.a}, {"b", e.b}, {"type", e.type}, {"march_hours", e.march_hours}};
    if (e.can_dismantle) j["can_dismantle"] = true;
    if (!e.note.empty()) j["note"] = e.note;
    return j;
}
json command_to_json(const Command& c) {
    return {{"apply_on_hour", c.apply_on_hour},
            {"sequence", c.sequence},
            {"kind", static_cast<int>(c.payload.kind)},
            {"army_id", c.payload.army_id},
            {"dest_province", c.payload.dest_province},
            {"edge_path", c.payload.edge_path},
            {"result_json", c.payload.result_json}};
}
Command command_from_json(const json& j) {
    Command c;
    c.apply_on_hour = j.at("apply_on_hour").get<int64_t>();
    c.sequence = j.at("sequence").get<uint64_t>();
    c.payload.kind = static_cast<CommandKind>(j.at("kind").get<int>());
    c.payload.army_id = j.value("army_id", "");
    c.payload.dest_province = j.value("dest_province", "");
    c.payload.edge_path = j.value("edge_path", std::vector<std::string>{});
    c.payload.result_json = j.value("result_json", "");
    return c;
}

CampaignState state_from_json(const json& j, bool is_save) {
    CampaignState s;
    if (is_save) {
        const int ver = j.at("schema_version").get<int>();
        if (ver != kSchemaVersion)
            throw std::runtime_error("unsupported save version " + std::to_string(ver) +
                                     " (need " + std::to_string(kSchemaVersion) +
                                     "); turn-based v1 saves cannot be loaded");
    }
    s.scenario = j.value("scenario", "");
    s.seed = j.at("seed").get<uint64_t>();
    s.player_faction = j.value("player_faction", std::string(kContinental));
    s.ai_objective = j.value("ai_objective", "");
    s.march_hours_per_day = j.value("march_hours_per_day", kDefaultMarchHoursPerDay);
    if (j.contains("now")) {
        s.now.hour_index = j.at("now").at("hour_index").get<int64_t>();
    } else if (j.contains("start_ymdh")) {
        const json& y = j.at("start_ymdh");
        s.now = from_ymdh(YMDH{y.at("y").get<int32_t>(), static_cast<uint8_t>(y.at("m").get<int>()),
                               static_cast<uint8_t>(y.at("d").get<int>()),
                               static_cast<uint8_t>(y.value("h", 0))});
    } else if (!is_save) {
        s.now = from_ymdh(YMDH{1775, 4, 18, 20});
    }
    for (const auto& p : j.at("provinces")) {
        Province pr = province_from_json(p);
        s.provinces[pr.id] = pr;
    }
    if (j.contains("edges")) {
        for (const auto& e : j.at("edges")) {
            Edge ed = edge_from_json(e);
            s.edges[ed.id] = ed;
        }
    }
    rebuild_adjacency(s);
    for (const auto& a : j.at("armies")) {
        Army ar = army_from_json(a);
        s.armies[ar.id] = ar;
    }
    for (auto it = j.at("factions").begin(); it != j.at("factions").end(); ++it)
        s.factions[it.key()] = faction_from_json(it.value());
    s.supplies = j.at("supplies").get<std::map<std::string, int>>();
    s.relations = j.at("relations").get<std::map<std::string, int>>();
    s.prisoners_held = j.at("prisoners_held").get<std::map<std::string, int>>();
    s.player_gold = j.value("player_gold", 500);
    s.player_vassal = j.value("player_vassal", false);
    s.player_title = j.value("player_title", "");
    s.fiefs = j.value("fiefs", std::map<std::string, std::string>{});
    s.local_support = j.value("local_support", 55);
    s.pending_event_id = j.value("pending_event_id", "");
    if (j.contains("province_stores")) {
        for (auto it = j.at("province_stores").begin(); it != j.at("province_stores").end(); ++it)
            s.province_stores[it.key()] = province_stores_from_json(it.value());
    }
    if (j.contains("stores")) {
        for (auto it = j.at("stores").begin(); it != j.at("stores").end(); ++it)
            s.stores[it.key()] = stores_from_json(it.value());
    }
    if (j.contains("staff")) s.staff = staff_from_json(j.at("staff"));
    if (j.contains("progress")) s.progress = progress_from_json(j.at("progress"));
    if (j.contains("donor_reserves")) {
        for (auto it = j.at("donor_reserves").begin(); it != j.at("donor_reserves").end(); ++it)
            s.donor_reserves[it.key()] = donor_from_json(it.value());
    }
    if (j.contains("shipments"))
        for (const auto& sh : j.at("shipments")) s.shipments.push_back(shipment_from_json(sh));
    ensure_logistics_defaults(s);
    migrate_campaign_state(s);
    if (is_save) {
        for (const auto& t : j.at("treaties")) s.treaties.push_back(treaty_from_json(t));
        for (const auto& m : j.at("memory"))
            s.memory.push_back({m.at("hour_index").get<int64_t>(), m.at("faction").get<std::string>(),
                                m.at("text").get<std::string>()});
        const json& pb = j.at("pending_battle");
        s.pending_battle = {pb.at("active").get<bool>(), pb.at("province").get<std::string>(),
                            pb.at("attacker_army").get<std::string>(), pb.at("defender_army").get<std::string>()};
        s.log = j.at("log").get<std::vector<std::string>>();
        s.next_command_seq = j.value("next_command_seq", 1);
        if (j.contains("pending_commands"))
            for (const auto& c : j.at("pending_commands")) s.pending_commands.push_back(command_from_json(c));
        if (j.contains("resource_changes"))
            for (const auto& c : j.at("resource_changes")) s.resource_changes.push_back(resource_change_from_json(c));
    }
    for (const auto& kv : s.armies)
        if (!s.provinces.count(kv.second.province)) throw std::runtime_error("army in unknown province: " + kv.first);
    auto gerr = validate_scenario_graph(s);
    if (!gerr.empty() && !s.edges.empty()) throw std::runtime_error("scenario graph invalid: " + gerr.front());
    return s;
}

}  // namespace

std::string read_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

CampaignState load_scenario(const std::string& text) {
    try {
        return state_from_json(json::parse(text), false);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("scenario parse error: ") + e.what());
    }
}

std::string serialize(const CampaignState& s) {
    json j;
    j["schema_version"] = kSchemaVersion;
    j["scenario"] = s.scenario;
    j["now"] = {{"hour_index", s.now.hour_index}};
    j["seed"] = s.seed;
    j["player_faction"] = s.player_faction;
    j["ai_objective"] = s.ai_objective;
    j["march_hours_per_day"] = s.march_hours_per_day;
    j["provinces"] = json::array();
    for (const auto& kv : s.provinces) j["provinces"].push_back(province_to_json(kv.second));
    j["edges"] = json::array();
    for (const auto& kv : s.edges) j["edges"].push_back(edge_to_json(kv.second));
    j["armies"] = json::array();
    for (const auto& kv : s.armies) j["armies"].push_back(army_to_json(kv.second));
    j["factions"] = json::object();
    for (const auto& kv : s.factions) j["factions"][kv.first] = faction_to_json(kv.second);
    j["supplies"] = s.supplies;
    j["relations"] = s.relations;
    j["prisoners_held"] = s.prisoners_held;
    j["player_gold"] = s.player_gold;
    j["player_vassal"] = s.player_vassal;
    j["player_title"] = s.player_title;
    j["fiefs"] = s.fiefs;
    j["local_support"] = s.local_support;
    j["pending_event_id"] = s.pending_event_id;
    j["province_stores"] = json::object();
    for (const auto& kv : s.province_stores)
        j["province_stores"][kv.first] = province_stores_to_json(kv.second);
    j["stores"] = json::object();
    for (const auto& kv : s.stores) j["stores"][kv.first] = stores_to_json(kv.second);
    j["staff"] = staff_to_json(s.staff);
    j["progress"] = progress_to_json(s.progress);
    j["donor_reserves"] = json::object();
    for (const auto& kv : s.donor_reserves) j["donor_reserves"][kv.first] = donor_to_json(kv.second);
    j["shipments"] = json::array();
    for (const auto& sh : s.shipments) j["shipments"].push_back(shipment_to_json(sh));
    j["treaties"] = json::array();
    for (const auto& t : s.treaties) j["treaties"].push_back(treaty_to_json(t));
    j["memory"] = json::array();
    for (const auto& m : s.memory)
        j["memory"].push_back({{"hour_index", m.hour_index}, {"faction", m.faction}, {"text", m.text}});
    j["pending_battle"] = {{"active", s.pending_battle.active},
                           {"province", s.pending_battle.province},
                           {"attacker_army", s.pending_battle.attacker_army},
                           {"defender_army", s.pending_battle.defender_army}};
    j["pending_commands"] = json::array();
    for (const auto& c : s.pending_commands) j["pending_commands"].push_back(command_to_json(c));
    j["next_command_seq"] = s.next_command_seq;
    j["log"] = s.log;
    j["resource_changes"] = json::array();
    for (const auto& c : s.resource_changes) j["resource_changes"].push_back(resource_change_to_json(c));
    return j.dump();
}

CampaignState deserialize(const std::string& text) {
    try {
        return state_from_json(json::parse(text), true);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("save parse error: ") + e.what());
    }
}

uint64_t state_hash(const CampaignState& s) { return detail::fnv1a(serialize(s)); }

std::string state_hash_hex(const CampaignState& s) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(state_hash(s)));
    return buf;
}

// ---- Diplomacy bridges ---------------------------------------------------
std::string context_to_json(const DiplomacyContext& c) {
    json j;
    j["faction"] = c.faction;
    j["counterpart"] = c.counterpart;
    j["hour_index"] = c.hour_index;
    j["relation"] = c.relation;
    j["allowed_clause_types"] = c.allowed_clause_types;
    j["limits"] = {{"max_ceasefire_days", c.max_ceasefire_days},
                   {"max_safe_passage_days", c.max_safe_passage_days}};
    j["protected_provinces"] = c.protected_provinces;
    j["provinces"] = c.provinces;
    j["armies"] = c.armies;
    j["prisoners_held"] = c.prisoners_held;
    j["treaties"] = json::array();
    for (const auto& t : c.treaties) j["treaties"].push_back(treaty_to_json(t));
    j["memory"] = c.memory;
    j["envoy_loyalty"] = c.envoy_loyalty;
    j["envoy_defected"] = c.envoy_defected;
    j["player_gold"] = c.player_gold;
    j["defect_price"] = c.defect_price;
    j["titles"] = c.titles;
    j["army_provinces"] = c.army_provinces;
    j["army_strengths"] = c.army_strengths;
    j["adjacency"] = c.adjacency;
    j["grievance"] = c.grievance;
    j["snubbed_hours_left"] = c.snubbed_hours_left;
    j["mood"] = c.mood;
    j["player_vassal"] = c.player_vassal;
    return j.dump();
}

DiplomacyContext context_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        DiplomacyContext c;
        c.faction = j.at("faction").get<std::string>();
        c.counterpart = j.at("counterpart").get<std::string>();
        if (j.contains("hour_index"))
            c.hour_index = j.at("hour_index").get<int64_t>();
        else if (j.contains("turn"))
            c.hour_index = j.at("turn").get<int64_t>();  // legacy test vectors
        c.relation = j.at("relation").get<int>();
        c.allowed_clause_types = j.at("allowed_clause_types").get<std::vector<std::string>>();
        const json& lim = j.at("limits");
        c.max_ceasefire_days = lim.value("max_ceasefire_days", lim.value("max_ceasefire_turns", 365));
        c.max_safe_passage_days = lim.value("max_safe_passage_days", lim.value("max_safe_passage_turns", 90));
        c.protected_provinces = j.at("protected_provinces").get<std::vector<std::string>>();
        c.provinces = j.at("provinces").get<std::map<std::string, std::string>>();
        c.armies = j.at("armies").get<std::map<std::string, std::string>>();
        c.prisoners_held = j.at("prisoners_held").get<std::map<std::string, int>>();
        for (const auto& t : j.at("treaties")) {
            if (t.contains("hours_remaining"))
                c.treaties.push_back(treaty_from_json(t));
            else {
                Treaty tr;
                tr.type = t.at("type").get<std::string>();
                tr.hours_remaining = t.value("turns_remaining", 0) * 24;
                tr.army_id = t.value("army_id", "");
                tr.province_id = t.value("province_id", "");
                c.treaties.push_back(tr);
            }
        }
        c.memory = j.value("memory", std::vector<std::string>{});
        c.envoy_loyalty = j.value("envoy_loyalty", 0);
        c.envoy_defected = j.value("envoy_defected", false);
        c.player_gold = j.value("player_gold", 0);
        c.defect_price = j.value("defect_price", 0);
        c.titles = j.value("titles", std::map<std::string, int>{});
        c.army_provinces = j.value("army_provinces", std::map<std::string, std::string>{});
        c.army_strengths = j.value("army_strengths", std::map<std::string, int64_t>{});
        c.adjacency = j.value("adjacency", std::map<std::string, std::vector<std::string>>{});
        c.grievance = j.value("grievance", 0);
        c.snubbed_hours_left = j.value("snubbed_hours_left", 0);
        c.mood = j.value("mood", "calm");
        c.player_vassal = j.value("player_vassal", false);
        return c;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("context parse error: ") + e.what());
    }
}

namespace {

json clause_to_json(const Clause& c) {
    json j{{"type", c.type}};
    if (c.type == "ceasefire")
        j["days"] = c.days;
    else if (c.type == "prisoner_exchange")
        j["counts"] = c.counts;
    else if (c.type == "safe_passage")
        j.update({{"army_id", c.army_id}, {"province_id", c.province_id}, {"days", c.days}});
    else if (c.type == "cede_province")
        j["province_id"] = c.province_id;
    else if (c.type == "defect")
        j.update({{"title", c.title}, {"gold", c.gold}, {"provinces", c.provinces}});
    else if (c.type == "surrender")
        j.update({{"army_id", c.army_id}, {"days", c.days}});
    else if (c.type == "vassalage")
        j.update({{"title", c.title}, {"gold", c.gold}, {"provinces", c.provinces}});
    else if (c.type == "take_offense")
        j.update({{"reason", c.reason}, {"days", c.days}});
    return j;
}

bool int_field(const json& v, int& out) {
    constexpr int64_t kMin = std::numeric_limits<int>::min();
    constexpr int64_t kMax = std::numeric_limits<int>::max();
    if (v.is_number_unsigned()) {
        if (v.get<uint64_t>() > static_cast<uint64_t>(kMax)) return false;
        out = static_cast<int>(v.get<uint64_t>());
        return true;
    }
    if (!v.is_number_integer()) return false;
    const int64_t n = v.get<int64_t>();
    if (n < kMin || n > kMax) return false;
    out = static_cast<int>(n);
    return true;
}

}  // namespace

std::string proposal_to_json(const DiplomacyProposal& p) {
    json j{{"spoken_line", p.spoken_line}, {"clauses", json::array()}};
    for (const auto& c : p.clauses) j["clauses"].push_back(clause_to_json(c));
    return j.dump();
}

std::vector<std::string> proposal_from_json(const std::string& text, DiplomacyProposal& out) {
    using detail::err;
    std::vector<std::string> errors;
    out = DiplomacyProposal{};
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {err("malformed_proposal", "not a JSON object")};
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.key() != "spoken_line" && it.key() != "clauses") errors.push_back(err("unexpected_field", it.key()));
    if (j.contains("spoken_line")) {
        if (!j["spoken_line"].is_string())
            errors.push_back(err("malformed_proposal", "spoken_line"));
        else
            out.spoken_line = j["spoken_line"].get<std::string>();
    }
    if (!j.contains("clauses")) return errors;
    if (!j["clauses"].is_array()) {
        errors.push_back(err("malformed_proposal", "clauses must be an array"));
        return errors;
    }
    for (const auto& cj : j["clauses"]) {
        Clause c;
        if (!cj.is_object() || !cj.contains("type") || !cj["type"].is_string()) {
            errors.push_back(err("malformed_clause", "missing type"));
            continue;
        }
        c.type = cj["type"].get<std::string>();
        if (cj.contains("turns")) {
            errors.push_back(err("turns_not_allowed", "use days instead of turns"));
            continue;
        }
        static const std::map<std::string, std::vector<std::string>> kFields = {
            {"ceasefire", {"type", "days"}},
            {"prisoner_exchange", {"type", "counts"}},
            {"safe_passage", {"type", "army_id", "province_id", "days"}},
            {"cede_province", {"type", "province_id"}},
            {"defect", {"type", "title", "gold", "provinces"}},
            {"surrender", {"type", "army_id", "days"}},
            {"vassalage", {"type", "title", "gold", "provinces"}},
            {"take_offense", {"type", "reason", "days"}}};
        auto kf = kFields.find(c.type);
        for (auto it = cj.begin(); it != cj.end(); ++it) {
            const std::string& k = it.key();
            if (kf != kFields.end() && !detail::contains(kf->second, k)) {
                errors.push_back(err("unexpected_field", c.type + "." + k));
                continue;
            }
            if (k == "days") {
                if (!int_field(it.value(), c.days)) errors.push_back(err("malformed_clause", "days"));
            } else if (k == "army_id" || k == "province_id") {
                if (!it.value().is_string())
                    errors.push_back(err("malformed_clause", k));
                else
                    (k == "army_id" ? c.army_id : c.province_id) = it.value().get<std::string>();
            } else if (k == "reason") {
                if (!it.value().is_string())
                    errors.push_back(err("malformed_clause", "reason"));
                else
                    c.reason = it.value().get<std::string>();
            } else if (k == "title") {
                if (!it.value().is_string())
                    errors.push_back(err("malformed_clause", "title"));
                else
                    c.title = it.value().get<std::string>();
            } else if (k == "gold") {
                if (!int_field(it.value(), c.gold)) errors.push_back(err("malformed_clause", "gold"));
            } else if (k == "provinces") {
                if (!it.value().is_array()) {
                    errors.push_back(err("malformed_clause", "provinces"));
                    continue;
                }
                for (const auto& pv : it.value()) {
                    if (!pv.is_string())
                        errors.push_back(err("malformed_clause", "provinces"));
                    else
                        c.provinces.push_back(pv.get<std::string>());
                }
            } else if (k == "counts") {
                c.has_counts = true;
                if (!it.value().is_object()) {
                    errors.push_back(err("malformed_clause", "counts"));
                    continue;
                }
                for (auto ci = it.value().begin(); ci != it.value().end(); ++ci) {
                    int n = 0;
                    if (!int_field(ci.value(), n))
                        errors.push_back(err("malformed_clause", "counts." + ci.key()));
                    else
                        c.counts[ci.key()] = n;
                }
            }
        }
        out.clauses.push_back(c);
    }
    return errors;
}

// ---- Battle bridges ------------------------------------------------------
std::string setup_to_json(const BattleSetup& b) {
    json j;
    j["valid"] = b.valid;
    j["province_id"] = b.province_id;
    j["province_name"] = b.province_name;
    j["objective"] = b.objective;
    j["scenario_preset"] = b.scenario_preset;
    j["seed"] = b.seed;
    j["attacker_faction"] = b.attacker_faction;
    j["defender_faction"] = b.defender_faction;
    j["player_faction"] = b.player_faction;
    j["terrain_tags"] = b.terrain_tags;
    j["province_cartridges"] = b.province_cartridges;
    j["province_powder"] = b.province_powder;
    j["force_notes"] = b.force_notes;
    j["farm_level"] = b.farm_level;
    j["workshop_level"] = b.workshop_level;
    j["depot_level"] = b.depot_level;
    j["fort_level"] = b.fort_level;
    j["church"] = b.church;
    j["militia_muster"] = b.militia_muster;
    j["initial_post_holders"] = b.initial_post_holders;
    j["modifier_notes"] = b.modifier_notes;
    j["sides"] = json::object();
    for (const auto& kv : b.sides)
        j["sides"][kv.first] = {{"faction", kv.second.faction},
                                {"army_id", kv.second.army_id},
                                {"tickets", kv.second.tickets},
                                {"morale", kv.second.morale},
                                {"readiness", kv.second.readiness},
                                {"cartridge_budget", kv.second.cartridge_budget},
                                {"reinforcement_scale", kv.second.reinforcement_scale},
                                {"retreat_province", kv.second.retreat_province}};
    return j.dump();
}

BattleSetup setup_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        BattleSetup b;
        b.valid = j.at("valid").get<bool>();
        b.province_id = j.at("province_id").get<std::string>();
        b.province_name = j.at("province_name").get<std::string>();
        b.objective = j.at("objective").get<std::string>();
        b.scenario_preset = j.value("scenario_preset", scenario_preset_for_province(b.province_id));
        b.seed = j.at("seed").get<uint64_t>();
        b.attacker_faction = j.at("attacker_faction").get<std::string>();
        b.defender_faction = j.at("defender_faction").get<std::string>();
        b.player_faction = j.at("player_faction").get<std::string>();
        b.terrain_tags = j.value("terrain_tags", std::vector<std::string>{});
        b.province_cartridges = j.value("province_cartridges", 0);
        b.province_powder = j.value("province_powder", 0);
        b.force_notes = j.value("force_notes", std::string("sourced approximation — not a precise muster roll"));
        b.farm_level = j.value("farm_level", 0);
        b.workshop_level = j.value("workshop_level", 0);
        b.depot_level = j.value("depot_level", 0);
        b.fort_level = j.value("fort_level", 0);
        b.church = j.value("church", false);
        b.militia_muster = j.value("militia_muster", false);
        b.initial_post_holders = j.value("initial_post_holders", std::map<std::string, std::string>{});
        b.modifier_notes = j.value("modifier_notes", std::vector<std::string>{});
        for (auto it = j.at("sides").begin(); it != j.at("sides").end(); ++it) {
            SideSetup s;
            s.faction = it.value().at("faction").get<std::string>();
            s.army_id = it.value().at("army_id").get<std::string>();
            s.tickets = it.value().at("tickets").get<std::map<std::string, int>>();
            s.morale = it.value().at("morale").get<int>();
            s.readiness = it.value().value("readiness", 100);
            s.cartridge_budget = it.value().value("cartridge_budget", b.province_cartridges);
            s.reinforcement_scale = it.value().value("reinforcement_scale", 1.f);
            s.retreat_province = it.value().at("retreat_province").get<std::string>();
            b.sides[it.key()] = s;
        }
        return b;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("setup parse error: ") + e.what());
    }
}

std::string result_to_json(const BattleResult& r) {
    json j{{"province_id", r.province_id},
           {"winner", r.winner},
           {"objective_holder", r.objective_holder},
           {"objectives_held", r.objectives_held},
           {"losses", r.losses},
           {"wounded", r.wounded},
           {"cartridges_spent", r.cartridges_spent},
           {"powder_spent", r.powder_spent},
           {"morale_delta", r.morale_delta},
           {"shots_fired", r.shots_fired},
           {"personal_kills", r.personal_kills},
           {"province_owner_after", r.province_owner_after},
           {"retreat_province", r.retreat_province}};
    return j.dump();
}

std::string report_to_json(const BattleReport& r) { return battle_report_to_json(r).dump(); }

BattleResult result_from_json(const std::string& text) {
    try {
        json j = json::parse(text);
        BattleResult r;
        r.province_id = j.at("province_id").get<std::string>();
        r.winner = j.at("winner").get<std::string>();
        r.objective_holder = j.value("objective_holder", "");
        r.objectives_held = j.value("objectives_held", std::map<std::string, std::string>{});
        r.losses = j.value("losses", std::map<std::string, std::map<std::string, int>>{});
        r.wounded = j.value("wounded", std::map<std::string, std::map<std::string, int>>{});
        r.cartridges_spent = j.value("cartridges_spent", std::map<std::string, int>{});
        r.powder_spent = j.value("powder_spent", std::map<std::string, int>{});
        r.morale_delta = j.value("morale_delta", std::map<std::string, int>{});
        r.shots_fired = j.value("shots_fired", std::map<std::string, int>{});
        r.personal_kills = j.value("personal_kills", 0);
        r.province_owner_after = j.value("province_owner_after", "");
        r.retreat_province = j.value("retreat_province", "");
        return r;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("result parse error: ") + e.what());
    }
}

}  // namespace pa250
