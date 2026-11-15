// Cases from the playtest of 2026-11-15 (the user's report): a battle won at Boston that took
// nothing and said nothing, a march to Charlestown that did nothing, and missions that ended with
// no word and no reward. Included by sim_cases.hpp inside namespace pa250_tests, after pass_cases.hpp.

static bool line_has(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines)
        if (l.find(part) != std::string::npos) return true;
    return false;
}

static void test_playtest_mission_results() {
    CampaignPassData data;
    CampaignState s = fresh();
    const int gold0 = s.player_gold, support0 = s.local_support;
    CHECK(s.progress.mission_results.empty());
    CHECK(serialize(s).find("mission_results") == std::string::npos);  // a state with none has no such key

    // The first mission done: it pays, and the result is kept for the map's card.
    CampaignState won = apply_mission_effect(s, "p1_save_the_stores", "complete");
    CHECK(won.progress.mission_results.size() == 1);
    if (!won.progress.mission_results.empty()) {
        const MissionResult& r = won.progress.mission_results.back();
        CHECK(r.mission_id == "p1_save_the_stores" && r.title == "Save the stores" && r.outcome == "complete");
        CHECK(r.hour_index == s.now.hour_index);
        CHECK(r.gold == 25 && won.player_gold == gold0 + 25);
        CHECK(r.local_support == won.local_support - support0 && r.local_support > 0);
        CHECK(r.army_morale == 3);
        CHECK(r.summary.find("+25 gold") != std::string::npos);
        CHECK(raw_ids_in(won, r.summary).empty() && raw_ids_in(won, r.title).empty());
    }
    // Every Patriot mission that can be done pays something a player can count.
    for (const MissionDef& m : mission_defs()) {
        if (m.faction != kContinental) continue;
        const MissionEffects& e = m.reward;
        const bool pays = e.gold > 0 || e.local_support > 0 || e.army_morale > 0 || e.army_readiness > 0 || e.start_evacuation;
        if (!pays) g_reporter()->message("mission with no reward: " + m.id);
        CHECK(pays);
        CHECK(!e.summary.empty());
    }
    // A mission lost says what it cost.
    CampaignState lost = apply_mission_effect(s, "p1_save_the_stores", "failed");
    CHECK(lost.progress.mission_results.size() == 1);
    if (!lost.progress.mission_results.empty()) {
        const MissionResult& r = lost.progress.mission_results.back();
        CHECK(r.outcome == "failed" && r.gold == 0 && r.local_support == lost.local_support - support0 && r.local_support < 0);
    }
    // The enemy's missions are his own business: no card for them.
    CampaignState theirs = apply_mission_effect(s, "b1_seize_the_stores", "complete");
    CHECK(theirs.progress.mission_results.empty());
    // Support already at the top: the card shows what was really gained, not what the file offers.
    {
        CampaignState full = s;
        full.local_support = 100;
        const CampaignState after = apply_mission_effect(full, "p1_save_the_stores", "complete");
        CHECK(after.progress.mission_results.back().local_support == 0);
    }
    // Saved and loaded; two missions keep their order.
    CampaignState two = apply_mission_effect(won, "p3_deny_the_islands", "failed");
    CHECK(two.progress.mission_results.size() == 2);
    const CampaignState back = deserialize(serialize(two));
    CHECK(back.progress.mission_results.size() == 2);
    if (back.progress.mission_results.size() == 2) {
        CHECK(back.progress.mission_results[0].mission_id == "p1_save_the_stores" && back.progress.mission_results[0].gold == 25);
        CHECK(back.progress.mission_results[1].outcome == "failed");
    }
    CHECK(state_hash(back) == state_hash(two));
    // Through the clock: the mission is done by the player's own act and the result arrives with it.
    {
        std::string err;
        CampaignState c = send_stores_to_depot(fresh(), "concord", err);
        CHECK(err.empty());
        for (int h = 0; h < 6 && c.progress.mission_results.empty(); ++h) {
            c = advance_hour(c).state;
            if (c.pending_battle.active) c.pending_battle = PendingBattle{};
        }
        CHECK(c.progress.mission_results.size() == 1);
        if (!c.progress.mission_results.empty()) CHECK(c.progress.mission_results.back().outcome == "complete");
    }
}

static void test_playtest_no_general_is_explained() {
    CampaignPassData data;
    // 18 April, before any general has come in.
    {
        const CampaignState s = fresh();
        const std::string how = how_to_form_army(s, kContinental);
        CHECK(how.find("No general has come in yet") != std::string::npos);
        CHECK(how.find("Charlestown") != std::string::npos);  // the one Patriot town with works at the opening
        CHECK(how.find("People") != std::string::npos);
        CHECK(raw_ids_in(s, how).empty());
    }
    // Since the playtest the militia opens under Colonel Barrett (user decision 2026-11-15): the same
    // attack on Boston now sits down to a siege. What a detachment is told is checked with him relieved.
    {
        CampaignState b = pass_campaign_decided(YMDH{1775, 4, 25, 8});
        CHECK(general_of(b, "continental_militia") == "james_barrett");
        b.treaties.clear();
        b.armies.at("continental_militia").province = "boston";
        b.armies.at("continental_militia").previous_province = "roxbury";
        b.armies.at("british_regulars").province = "chelsea";
        b.pending_battle = {true, "boston", "continental_militia", "british_garrison"};
        CHECK(why_cannot_besiege(b, "continental_militia", "boston").empty());
        const CampaignState after = apply_battle_result(b, ended("boston", kContinental, "posts_swept"));
        CHECK(after.provinces.at("boston").owner == kBritish && after.provinces.at("boston").siege_by == kContinental);
        CHECK(after.progress.last_battle_report.ownership_reason != "no_general_no_siege");
    }
    CampaignState s = relieved(pass_campaign_decided(YMDH{1775, 4, 25, 8}));
    CHECK(general_of(s, "continental_militia").empty());
    {
        const std::string how = how_to_form_army(s, kContinental);
        CHECK(how.find("is at hand") != std::string::npos);
        CHECK(raw_ids_in(s, how).empty());
    }
    // The playtest: the militia, a detachment, comes at Boston by the neck and carries every post.
    {
        CampaignState b = s;
        b.treaties.clear();
        b.armies.at("continental_militia").province = "boston";
        b.armies.at("continental_militia").previous_province = "roxbury";
        b.armies.at("british_regulars").province = "chelsea";
        b.pending_battle = {true, "boston", "continental_militia", "british_garrison"};
        CHECK(!why_cannot_besiege(b, "continental_militia", "boston").empty());
        const CampaignState after = apply_battle_result(b, ended("boston", kContinental, "posts_swept"));
        const BattleReport& r = after.progress.last_battle_report;
        CHECK(after.provinces.at("boston").owner == kBritish && after.provinces.at("boston").siege_by.empty());
        CHECK(r.ownership_reason == "no_general_no_siege");
        // It is said, in the map changes and in the reasons, with what to do about it.
        CHECK(line_has(r.map_changes, "must be besieged") && line_has(r.map_changes, "no general"));
        CHECK(line_has(r.map_changes, "People"));
        CHECK(line_has(r.change_reasons, "no general to conduct a siege"));
        CHECK(!line_has(r.change_reasons, "Ownership unchanged"));
        for (const auto& l : r.map_changes) {
            if (!raw_ids_in(after, l).empty()) g_reporter()->message("raw id in: " + l);
            CHECK(raw_ids_in(after, l).empty());
        }
        for (const auto& l : r.change_reasons) CHECK(raw_ids_in(after, l).empty());
    }
    // The other half of the playtest: Charlestown in British hands, the militia marched into it alone.
    {
        CampaignState c = s;
        c.treaties.clear();
        c.provinces.at("charlestown").owner = kBritish;
        c.armies.at("continental_militia").province = "charlestown";
        for (auto& ak : c.armies)
            if (ak.second.faction == kBritish) ak.second.province = "boston";
        const std::string why = why_cannot_besiege(c, "continental_militia", "charlestown");
        CHECK(why.find("no general") != std::string::npos && why.find("Charlestown") != std::string::npos && why.find("Massachusetts Militia") != std::string::npos);
        CHECK(raw_ids_in(c, why).empty());
        // With Charlestown gone the Patriots hold no town with works: the advice says to build a muster.
        CHECK(how_to_form_army(c, kContinental).find("build a Militia Muster") != std::string::npos);
        c.ai_objective.clear();
        CampaignState stood = c;
        for (int h = 0; h < 48; ++h) {
            stood = advance_hour(stood).state;
            if (stood.pending_battle.active) break;
        }
        if (!stood.pending_battle.active) CHECK(stood.provinces.at("charlestown").siege_by.empty());  // two days there: nothing, as the player saw
        // Nothing to explain where there is nothing to besiege, or on his own ground.
        CHECK(why_cannot_besiege(c, "continental_militia", "lexington").empty());
        CampaignState open_town = c;
        open_town.provinces.at("medford").owner = kBritish;
        CHECK(why_cannot_besiege(open_town, "continental_militia", "medford").empty());
        CHECK(why_cannot_besiege(c, "no_such_army", "charlestown").empty() && why_cannot_besiege(c, "continental_militia", "nowhere").empty());
    }
    // Under a general the same two things work: the won battle begins the siege, and so does standing there.
    {
        CampaignState g = s;
        g.treaties.clear();
        g.armies.at("continental_militia").province = "charlestown";
        g = pass_order(g, order_json({{"order", "form_army"}, {"person", "william_heath"}, {"province", "charlestown"}}));
        CHECK(!general_of(g, "continental_militia").empty());
        CampaignState b = g;
        b.armies.at("continental_militia").province = "boston";
        b.armies.at("continental_militia").previous_province = "roxbury";
        b.armies.at("british_regulars").province = "chelsea";
        b.pending_battle = {true, "boston", "continental_militia", "british_garrison"};
        CHECK(why_cannot_besiege(b, "continental_militia", "boston").empty());
        const CampaignState after = apply_battle_result(b, ended("boston", kContinental, "posts_swept"));
        CHECK(after.progress.last_battle_report.ownership_reason == "siege_begun");
        CHECK(after.provinces.at("boston").siege_by == kContinental && after.provinces.at("boston").siege_hours_remaining == 144);
        CHECK(line_has(after.progress.last_battle_report.map_changes, "is besieged"));
    }
    // Without the people file there are no generals and no such rule: nothing to explain.
    unload_persons();
    CHECK(how_to_form_army(s, kContinental).empty());
    CHECK(why_cannot_besiege(s, "continental_militia", "boston").empty());
    load_persons(read_text_file(g_data_dir() + "/persons_boston_1775.json"));
}
