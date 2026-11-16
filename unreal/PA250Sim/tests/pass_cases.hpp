// Campaign pass cases: people (generals, agents, organizers), lobbying (measures, dispatches,
// favors, Unity, the ministry's confidence), decisions. Included by sim_cases.hpp inside
// namespace pa250_tests, after its helpers. Shared vectors: protocol/test_vectors/campaign.

// The arc and every campaign-pass data file, loaded for one case and unloaded after it.
struct CampaignPassData {
    CampaignArc arc;
    CampaignPassData() {
        const auto pe = load_persons(read_text_file(g_data_dir() + "/persons_boston_1775.json"));
        const auto po = load_politics(read_text_file(g_data_dir() + "/bodies_boston_1775.json"),
                                      read_text_file(g_data_dir() + "/measures_boston_1775.json"));
        const auto de = load_decisions(read_text_file(g_data_dir() + "/decisions_boston_1775.json"));
        for (const auto& e : pe) g_reporter()->message("persons: " + e);
        for (const auto& e : po) g_reporter()->message("politics: " + e);
        for (const auto& e : de) g_reporter()->message("decisions: " + e);
        CHECK(pe.empty() && persons_loaded());
        CHECK(po.empty() && politics_loaded());
        CHECK(de.empty() && decisions_loaded());
    }
    ~CampaignPassData() {
        unload_persons();
        unload_politics();
        unload_decisions();
    }
};

static int pass_units(const Army& a) {
    int n = 0;
    for (const auto& kv : a.units) n += kv.second;
    return n;
}

static std::string order_json(std::initializer_list<std::pair<const char*, std::string>> fields) {
    json j = json::object();
    for (const auto& f : fields) j[f.first] = f.second;
    return j.dump();
}

// The opening militia has Colonel Barrett from the first hour (user decision 2026-11-15). A case
// about a detachment relieves him first, as the player would: the order, not a poke at the state.
static CampaignState relieved(const CampaignState& s, const std::string& person = "james_barrett") {
    std::string error;
    CampaignState after = apply_campaign_order(s, json{{"order", "relieve_person"}, {"person", person}}.dump(), error);
    if (!error.empty()) g_reporter()->message("could not relieve " + person + ": " + error);
    CHECK(error.empty());
    return after;
}

// For a case that places its own generals while a battle is pending (when no one can be relieved by
// order): the opening commanders stand down, by hand.
static void stand_down_generals(CampaignState& s) {
    for (auto& pk : s.pass.persons)
        if (pk.second.role == "general") {
            pk.second.role.clear();
            pk.second.army_id.clear();
        }
}

// A quiet campaign at a date, seeded (starting roles, leans, the dispatch at sea) by one hour's tick.
static CampaignState pass_campaign(YMDH at) {
    CampaignState s = quiet_campaign();
    s.ai_objective.clear();
    s.now = GameTime{from_ymdh(at).hour_index - 1};
    s = advance_hour(s).state;
    return s;
}

// The same, with every decision taken as answered already: for cases that are not about decisions
// (a pending decision stops the clock).
static CampaignState pass_campaign_decided(YMDH at) {
    CampaignState s = quiet_campaign();
    s.ai_objective.clear();
    for (const char* id : {"which_heights", "canada_expedition", "reenlist_how", "assault_or_guns", "take_the_heights_how", "burn_charlestown", "hold_or_sail"})
        s.pass.decisions[id] = "";
    s.progress.enlistment_warned = s.progress.enlistments_expired = true;
    s.now = GameTime{from_ymdh(at).hour_index - 1};
    s = advance_hour(s).state;
    return s;
}

static CampaignState pass_order(const CampaignState& s, const std::string& order, std::string* error_out = nullptr) {
    std::string error;
    CampaignState after = apply_campaign_order(s, order, error);
    if (error_out) *error_out = error;
    else if (!error.empty()) {
        g_reporter()->message("order refused: " + error + " " + order);
        CHECK(false);
    }
    return after;
}

static std::vector<std::string> campaign_vectors(const std::string& prefix) {
    const std::string dir = g_vector_dir() + "/../campaign";
    std::vector<std::string> files;
    for (const std::string& n : dir_file_names(dir)) {
        if (n.size() > 5 && n.substr(n.size() - 5) == ".json" && n.rfind(prefix, 0) == 0) files.push_back(dir + "/" + n);
    }
    std::sort(files.begin(), files.end());
    return files;
}

static void check_persons_expect(const CampaignState& s, const json& expect, const std::string& name) {
    if (!expect.contains("persons")) return;
    for (auto it = expect.at("persons").begin(); it != expect.at("persons").end(); ++it) {
        const PersonState st = s.pass.persons.count(it.key()) ? s.pass.persons.at(it.key()) : PersonState{};
        for (auto f = it.value().begin(); f != it.value().end(); ++f) {
            const std::string want = f.value().get<std::string>();
            const std::string have = f.key() == "role" ? st.role
                                     : f.key() == "army_id" ? st.army_id
                                     : f.key() == "province" ? st.province
                                     : f.key() == "bloc" ? st.bloc
                                     : st.status;
            if (have != want) g_reporter()->message(name + ": " + it.key() + "." + f.key() + " is '" + have + "', wanted '" + want + "'");
            CHECK(have == want);
        }
    }
}

// Orders and decisions from the shared vectors: accepted ones through the command queue as well as
// the pure function, refused ones with their code and the state untouched.
static void test_pass_order_vectors() {
    CampaignPassData data;
    std::vector<std::string> files = campaign_vectors("order_");
    for (const std::string& f : campaign_vectors("reject_")) files.push_back(f);
    CHECK(files.size() >= 20);
    for (const std::string& path : files) {
        const json v = json::parse(read_text_file(path));
        const std::string name = v.at("name").get<std::string>();
        if (name.rfind("reject_halt", 0) == 0) continue;  // halt_orders runs those
        const auto& t = v.at("start_ymdh");
        const YMDH at{t.at("y").get<int>(), static_cast<uint8_t>(t.at("m").get<int>()), static_cast<uint8_t>(t.at("d").get<int>()),
                      static_cast<uint8_t>(t.value("h", 0))};
        CampaignState s = quiet_campaign();
        s.ai_objective.clear();
        s.progress.enlistment_warned = s.progress.enlistments_expired = true;  // a vector's date is not about enlistments
        if (v.contains("setup") && v.at("setup").contains("decisions_taken"))
            s.pass.decisions = v.at("setup").at("decisions_taken").get<std::map<std::string, std::string>>();
        s.now = GameTime{from_ymdh(at).hour_index - 1};
        s = advance_hour(s).state;
        if (v.contains("setup")) {
            const json& su = v.at("setup");
            if (su.contains("armies"))
                for (auto it = su.at("armies").begin(); it != su.at("armies").end(); ++it) {
                    s.armies.at(it.key()).province = it.value().at("province").get<std::string>();
                    s.armies.at(it.key()).previous_province = s.armies.at(it.key()).province;
                }
            // Optional new stacks for transfer_units (id, province, units, optional role).
            if (su.contains("add_armies"))
                for (const auto& row : su.at("add_armies")) {
                    Army a;
                    a.id = row.at("id").get<std::string>();
                    a.name = row.value("name", a.id);
                    a.faction = row.value("faction", std::string(kContinental));
                    a.province = row.at("province").get<std::string>();
                    a.previous_province = a.province;
                    a.role = row.value("role", std::string());
                    a.morale = row.value("morale", 60);
                    if (row.contains("units"))
                        a.units = row.at("units").get<std::map<std::string, int>>();
                    if (row.contains("allied"))
                        a.allied = row.at("allied").get<std::map<std::string, int>>();
                    s.armies[a.id] = a;
                }
            if (su.contains("relieved"))
                for (const auto& who : su.at("relieved")) s = relieved(s, who.get<std::string>());
            if (su.contains("favors")) s.pass.favors = su.at("favors").get<int>();
            if (su.contains("gold")) s.player_gold = su.at("gold").get<int>();
            if (su.contains("pending_decision")) {
                // The decision as the hourly rule puts it (the date in the vector is its day).
                CHECK(s.pass.pending_decision == su.at("pending_decision").get<std::string>());
            }
        }
        const std::string order = v.at("order").dump();
        if (v.contains("expect_error")) {
            const std::string want = v.at("expect_error").get<std::string>();
            const std::string code = validate_campaign_order(s, order);
            if (code != want) g_reporter()->message(name + ": refused with '" + code + "', wanted '" + want + "'");
            CHECK(code == want);
            std::string error;
            const CampaignState same = apply_campaign_order(s, order, error);
            CHECK(error == want);
            CHECK(state_hash(same) == state_hash(s));
            // The controller refuses it too, and queues nothing.
            continue;
        }
        const std::string code = validate_campaign_order(s, order);
        if (!code.empty()) g_reporter()->message(name + ": refused with '" + code + "'");
        CHECK(code.empty());
        const CampaignState after = pass_order(s, order);
        const json& ex = v.at("expect");
        check_persons_expect(after, ex, name);
        if (ex.contains("general_of"))
            for (auto it = ex.at("general_of").begin(); it != ex.at("general_of").end(); ++it)
                CHECK(general_of(after, it.key()) == it.value().get<std::string>());
        if (ex.contains("unit_cap"))
            for (auto it = ex.at("unit_cap").begin(); it != ex.at("unit_cap").end(); ++it)
                CHECK(army_unit_cap(after, it.key()) == it.value().get<int>());
        if (ex.contains("can_besiege"))
            for (auto it = ex.at("can_besiege").begin(); it != ex.at("can_besiege").end(); ++it)
                CHECK(!general_of(after, it.key()).empty() == it.value().get<bool>());
        if (ex.contains("intel_known"))
            for (auto it = ex.at("intel_known").begin(); it != ex.at("intel_known").end(); ++it) {
                CHECK(json::parse(army_intel_json(s, kContinental, it.key())).at("known").get<bool>() == false);
                CHECK(json::parse(army_intel_json(after, kContinental, it.key())).at("known").get<bool>() == it.value().get<bool>());
            }
        if (ex.contains("uses_left"))
            for (auto it = ex.at("uses_left").begin(); it != ex.at("uses_left").end(); ++it)
                CHECK(after.pass.persons.at(it.key()).uses_left == it.value().get<int>());
        if (ex.contains("town_factor")) {
            bool found = false;
            for (const TownFactor& tf : after.pass.town_factors)
                found = found || (tf.province == ex.at("town_factor").at("province").get<std::string>() &&
                                  tf.label == ex.at("town_factor").at("label").get<std::string>() &&
                                  tf.value == ex.at("town_factor").at("value").get<int>() && tf.faction == kContinental);
            CHECK(found);
        }
        if (ex.contains("path_ok")) {
            const std::string army = ex.at("path_ok").at("army").get<std::string>();
            const std::string to = ex.at("path_ok").at("to").get<std::string>();
            CHECK(!plan_path(s, army, to).ok || plan_path(s, army, to).total_march_hours > 1);  // no ferry before
            const PathPlan with_boats = plan_path(after, army, to);
            CHECK(with_boats.ok && with_boats.edges.size() == 1 && after.edges.at(with_boats.edges[0]).type == "ferry");
        }
        if (ex.contains("bloc_lean"))
            for (auto it = ex.at("bloc_lean").begin(); it != ex.at("bloc_lean").end(); ++it)
                CHECK(after.pass.bloc_lean.at(it.key()) == it.value().get<int>());
        if (ex.contains("clerk_slots_used"))
            CHECK(after.staff.letters_processed_today - s.staff.letters_processed_today == ex.at("clerk_slots_used").get<int>());
        if (ex.contains("favors")) CHECK(after.pass.favors == ex.at("favors").get<int>());
        if (ex.contains("decision"))
            for (auto it = ex.at("decision").begin(); it != ex.at("decision").end(); ++it)
                CHECK(after.pass.decisions.at(it.key()) == it.value().get<std::string>());
        if (ex.contains("fort_level"))
            for (auto it = ex.at("fort_level").begin(); it != ex.at("fort_level").end(); ++it)
                CHECK(after.provinces.at(it.key()).fort_level - s.provinces.at(it.key()).fort_level == it.value().get<int>());
        if (ex.contains("gold")) CHECK(after.player_gold == ex.at("gold").get<int>());
        if (ex.contains("pending_decision")) CHECK(after.pass.pending_decision == ex.at("pending_decision").get<std::string>());
        if (ex.contains("delayed")) CHECK(static_cast<int>(after.pass.delayed.size()) - static_cast<int>(s.pass.delayed.size()) == ex.at("delayed").get<int>());
        if (ex.contains("away_days"))
            for (auto it = ex.at("away_days").begin(); it != ex.at("away_days").end(); ++it)
                CHECK(after.pass.persons.at(it.key()).away_until_hour == after.now.hour_index + 24LL * it.value().get<int>());
        if (ex.contains("army_gone"))
            for (const auto& id : ex.at("army_gone")) CHECK(after.armies.count(id.get<std::string>()) == 0);
        if (ex.contains("army_units"))
            for (auto it = ex.at("army_units").begin(); it != ex.at("army_units").end(); ++it) {
                int n = 0;
                for (const auto& u : after.armies.at(it.key()).units) n += u.second;
                CHECK(n == it.value().get<int>());
            }
        // The same order as a command: queued now, applied on the next hour (or, with the clock
        // stopped for a decision, at this boundary), and the result is the pure function's.
        {
            CampaignController c;
            c.new_campaign(read_text_file(g_data_dir() + "/boston1775.json"));
            std::string err;
            const std::string tmp = std::filesystem::temp_directory_path().string() + "/pa250_pass_vector.json";
            CHECK(write_text_file(tmp, serialize(s)));
            CHECK(c.load(tmp, err));
            const uint64_t before = state_hash(c.state());
            CHECK(c.issue_order(order).empty());
            if (s.pass.pending_decision.empty()) {
                CHECK(state_hash(c.state()) != before);  // queued
                CHECK(c.state().pending_commands.back().payload.kind == CommandKind::CampaignOrder);
                c.advance_one_hour();
            }
            check_persons_expect(c.state(), ex, name + " (command)");
            if (ex.contains("decision"))
                for (auto it = ex.at("decision").begin(); it != ex.at("decision").end(); ++it)
                    CHECK(c.state().pass.decisions.count(it.key()) && c.state().pass.decisions.at(it.key()) == it.value().get<std::string>());
            // Save and load keep it.
            const CampaignState loaded = deserialize(serialize(c.state()));
            CHECK(state_hash(loaded) == state_hash(c.state()));
        }
    }
}

// Battle documents with a general: the setup names him and carries his traits as rows; the result
// says what became of him.
static void test_pass_commander_vectors() {
    CampaignPassData data;
    ensure_battle_rules();
    const std::vector<std::string> files = campaign_vectors("battle_");
    CHECK(files.size() >= 17);
    for (const std::string& path : files) {
        const json v = json::parse(read_text_file(path));
        const std::string name = v.at("name").get<std::string>();
        const json& b = v.at("battle");
        CampaignState s = battle_at(b.at("province").get<std::string>(), b.at("attacker").get<std::string>(), b.at("defender").get<std::string>());
        // The vector says who commands: nobody else does.
        stand_down_generals(s);
        for (auto it = v.at("generals").begin(); it != v.at("generals").end(); ++it) {
            PersonState& st = s.pass.persons[it.value().get<std::string>()];
            st.role = "general";
            st.army_id = it.key();
        }
        s.pass.seeded = true;
        if (v.contains("at_ymdh")) s.now = from_ymdh(ymdh_of(v.at("at_ymdh")));
        const BattleSetup setup = build_battle_setup(s, s.pending_battle.province);
        if (v.contains("expect_setup")) {
            const json& ex = v.at("expect_setup");
            for (auto it = ex.at("commander").begin(); it != ex.at("commander").end(); ++it) {
                const SideSetup& side = setup.sides.at(it.key());
                CHECK(side.commander_id == it.value().get<std::string>());
                CHECK(!side.commander_name.empty() && side.commander_traits.size() == 2 && side.commander_aura_radius_m > 0.f);
            }
            if (ex.contains("no_commander"))
                for (const auto& f : ex.at("no_commander")) CHECK(setup.sides.at(f.get<std::string>()).commander_id.empty());
            for (const auto& row : ex.at("factor_rows")) {
                bool found = false;
                for (const SideFactor& f : setup.sides.at(row.at("faction").get<std::string>()).factors)
                    found = found || (f.label == row.at("label").get<std::string>() && f.value == row.at("value").get<int>() &&
                                      f.source.find(row.at("source_has").get<std::string>()) != std::string::npos);
                if (!found) g_reporter()->message(name + ": no row " + row.dump());
                CHECK(found);
            }
            for (auto it = ex.at("morale").begin(); it != ex.at("morale").end(); ++it)
                CHECK(setup.sides.at(it.key()).morale == it.value().get<int>());
            // The hero kit (GAME INVENTION): on the player's general only, with his first trait's ability.
            auto close = [](float a, double b) { return std::abs(static_cast<double>(a) - b) < 1e-4; };
            if (ex.contains("hour_of_day")) CHECK(setup.hour_of_day == ex.at("hour_of_day").get<int>());
            if (ex.contains("hero")) {
                const json& h = ex.at("hero");
                const SideSetup& side = setup.sides.at(h.at("faction").get<std::string>());
                CHECK(side.has_hero);
                CHECK(side.hero_ability.trait == h.at("trait").get<std::string>());
                CHECK(side.hero_ability.trait == side.commander_traits.front());
                CHECK(side.hero_ability.kind == h.at("kind").get<std::string>());
                CHECK(!side.hero_ability.label.empty() && !side.hero_ability.text.empty());
                CHECK(close(side.hero_health_scale, h.at("health_scale").get<double>()));
                CHECK(close(side.hero_aura_radius_m, h.at("aura_radius_m").get<double>()));
                CHECK(close(side.hero_rally_radius_m, h.at("rally_radius_m").get<double>()));
                CHECK(close(side.hero_rally_cooldown_seconds, h.at("rally_cooldown_seconds").get<double>()));
                CHECK(side.hero_aura_radius_m > side.commander_aura_radius_m);  // larger than any general's
            }
            if (ex.contains("no_hero"))
                for (const auto& f : ex.at("no_hero")) CHECK(!setup.sides.at(f.get<std::string>()).has_hero);
            // The setup document reads back with the commander in it.
            const BattleSetup back = setup_from_json(setup_to_json(setup));
            for (const auto& sk : setup.sides) {
                CHECK(back.sides.at(sk.first).commander_id == sk.second.commander_id);
                CHECK(back.sides.at(sk.first).commander_traits == sk.second.commander_traits);
                CHECK(back.sides.at(sk.first).intel == sk.second.intel);
                CHECK(back.sides.at(sk.first).has_hero == sk.second.has_hero);
                CHECK(back.sides.at(sk.first).hero_ability.kind == sk.second.hero_ability.kind);
                CHECK(back.sides.at(sk.first).hero_ability.trait == sk.second.hero_ability.trait);
                CHECK(back.sides.at(sk.first).hero_health_scale == sk.second.hero_health_scale);
                CHECK(back.sides.at(sk.first).hero_aura_radius_m == sk.second.hero_aura_radius_m);
            }
            CHECK(back.hour_of_day == setup.hour_of_day);
            continue;
        }
        const json& ex = v.at("expect");
        BattleResult r;
        bool parsed = true;
        try {
            r = result_from_json(v.at("result").dump());
        } catch (const std::exception&) {
            parsed = false;
        }
        CHECK(parsed == ex.at("applied").get<bool>());
        if (!parsed) continue;
        const CampaignState after = apply_battle_result(s, r);
        CHECK(!after.pending_battle.active);
        check_persons_expect(after, ex, name);
        if (ex.contains("away_days"))
            for (auto it = ex.at("away_days").begin(); it != ex.at("away_days").end(); ++it)
                CHECK(after.pass.persons.at(it.key()).away_until_hour == after.now.hour_index + 24LL * it.value().get<int>());
        if (ex.contains("prisoners_held_delta"))
            for (auto it = ex.at("prisoners_held_delta").begin(); it != ex.at("prisoners_held_delta").end(); ++it)
                CHECK(after.prisoners_held.at(it.key()) - s.prisoners_held.at(it.key()) >= it.value().get<int>());
        const auto& rc = after.progress.last_battle_report.commander_events;
        CHECK(rc.size() == ex.at("report_commander_events").size());
        for (auto it = ex.at("report_commander_events").begin(); it != ex.at("report_commander_events").end(); ++it)
            CHECK(rc.count(it.key()) && rc.at(it.key()) == it.value().get<std::string>());
        // The general's fall is told on the card, in words, and the army is a detachment again.
        for (const auto& ev : rc) {
            const std::string army = ev.first == kBritish ? b.at(setup.attacker_faction == kBritish ? "attacker" : "defender").get<std::string>()
                                                          : b.at(setup.attacker_faction == kContinental ? "attacker" : "defender").get<std::string>();
            CHECK(general_of(after, army).empty());
            bool told = false;
            for (const std::string& line : after.progress.last_battle_report.map_changes)
                told = told || (line.find("General") != std::string::npos || line.find("Colonel") != std::string::npos);
            CHECK(told);
            for (const std::string& line : after.progress.last_battle_report.map_changes) CHECK(raw_ids_in(after, line).empty());
        }
        // The hero report: one line on the card when it names the player's general, none otherwise.
        auto reason_has = [&after](const std::string& text) {
            for (const std::string& line : after.progress.last_battle_report.change_reasons)
                if (line.find(text) != std::string::npos) return true;
            return false;
        };
        if (ex.contains("report_line_has"))
            for (const auto& t : ex.at("report_line_has")) {
                if (!reason_has(t.get<std::string>())) g_reporter()->message(name + ": the report does not say \"" + t.get<std::string>() + "\"");
                CHECK(reason_has(t.get<std::string>()));
            }
        if (ex.contains("report_line_absent"))
            for (const auto& t : ex.at("report_line_absent")) CHECK(!reason_has(t.get<std::string>()));
        // The result document reads back with the hero report in it.
        {
            const BattleResult back = result_from_json(result_to_json(r));
            CHECK(back.hero_faction == r.hero_faction && back.hero_person_id == r.hero_person_id);
            CHECK(back.hero_takeovers == r.hero_takeovers && back.hero_fell_while_played == r.hero_fell_while_played);
            CHECK(back.hero_seconds_played == r.hero_seconds_played);
        }
        // Winning with a fallen general: detachment, but no fall morale hit (army_morale_on_fall_victory).
        if (ex.value("winner_fall_morale_zero", false)) {
            const std::string win_fac = v.at("result").at("winner").get<std::string>();
            for (const auto& kv : after.progress.last_battle_report.morale_delta) {
                if (after.armies.count(kv.first) && after.armies.at(kv.first).faction == win_fac) {
                    // Victory bump (+5) and personal kills may raise morale; the fall itself adds 0.
                    CHECK(kv.second >= 0);
                }
            }
            bool detach_told = false, morale_paren = false;
            for (const std::string& line : after.progress.last_battle_report.map_changes) {
                if (line.find("detachment again") != std::string::npos) detach_told = true;
                if (line.find("detachment again (morale") != std::string::npos) morale_paren = true;
            }
            CHECK(detach_told && !morale_paren);
        }
        const CampaignState loaded = deserialize(serialize(after));
        CHECK(state_hash(loaded) == state_hash(after));
    }
}

// M5 (heroes, GAME INVENTION): the hero contract has its vectors, the hero's fall reaches the campaign
// through commander_events and only through it, and only the player's general is ever a hero.
static void test_pass_hero_vectors() {
    CampaignPassData data;
    ensure_battle_rules();
    CHECK(campaign_vectors("battle_setup_hero").size() >= 3);
    CHECK(campaign_vectors("battle_result_hero").size() >= 5);
    CHECK(campaign_vectors("battle_result_reject_hero").size() >= 2);
    CHECK(hero_rules().loaded);

    auto with_ward = []() {
        CampaignState s = battle_at("lexington", "british_regulars", "continental_militia");
        stand_down_generals(s);
        s.pass.persons["artemas_ward"].role = "general";
        s.pass.persons["artemas_ward"].army_id = "continental_militia";
        s.pass.persons["francis_smith"].role = "general";
        s.pass.persons["francis_smith"].army_id = "british_regulars";
        s.pass.seeded = true;
        return s;
    };
    // The fall of a played general reaches the campaign exactly as any general's does.
    {
        const CampaignState s = with_ward();
        BattleResult r = ended("lexington", kBritish, "army_broke");
        r.commander_events[kContinental] = "wounded";
        BattleResult played = r;
        played.hero_faction = kContinental;
        played.hero_person_id = "artemas_ward";
        played.hero_takeovers = 1;
        played.hero_seconds_played = 40.f;
        played.hero_fell_while_played = true;
        const CampaignState plain = apply_battle_result(s, r);
        CampaignState hero = apply_battle_result(s, result_from_json(result_to_json(played)));
        CHECK(hero.pass.persons.at("artemas_ward").status == "wounded");
        CHECK(hero.pass.persons.at("artemas_ward").role.empty());
        CHECK(general_of(hero, "continental_militia").empty());  // a detachment again
        CHECK(hero.progress.last_battle_report.commander_events.at(kContinental) == "wounded");
        CHECK(hero.armies.at("continental_militia").morale == plain.armies.at("continental_militia").morale);
        CHECK(hero.armies.at("continental_militia").morale < s.armies.at("continental_militia").morale);
        // The hero block is a report and nothing more: take its one line away and the two states are one.
        CHECK(hero.progress.last_battle_report.change_reasons.size() == plain.progress.last_battle_report.change_reasons.size() + 1);
        hero.progress.last_battle_report.change_reasons = plain.progress.last_battle_report.change_reasons;
        CHECK(state_hash(hero) == state_hash(plain));
    }
    // The hero is the PLAYER's general, whichever side the player has.
    {
        CampaignState s = with_ward();
        const BattleSetup mine = build_battle_setup(s, "lexington");
        CHECK(mine.sides.at(kContinental).has_hero && !mine.sides.at(kBritish).has_hero);
        s.player_faction = kBritish;
        const BattleSetup theirs = build_battle_setup(s, "lexington");
        CHECK(theirs.sides.at(kBritish).has_hero && !theirs.sides.at(kContinental).has_hero);
        CHECK(theirs.sides.at(kBritish).hero_ability.trait == theirs.sides.at(kBritish).commander_traits.front());
    }
    // No general, no hero.
    {
        CampaignState s = with_ward();
        s.pass.persons["artemas_ward"].role.clear();
        s.pass.persons["artemas_ward"].army_id.clear();
        const BattleSetup setup = build_battle_setup(s, "lexington");
        CHECK(setup.sides.at(kContinental).commander_id.empty());
        CHECK(!setup.sides.at(kContinental).has_hero);
        CHECK(setup_to_json(setup).find("\"hero\"") == std::string::npos);
    }
    // Every general in the file has a first trait, and that trait has an ability.
    {
        const json persons = json::parse(read_text_file(g_data_dir() + "/persons_boston_1775.json"));
        const HeroRules H = hero_rules();
        int generals = 0;
        for (const auto& p : persons.at("persons")) {
            bool general = false;
            for (const auto& role : p.at("roles")) general = general || role.get<std::string>() == "general";
            if (!general) continue;
            ++generals;
            CHECK(H.trait_abilities.count(p.at("traits").at(0).get<std::string>()) == 1);
        }
        CHECK(generals >= 10);
    }
}

// The hero's numbers are the data file's: a changed file changes the rules and the setup; a file with
// no hero block has no hero; a hero block not marked invented, or short of an ability, is refused.
static void test_hero_rules_are_data() {
    CampaignPassData data;
    ensure_battle_rules();
    const json original = json::parse(read_text_file(g_data_dir() + "/persons_boston_1775.json"));
    auto setup_with = [](const std::string& general) {
        CampaignState s = battle_at("lexington", "british_regulars", "continental_militia");
        stand_down_generals(s);
        s.pass.persons[general].role = "general";
        s.pass.persons[general].army_id = "continental_militia";
        s.pass.seeded = true;
        return build_battle_setup(s, "lexington");
    };
    {
        json changed = original;
        changed["hero"]["score"]["threshold"] = 250;
        changed["hero"]["meter"]["seconds"] = 33;
        changed["hero"]["kit"]["aura_radius_m"] = 77;
        changed["hero"]["kit"]["sword"]["damage"] = 91;
        changed["hero"]["trait_abilities"]["defensive"]["radius_m"] = 99;
        for (auto& p : changed["persons"])
            if (p.at("id").get<std::string>() == "israel_putnam") p["hero"]["health_scale"] = 3.5;
        CHECK(load_persons(changed.dump()).empty());
        const HeroRules H = hero_rules();
        CHECK(H.loaded && H.score_threshold == 250 && H.meter_seconds == 33.f && H.aura_radius_m == 77.f && H.sword_damage == 91.f);
        const BattleSetup ward = setup_with("artemas_ward");
        CHECK(ward.sides.at(kContinental).has_hero);
        CHECK(ward.sides.at(kContinental).hero_aura_radius_m == 77.f);
        CHECK(ward.sides.at(kContinental).hero_ability.kind == "hold_fast" && ward.sides.at(kContinental).hero_ability.radius_m == 99.f);
        const BattleSetup putnam = setup_with("israel_putnam");
        CHECK(putnam.sides.at(kContinental).hero_health_scale == 3.5f);
        CHECK(putnam.sides.at(kContinental).hero_aura_radius_m == 77.f);  // his own numbers only where he has them
    }
    {
        json none = original;
        none.erase("hero");
        for (auto& p : none["persons"]) p.erase("hero");
        CHECK(load_persons(none.dump()).empty());
        CHECK(!hero_rules().loaded);
        const BattleSetup ward = setup_with("artemas_ward");
        CHECK(!ward.sides.at(kContinental).commander_id.empty() && !ward.sides.at(kContinental).has_hero);
    }
    CHECK(load_persons(original.dump()).empty());
    {
        json unmarked = original;
        unmarked["hero"]["basis"] = "documented";
        CHECK(!load_persons(unmarked.dump()).empty());
        json short_one = original;
        short_one["hero"]["trait_abilities"].erase("popular");
        CHECK(!load_persons(short_one.dump()).empty());
        json bad_kind = original;
        bad_kind["hero"]["trait_abilities"]["popular"]["kind"] = "thunderbolt";
        CHECK(!load_persons(bad_kind.dump()).empty());
        json agent_hero = original;
        for (auto& p : agent_hero["persons"])
            if (p.at("id").get<std::string>() == "paul_revere") p["hero"] = {{"basis", "invented"}};
        CHECK(!load_persons(agent_hero.dump()).empty());
        // A refused file leaves the loaded rules as they were.
        CHECK(hero_rules().loaded && hero_rules().score_threshold == original.at("hero").at("score").at("threshold").get<int>());
    }
}

// M7 (time bands): the setup says the hour of the day, from the campaign clock; an old document that
// does not say it reads as "not said" and is written back without it.
static void test_setup_carries_the_hour() {
    ensure_battle_rules();
    for (int h : {0, 5, 12, 19, 23}) {
        CampaignState s = battle_at("lexington");
        YMDH at{1775, 6, 17, static_cast<uint8_t>(h)};
        s.now = from_ymdh(at);
        const BattleSetup setup = build_battle_setup(s, "lexington");
        CHECK(setup.valid && setup.hour_of_day == h);
        CHECK(setup_from_json(setup_to_json(setup)).hour_of_day == h);
    }
    const BattleSetup setup = build_battle_setup(battle_at("lexington"), "lexington");
    json doc = json::parse(setup_to_json(setup));
    CHECK(doc.contains("hour_of_day"));
    doc.erase("hour_of_day");
    const BattleSetup old = setup_from_json(doc.dump());
    CHECK(old.hour_of_day == -1);
    CHECK(!json::parse(setup_to_json(old)).contains("hour_of_day"));
    for (int bad : {-1, 24}) {
        doc["hour_of_day"] = bad;
        bool threw = false;
        try {
            (void)setup_from_json(doc.dump());
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }
}

static BattleResult ended_with(const std::string& province, const std::string& winner, const std::string& reason) {
    BattleResult r;
    r.province_id = province;
    r.winner = winner;
    r.end_reason = reason;
    r.objective_holder = winner;
    return r;
}

// Generals, detachments, agents, organizers, the informer, what each side knows.
static void test_pass_people() {
    CampaignPassData data;
    ensure_battle_rules();
    // 1) The opening: every army has its commander from the first hour, before the clock has run
    //    (user decision 2026-11-15: Colonel Barrett, who commanded at Concord, has the militia).
    {
        const CampaignState start = fresh();
        CHECK(start.pass.seeded);
        CHECK(general_of(start, "continental_militia") == "james_barrett");
        CHECK(general_of(start, "british_regulars") == "francis_smith" && general_of(start, "british_garrison") == "hugh_percy");
        CHECK(person_name("james_barrett") == "Colonel James Barrett");
        CHECK(army_unit_cap(start, "continental_militia") == 40);
        CHECK(why_cannot_besiege(start, "continental_militia", "boston").empty());
        // No army opens as a detachment, on either side.
        for (const auto& ak : start.armies) {
            if (general_of(start, ak.first).empty()) g_reporter()->message("opens with no commander: " + ak.first);
            CHECK(!general_of(start, ak.first).empty());
        }
        // Heath, the only general officer on the field that day, is at hand from the 19th and holds nothing.
        CHECK(start.pass.persons.count("william_heath") == 0 || start.pass.persons.at("william_heath").role.empty());
        // Saved and loaded at the first hour, he still commands.
        const CampaignState loaded = deserialize(serialize(start));
        CHECK(general_of(loaded, "continental_militia") == "james_barrett" && state_hash(loaded) == state_hash(start));
        // The first fight, at Concord: he is named on the card and his two traits are rows with him as
        // their source (Defensive +8 morale standing to receive, Popular +4).
        CampaignState fight = start;
        fight.armies.at("british_regulars").province = "concord";
        fight.armies.at("british_regulars").previous_province = "lincoln";
        fight.pending_battle = {true, "concord", "british_regulars", "continental_militia"};
        const BattleSetup setup = build_battle_setup(fight, "concord");
        const SideSetup& militia = setup.sides.at(kContinental);
        CHECK(militia.commander_id == "james_barrett" && militia.commander_name == "Colonel James Barrett" && militia.commander_traits.size() == 2);
        int trait_morale = 0, trait_rows = 0;
        for (const SideFactor& f : militia.factors)
            if (f.source.find("Colonel James Barrett") != std::string::npos && f.label == "morale") {
                trait_morale += f.value;
                ++trait_rows;
            }
        CHECK(trait_rows == 2 && trait_morale == 12);
        CampaignState plain = fight;
        stand_down_generals(plain);
        plain.pass.persons["francis_smith"].role = "general";  // the British keep theirs
        plain.pass.persons["francis_smith"].army_id = "british_regulars";
        const BattleSetup bare = build_battle_setup(plain, "concord");
        CHECK(bare.sides.at(kContinental).commander_id.empty());
        CHECK(militia.morale == std::min(100, bare.sides.at(kContinental).morale + 12));
        // The men on the field are the same with or without him: he steadies them, he does not add any.
        CHECK(militia.field_strength == bare.sides.at(kContinental).field_strength);
        CHECK(setup.sides.at(kBritish).field_strength == bare.sides.at(kBritish).field_strength);
    }
    {
        const CampaignState s = pass_campaign(YMDH{1775, 4, 19, 0});
        CHECK(general_of(s, "british_regulars") == "francis_smith");
        CHECK(general_of(s, "british_garrison") == "hugh_percy");
        CHECK(general_of(s, "continental_militia") == "james_barrett");
        CHECK(s.pass.persons.at("thomas_gage").role == "envoy");
        CHECK(person_name("artemas_ward") == "General Artemas Ward");
        // The forces list reads people, not ids.
        const json people = json::parse(persons_json(s));
        CHECK(people.at("persons").size() == 22);  // the Patriot roster
        int generals = 0, at_hand = 0;
        for (const auto& p : people.at("persons")) {
            CHECK(raw_ids_in(s, p.at("where").get<std::string>()).empty());
            CHECK(p.at("basis").get<std::string>() == "documented" || p.at("basis").get<std::string>() == "invented");
            bool general = false;
            for (const auto& r : p.at("roles")) general = general || r.get<std::string>() == "general";
            if (general) {
                ++generals;
                CHECK(p.at("traits").size() == 2);
            }
            if (p.at("present").get<bool>()) ++at_hand;
        }
        CHECK(generals == 10);
        // On 19 April: Heath is on the field; Ward comes on the 20th; Washington not at all yet.
        auto present = [&](const std::string& id) {
            for (const auto& p : people.at("persons"))
                if (p.at("id").get<std::string>() == id) return p.at("present").get<bool>();
            return false;
        };
        CHECK(present("william_heath") && !present("artemas_ward") && !present("george_washington") && !present("daniel_morgan"));
        CHECK(at_hand >= 8);
    }
    // 2) A detachment cannot besiege; an army under a general can.
    {
        CampaignState s = battle_at("charlestown", "continental_militia", "british_regulars");
        s.provinces.at("charlestown").owner = kBritish;  // Fort L1, held by the British for this case
        s.pass.seeded = true;
        // Under Colonel Barrett the militia sits down to the fort it could not take in the playtest.
        const CampaignState barrett = apply_battle_result(s, ended_with("charlestown", kContinental, "posts_swept"));
        CHECK(barrett.provinces.at("charlestown").siege_by == kContinental);
        // Relieved (a battle is pending, so by hand here: the order is refused until it is fought).
        CHECK(validate_campaign_order(s, order_json({{"order", "relieve_person"}, {"person", "james_barrett"}})) == "battle_pending");
        s.pass.persons["james_barrett"].role.clear();
        s.pass.persons["james_barrett"].army_id.clear();
        const CampaignState none = apply_battle_result(s, ended_with("charlestown", kContinental, "posts_swept"));
        CHECK(none.provinces.at("charlestown").owner == kBritish);
        CHECK(none.provinces.at("charlestown").siege_by.empty());
        CHECK(none.progress.last_battle_report.ownership_reason == "no_general_no_siege");
        CampaignState led = s;
        led.pass.persons["william_heath"].role = "general";
        led.pass.persons["william_heath"].army_id = "continental_militia";
        const CampaignState sat = apply_battle_result(led, ended_with("charlestown", kContinental, "posts_swept"));
        CHECK(sat.provinces.at("charlestown").siege_by == kContinental);
        CHECK(sat.provinces.at("charlestown").siege_hours_remaining == 72);
        // The siege is lifted if the general leaves the army.
        CampaignState left = sat;
        left.pass.persons["william_heath"].role.clear();
        left.pass.persons["william_heath"].army_id.clear();
        tick_sieges(left);
        CHECK(left.provinces.at("charlestown").siege_by.empty());
        // Occupation: a detachment standing alone before a fort does not sit down to it at midnight.
        CampaignState occ = relieved(pass_campaign(YMDH{1775, 4, 20, 12}));
        occ.treaties.clear();
        occ.provinces.at("charlestown").owner = kBritish;
        occ.armies.at("continental_militia").province = "charlestown";
        occ.armies.at("british_regulars").province = "boston";
        for (int i = 0; i < 14; ++i) occ = advance_hour(occ).state;
        CHECK(occ.provinces.at("charlestown").siege_by.empty() && occ.provinces.at("charlestown").owner == kBritish);
        occ = pass_order(occ, order_json({{"order", "assign_person"}, {"person", "samuel_adams"}, {"role", "organizer"}, {"province", "cambridge"}}));
        std::string err;
        // (Charlestown is British here, so no army can be formed in it: the muster must be the player's.)
        pass_order(occ, order_json({{"order", "form_army"}, {"person", "william_heath"}, {"province", "charlestown"}}), &err);
        CHECK(err == "not_your_province");
    }
    // 3) The cap: a company raised where a detachment is full stands by itself; under a general it joins.
    {
        CampaignState s = relieved(pass_campaign(YMDH{1775, 4, 21, 0}));
        s.player_gold = 500;
        s.armies.at("continental_militia").province = "charlestown";
        s.provinces.at("charlestown").militia_muster = true;
        s.province_stores["charlestown"].provisions = 60;
        s.province_stores["charlestown"].muskets = 60;
        const int before = pass_units(s.armies.at("continental_militia"));
        CHECK(before == 22 && army_unit_cap(s, "continental_militia") == 12);
        std::string err;
        CampaignState raised = start_recruitment(s, "charlestown", "militia_company", err);
        CHECK(err.empty());
        for (int i = 0; i < 24 * 8 && !raised.progress.recruit_jobs.empty(); ++i) raised = advance_hour(raised).state;
        CHECK(raised.progress.recruit_jobs.empty());
        CHECK(pass_units(raised.armies.at("continental_militia")) == before);  // over the cap: not taken in
        int others = 0;
        for (const auto& ak : raised.armies)
            if (ak.second.faction == kContinental && ak.first != "continental_militia") ++others;
        CHECK(others == 1);
        CampaignState led = pass_order(s, order_json({{"order", "form_army"}, {"person", "william_heath"}, {"province", "charlestown"}}));
        CHECK(army_unit_cap(led, "continental_militia") == 40);
        led = start_recruitment(led, "charlestown", "militia_company", err);
        CHECK(err.empty());
        for (int i = 0; i < 24 * 8 && !led.progress.recruit_jobs.empty(); ++i) led = advance_hour(led).state;
        CHECK(pass_units(led.armies.at("continental_militia")) > before);
        // An organizer in the town musters the next company in three quarters of the time.
        CampaignState org = pass_order(led, order_json({{"order", "assign_person"}, {"person", "samuel_adams"}, {"role", "organizer"}, {"province", "charlestown"}}));
        CampaignState plain = start_recruitment(led, "charlestown", "militia_company", err);
        CampaignState quick = start_recruitment(org, "charlestown", "militia_company", err);
        if (!err.empty()) g_reporter()->message("second muster refused: " + err);
        CHECK(err.empty() && !plain.progress.recruit_jobs.empty() && !quick.progress.recruit_jobs.empty());
        if (!plain.progress.recruit_jobs.empty() && !quick.progress.recruit_jobs.empty())
            CHECK(quick.progress.recruit_jobs[0].hours_total * 100 == plain.progress.recruit_jobs[0].hours_total * 75);
        // ... and Support rises on the 1st of the month with his name on the line.
        const int support = org.local_support;
        org.now = GameTime{from_ymdh({1775, 5, 1, 0}).hour_index - 1};
        org = advance_hour(org).state;
        bool line = false;
        for (const auto& ch : org.resource_changes) line = line || (ch.reason.find("Samuel Adams, organizer at Charlestown") != std::string::npos && ch.local_support == 1);
        if (!line)
            for (const auto& ch : org.resource_changes) g_reporter()->message("ledger: " + ch.reason);
        CHECK(line);
        (void)support;
        // Relieved, he is at hand again.
        org = pass_order(org, order_json({{"order", "relieve_person"}, {"person", "samuel_adams"}}));
        CHECK(org.pass.persons.at("samuel_adams").role.empty());
    }
    // 4) What each side knows. An estimate until scouted, or with an organizer in the town.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 21, 0});
        // (The British scout, who would count the militia at the first midnight, is kept out of this picture.)
        CHECK(s.pass.scouted_until.count(std::string(kBritish) + ":continental_militia") == 1);
        s.pass.scouted_until.clear();
        s.pass.persons["henry_de_berniere"].uses_left = 0;
        const json rough = json::parse(army_intel_json(s, kContinental, "british_regulars"));
        CHECK(!rough.at("known").get<bool>());
        CHECK(rough.at("men_high").get<int>() > rough.at("men_low").get<int>());
        CHECK(rough.at("men_low").get<int>() % 20 == 0);  // estimates.step_men
        const int truth = pass_units(s.armies.at("british_regulars")) * battle_rules().soldiers_per_unit;
        // Unscouted range is centred on the truth (width_pct in data); never excludes it.
        CHECK(rough.at("men_low").get<int>() <= truth && rough.at("men_high").get<int>() >= truth);
        // Across seeds and days the true men count is always inside the band.
        for (uint64_t seed = 1; seed <= 64; ++seed) {
            for (int day = 0; day < 30; ++day) {
                CampaignState t = s;
                t.seed = seed;
                t.now.hour_index = s.now.hour_index + day * 24;
                t.pass.scouted_until.clear();
                const json est = json::parse(army_intel_json(t, kContinental, "british_regulars"));
                CHECK(!est.at("known").get<bool>());
                CHECK(est.at("men_low").get<int>() <= truth);
                CHECK(est.at("men_high").get<int>() >= truth);
                CHECK(est.at("men_high").get<int>() - est.at("men_low").get<int>() >= 20);
                const int seen = units_as_seen(t, kContinental, "british_regulars");
                const int truth_units = pass_units(t.armies.at("british_regulars"));
                CHECK(seen <= truth_units);
                const int mid_units = ((est.at("men_low").get<int>() + est.at("men_high").get<int>()) / 2) / battle_rules().soldiers_per_unit;
                CHECK(seen == std::min(truth_units, std::max(1, mid_units)));
            }
        }
        // A column smaller than one step, and a zero width: the truth stays inside, and a zero
        // width does not take the seed's extra. Targeting never counts more men than there are.
        {
            const MenBand wide = unscouted_men_band(4, 20, 30, 0);
            const MenBand wider = unscouted_men_band(4, 20, 30, 15);
            CHECK(wide.low <= 4 && wide.high >= 4 && wider.low <= 4 && wider.high >= 4);
            CHECK(wider.high >= wide.high);
            const MenBand none = unscouted_men_band(96, 20, 0, 0);
            const MenBand none_extra = unscouted_men_band(96, 20, 0, 40);
            CHECK(none.low == none_extra.low && none.high == none_extra.high);
            CHECK(none.low <= 96 && none.high >= 96);
            const MenBand empty = unscouted_men_band(0, 20, 30, 10);
            CHECK(empty.low <= 0 && empty.high >= 0);
            CampaignState tiny = s;
            tiny.pass.scouted_until.clear();
            tiny.armies.at("british_regulars").units = {{"line_infantry", 1}};
            const int men = pass_units(tiny.armies.at("british_regulars")) * battle_rules().soldiers_per_unit;
            CHECK(men > 0 && men < 20);
            const json est = json::parse(army_intel_json(tiny, kContinental, "british_regulars"));
            CHECK(!est.at("known").get<bool>());
            CHECK(est.at("men_low").get<int>() <= men && est.at("men_high").get<int>() >= men);
            CHECK(units_as_seen(tiny, kContinental, "british_regulars") == 1);
            tiny.armies.at("british_regulars").units.clear();
            CHECK(units_as_seen(tiny, kContinental, "british_regulars") == 0);
        }
        CHECK(json::parse(army_intel_json(s, kContinental, "continental_militia")).at("known").get<bool>());  // one's own
        // The British know the Patriot numbers for free while Church holds a role...
        CHECK(!json::parse(army_intel_json(s, kBritish, "continental_militia")).at("known").get<bool>());
        CampaignState church = pass_order(s, order_json({{"order", "assign_person"}, {"person", "benjamin_church"}, {"role", "organizer"}, {"province", "cambridge"}}));
        const json known = json::parse(army_intel_json(church, kBritish, "continental_militia"));
        CHECK(known.at("known").get<bool>() && known.at("men_low").get<int>() == 88);
        // ...until the ciphered letter is read on 4 October.
        church.now = GameTime{from_ymdh({1775, 10, 4, 0}).hour_index - 1};
        church = advance_hour(church).state;
        CHECK(church.pass.persons.at("benjamin_church").status == "exposed" && church.pass.persons.at("benjamin_church").role.empty());
        CHECK(!json::parse(army_intel_json(church, kBritish, "continental_militia")).at("known").get<bool>());
        bool letter = false;
        for (const auto& c : church.staff.queue) {
            if (c.subject.find("Benjamin Church") == std::string::npos) continue;
            letter = true;
            CHECK(c.kind == "historical_notice" && !c.awaiting_choice);
            CHECK(raw_ids_in(church, c.body).empty() && raw_ids_in(church, c.subject).empty());
        }
        CHECK(letter);
        std::string err;
        pass_order(church, order_json({{"order", "assign_person"}, {"person", "benjamin_church"}, {"role", "organizer"}, {"province", "concord"}}), &err);
        CHECK(err == "person_not_available");
        // A man never given a role is exposed all the same, without the free scouting ever having run.
        CampaignState idle = s;
        idle.now = GameTime{from_ymdh({1775, 10, 4, 0}).hour_index - 1};
        idle = advance_hour(idle).state;
        CHECK(idle.pass.persons.at("benjamin_church").status == "exposed");
        // The pre-battle card: the enemy column is an estimate, the player's own is exact.
        CampaignState b = battle_at("lexington");
        b.pass.seeded = true;
        const BattleSetup rough_setup = build_battle_setup(b, "lexington");
        CHECK(rough_setup.sides.at(kBritish).intel == "estimate" && rough_setup.sides.at(kContinental).intel == "exact");
        CHECK(rough_setup.sides.at(kBritish).intel_men_high > rough_setup.sides.at(kBritish).intel_men_low);
        CHECK(raw_ids_in(b, rough_setup.sides.at(kBritish).intel_source).empty());
        CampaignState org = b;
        org.pass.persons["samuel_adams"].role = "organizer";
        org.pass.persons["samuel_adams"].province = "lexington";
        CHECK(build_battle_setup(org, "lexington").sides.at(kBritish).intel == "exact");
        CampaignState scouted = b;
        scouted.pass.scouted_until[std::string(kContinental) + ":british_regulars"] = b.now.hour_index + 24;
        CHECK(build_battle_setup(scouted, "lexington").sides.at(kBritish).intel == "exact");
        // The battle itself always uses the true numbers.
        CHECK(rough_setup.sides.at(kBritish).field_strength == build_battle_setup(scouted, "lexington").sides.at(kBritish).field_strength);
    }
    // 5) The alarm, spent on the pre-battle card: applied at once (the clock is stopped), a row on
    //    the card, gone after the battle.
    {
        CampaignController c;
        c.new_campaign(read_text_file(g_data_dir() + "/boston1775.json"));
        c.advance_hours(72, false);
        CHECK(c.battle_pending());
        const std::string town = c.state().pending_battle.province;
        CHECK(c.state().armies.at(c.state().pending_battle.defender_army).faction == kContinental);
        const BattleSetup before = build_battle_setup(c.state(), town);
        CHECK(!before.sides.at(kContinental).alarmed);
        CHECK(c.issue_order(order_json({{"order", "use_agent"}, {"person", "paul_revere"}})).empty());
        CHECK(c.state().pending_commands.empty() || c.state().pending_commands.back().payload.kind != CommandKind::CampaignOrder);
        const BattleSetup after = build_battle_setup(c.state(), town);
        CHECK(after.sides.at(kContinental).alarmed && !after.sides.at(kBritish).alarmed);
        CHECK(after.sides.at(kContinental).morale == before.sides.at(kContinental).morale + 3);
        bool row = false;
        for (const SideFactor& f : after.sides.at(kContinental).factors)
            row = row || (f.label == "alarm" && f.source.find("Paul Revere's alarm") != std::string::npos);
        CHECK(row);
        CHECK(c.state().pass.persons.at("paul_revere").uses_left == 1);
        CHECK(c.issue_order(order_json({{"order", "use_agent"}, {"person", "william_dawes"}})) == std::vector<std::string>{"already_alarmed"});
        std::string err;
        CHECK(c.begin_battle(err));
        CHECK(c.battle_setup().sides.at(kContinental).alarmed);
        CHECK(c.finish_battle(auto_resolve_battle(c.state(), c.battle_setup()), err));
        for (const TownFactor& f : c.state().pass.town_factors) CHECK(f.label != "alarm");
    }
    // 6) The engineer's works and a general's traits are the numbers the battle uses.
    {
        CampaignState s = battle_at("lexington");
        s.pass.seeded = true;
        stand_down_generals(s);  // each general below is measured against no general at all
        const BattleSetup plain = build_battle_setup(s, "lexington");
        CHECK(plain.sides.at(kContinental).fortification_level == 0 && plain.mode == "conquest");
        s.now = from_ymdh({1775, 4, 25, 6});
        CampaignState works = pass_order(s, order_json({{"order", "use_agent"}, {"person", "richard_gridley"}, {"province", "lexington"}}));
        const BattleSetup dug = build_battle_setup(works, "lexington");
        CHECK(dug.sides.at(kContinental).fortification_level == 1);
        CHECK(dug.sides.at(kBritish).fortification_level == 0);  // works are the defender's
        CHECK(dug.mode == "assault");  // a defender behind works is assaulted (battle modes)
        // John Thomas (Engineer, Steadfast) defending his own ground digs in without an agent.
        CampaignState thomas = s;
        thomas.pass.persons["john_thomas"].role = "general";
        thomas.pass.persons["john_thomas"].army_id = "continental_militia";
        const BattleSetup t = build_battle_setup(thomas, "lexington");
        CHECK(t.sides.at(kContinental).fortification_level == 1 && t.sides.at(kContinental).readiness == std::min(100, plain.sides.at(kContinental).readiness + 10));
        // Pitcairn (Aggressive, Popular) attacking: +8 and +4 morale; defending he keeps only Popular.
        CampaignState pit = s;
        pit.pass.persons["john_pitcairn"].role = "general";
        pit.pass.persons["john_pitcairn"].army_id = "british_regulars";
        CHECK(build_battle_setup(pit, "lexington").sides.at(kBritish).morale == plain.sides.at(kBritish).morale + 12);
        CampaignState def = battle_at("lexington", "continental_militia", "british_regulars");
        def.pass.seeded = true;
        stand_down_generals(def);
        const int base = build_battle_setup(def, "lexington").sides.at(kBritish).morale;
        def.pass.persons["john_pitcairn"].role = "general";
        def.pass.persons["john_pitcairn"].army_id = "british_regulars";
        CHECK(build_battle_setup(def, "lexington").sides.at(kBritish).morale == base + 4);
        // Putnam (Adroit is not his; Aggressive, Popular) and Greene (Adroit): reinforcement comes faster.
        CampaignState greene = s;
        greene.pass.persons["nathanael_greene"].role = "general";
        greene.pass.persons["nathanael_greene"].army_id = "continental_militia";
        CHECK(build_battle_setup(greene, "lexington").sides.at(kContinental).reinforcement_scale < plain.sides.at(kContinental).reinforcement_scale);
    }
    // 7) The other side follows the same rules: a column formed in Boston gets a free general at
    //    the next midnight, and a general wounded comes back.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 22, 0});
        Army col;
        col.id = "british_field_x";
        col.name = "British Column";
        col.faction = kBritish;
        col.province = "boston";
        col.previous_province = "boston";
        col.units["line_infantry"] = 8;
        s.armies[col.id] = col;
        CHECK(general_of(s, "british_field_x").empty());
        for (int i = 0; i < 25; ++i) s = advance_hour(s).state;
        CHECK(general_of(s, "british_field_x") == "john_pitcairn");
        CHECK(s.pass.persons.at("timothy_ruggles").role == "organizer" && s.pass.persons.at("timothy_ruggles").province == "boston");
        s.pass.persons["john_pitcairn"].role.clear();
        s.pass.persons["john_pitcairn"].army_id.clear();
        s.pass.persons["john_pitcairn"].status = "wounded";
        s.pass.persons["john_pitcairn"].away_until_hour = s.now.hour_index + 24 * 21;
        for (int i = 0; i < 24 * 21 + 26; ++i) s = advance_hour(s).state;
        CHECK(s.pass.persons.at("john_pitcairn").status.empty());
        CHECK(general_of(s, "british_field_x") == "john_pitcairn");
    }
    // 8) Nothing of this runs, and nothing is saved, without the data.
    {
        unload_persons();
        unload_politics();
        unload_decisions();
        CampaignState s = quiet_campaign();
        for (int i = 0; i < 48; ++i) s = advance_hour(s).state;
        CHECK(!s.pass.any());
        CHECK(serialize(s).find("\"pass\"") == std::string::npos);
        CHECK(validate_campaign_order(s, order_json({{"order", "form_army"}, {"person", "william_heath"}, {"province", "charlestown"}})) == "persons_not_loaded");
        Army a = s.armies.at("continental_militia");
        CHECK(army_unit_cap(s, "continental_militia") == 0);
        (void)a;
    }
}

// Measures: pass, fail, the early vote, the return of a failed measure, dispatches, the recall.
static void test_pass_politics() {
    CampaignPassData data;
    ensure_battle_rules();
    auto measure = [](const CampaignState& s, const std::string& id) {
        const json floor = json::parse(politics_json(s));
        for (const auto& m : floor.at("measures"))
            if (m.at("id").get<std::string>() == id) return json(m);
        g_reporter()->message("measure not on the floor: " + id);
        CHECK(false);
        return json{{"status", ""}, {"when", ""}, {"chances", json::object()}, {"if_passed", ""}, {"if_failed", ""}};
    };
    // 1) The floor on 20 April: the Provincial Congress, three blocs, three measures.
    {
        const CampaignState s = pass_campaign(YMDH{1775, 4, 20, 0});
        const json floor = json::parse(politics_json(s));
        CHECK(floor.at("loaded").get<bool>() && floor.at("body").at("button").get<std::string>() == "Congress");
        CHECK(floor.at("body").at("name").get<std::string>() == "Massachusetts Provincial Congress");
        CHECK(floor.at("blocs").size() == 3 && floor.at("measures").size() == 3);
        CHECK(floor.at("favors").get<int>() == 1);
        CHECK(unity(s) == 70);  // leans 20, 5, -10: spread 30
        CHECK(measure(s, "raise_the_army").at("status").get<std::string>() == "open");
        CHECK(measure(s, "raise_the_army").at("when").get<std::string>() == "Vote on 23 April 1775");
        CHECK(measure(s, "raise_the_army").at("chances").at("committee_men").get<int>() == 85);  // 50 + 20 + 15
        CHECK(!measure(s, "raise_the_army").at("if_passed").get<std::string>().empty());
        CHECK(!measure(s, "raise_the_army").at("if_failed").get<std::string>().empty());
        for (const auto& m : floor.at("measures")) {
            CHECK(raw_ids_in(s, m.at("when").get<std::string>()).empty());
            CHECK(m.at("basis").get<std::string>() == "documented" || m.at("basis").get<std::string>() == "invented");
        }
    }
    // 2) Pass and fail, from the campaign seed. Warm blocs pass a measure on most seeds and cold
    //    ones fail it on most; one state always votes the same way.
    int passes_warm = 0, passes_cold = 0;
    uint64_t pass_seed = 0, fail_seed = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        CampaignState warm = pass_campaign(YMDH{1775, 4, 22, 23});
        warm.seed = seed;
        for (const char* b : {"committee_men", "country_towns", "seaport_merchants"}) warm.pass.bloc_lean[b] = 100;
        CampaignState cold = warm;
        for (const char* b : {"committee_men", "country_towns", "seaport_merchants"}) cold.pass.bloc_lean[b] = -100;
        const CampaignState w1 = advance_hour(warm).state;
        const CampaignState w2 = advance_hour(warm).state;
        CHECK(state_hash(w1) == state_hash(w2));
        const bool wp = w1.pass.measures.at("raise_the_army").status == "passed";
        const bool cp = advance_hour(cold).state.pass.measures.at("raise_the_army").status == "passed";
        passes_warm += wp ? 1 : 0;
        passes_cold += cp ? 1 : 0;
        if (wp && !pass_seed) pass_seed = seed;
        if (!cp && !fail_seed) fail_seed = seed;
    }
    g_reporter()->message("raise the army over 40 seeds: passes " + std::to_string(passes_warm) + " warm, " + std::to_string(passes_cold) + " cold");
    CHECK(passes_warm >= 34 && passes_cold <= 6 && pass_seed && fail_seed);
    // 3) Passed on its date: the militia enlist at Cambridge and the ledger says so in words.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 22, 23});
        s.seed = pass_seed;
        for (const char* b : {"committee_men", "country_towns", "seaport_merchants"}) s.pass.bloc_lean[b] = 100;
        int before = 0;
        for (const auto& ak : s.armies)
            if (ak.second.faction == kContinental) before += pass_units(ak.second);
        s = advance_hour(s).state;
        CHECK(to_ymdh(s.now).d == 23);
        CHECK(s.pass.measures.at("raise_the_army").status == "passed" && s.pass.measures.at("raise_the_army").yes >= 2);
        int after = 0;
        std::string at;
        for (const auto& ak : s.armies)
            if (ak.second.faction == kContinental) {
                after += pass_units(ak.second);
                if (ak.first != "continental_militia") at = ak.second.province;
            }
        CHECK(after == before + 6 && at == "cambridge");
        bool told = false;
        for (const auto& ch : s.resource_changes) {
            if (ch.reason.find("Raise the army passes") == std::string::npos) continue;
            told = true;
            CHECK(raw_ids_in(s, ch.reason).empty());
        }
        CHECK(told);
        CHECK(measure(s, "raise_the_army").at("status").get<std::string>() == "passed");
    }
    // 4) Failed: its failure effect, and it returns to the floor after retry_days with a week to
    //    work on the blocs before it is put again.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 22, 23});
        s.seed = fail_seed;
        for (const char* b : {"committee_men", "country_towns", "seaport_merchants"}) s.pass.bloc_lean[b] = -100;
        const int support = s.local_support;
        s = advance_hour(s).state;
        CHECK(s.pass.measures.at("raise_the_army").status == "failed");
        CHECK(s.local_support == support - 3);
        CHECK(s.pass.measures.at("raise_the_army").retry_hour == s.now.hour_index + 24 * 10);
        std::string err;
        pass_order(s, order_json({{"order", "vote_now"}, {"measure", "raise_the_army"}}), &err);
        CHECK(err == "not_before_it_returns");
        for (int i = 0; i < 24 * 10; ++i) s = advance_hour(s).state;
        CHECK(s.pass.measures.at("raise_the_army").status.empty());
        CHECK(measure(s, "raise_the_army").at("status").get<std::string>() == "open");
        CHECK(s.pass.measures.at("raise_the_army").vote_hour == s.now.hour_index + 24 * 7);
    }
    // 5) Vote now: the vote is taken on the next hour, before its date; lobbying first moves the odds.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 20, 6});
        s.seed = pass_seed;
        const int chance = measure(s, "raise_the_army").at("chances").at("seaport_merchants").get<int>();
        s = pass_order(s, order_json({{"order", "lobby"}, {"action", "write"}, {"bloc", "seaport_merchants"}}));
        s = pass_order(s, order_json({{"order", "lobby"}, {"action", "favor"}, {"bloc", "seaport_merchants"}}));
        CHECK(measure(s, "raise_the_army").at("chances").at("seaport_merchants").get<int>() == chance + 20);
        std::string err;
        pass_order(s, order_json({{"order", "lobby"}, {"action", "favor"}, {"bloc", "seaport_merchants"}}), &err);
        CHECK(err == "no_favors");
        s = pass_order(s, order_json({{"order", "lobby"}, {"action", "write"}, {"bloc", "country_towns"}}));
        pass_order(s, order_json({{"order", "lobby"}, {"action", "write"}, {"bloc", "country_towns"}}), &err);
        CHECK(err == "no_clerk_slot");  // two clerk slots a day
        CommandPayload p;
        p.kind = CommandKind::CampaignOrder;
        p.result_json = order_json({{"order", "vote_now"}, {"measure", "raise_the_army"}});
        s = enqueue_command(s, p);
        CHECK(s.pass.measures.count("raise_the_army") == 0 || s.pass.measures.at("raise_the_army").status.empty());
        s = advance_hour(s).state;
        const std::string st = s.pass.measures.at("raise_the_army").status;
        CHECK(st == "passed" || st == "failed");
        CHECK(to_ymdh(s.now).d == 20);  // three days before its date
        pass_order(s, order_json({{"order", "vote_now"}, {"measure", "raise_the_army"}}), &err);
        CHECK(err == (st == "passed" ? "already_passed" : "not_before_it_returns"));
    }
    // 6) A delegate shifts the bloc at once on seating, then week by week while he stays.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 20, 0});
        s = pass_order(s, order_json({{"order", "assign_person"}, {"person", "john_hancock"}, {"role", "delegate"}, {"bloc", "seaport_merchants"}}));
        const int lean0 = -10;
        CHECK(s.pass.bloc_lean.at("seaport_merchants") == lean0 + 3);  // seat_shift
        std::string err;
        pass_order(s, order_json({{"order", "relieve_person"}, {"person", "john_hancock"}}), &err);
        CHECK(err == "recently_seated");
        pass_order(s, order_json({{"order", "assign_person"}, {"person", "john_adams"}, {"role", "delegate"}, {"bloc", "seaport_merchants"}}), &err);
        CHECK(err == "bloc_taken");
        for (int i = 0; i < 24 * 14; ++i) s = advance_hour(s).state;
        const int with = s.pass.bloc_lean.at("seaport_merchants");
        CHECK(with == lean0 + 3 + 6);  // seat + two Mondays at +3
        CHECK(json::parse(politics_json(s)).at("blocs")[2].at("delegate").get<std::string>() == "John Hancock");
        err.clear();
        s = pass_order(s, order_json({{"order", "relieve_person"}, {"person", "john_hancock"}}), &err);
        CHECK(err.empty());  // reseat_hours (168) have passed
    }
    // 6b) transfer_units: companies move between idle armies; allied companies are refused.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 20, 0});
        Army raised;
        raised.id = "raised_concord_1";
        raised.name = "Concord Company";
        raised.faction = kContinental;
        raised.province = "concord";
        raised.previous_province = "concord";
        raised.units["militia"] = 1;
        raised.morale = 60;
        s.armies[raised.id] = raised;
        const int before = pass_units(s.armies.at("continental_militia"));
        s = pass_order(s, json{{"order", "transfer_units"},
                               {"from_army", "raised_concord_1"},
                               {"to_army", "continental_militia"},
                               {"units", {{"militia", 1}}}}
                               .dump());
        CHECK(!s.armies.count("raised_concord_1"));
        CHECK(pass_units(s.armies.at("continental_militia")) == before + 1);
        const int joined = pass_units(s.armies.at("continental_militia"));
        s = pass_order(s, json{{"order", "detach_company"},
                               {"army_id", "continental_militia"},
                               {"units", {{"militia", 1}}}}
                               .dump());
        std::string split;
        for (const auto& ak : s.armies)
            if (ak.second.role == "on_its_own") split = ak.first;
        CHECK(!split.empty());
        CHECK(s.armies.at(split).units.at("militia") == 1);
        CHECK(pass_units(s.armies.at("continental_militia")) == joined - 1);
        merge_idle_player_armies(s);
        CHECK(s.armies.count(split) == 1);  // a split stands until it marches back in
        fold_player_army_into_neighbor(s, split, true);
        CHECK(s.armies.count(split) == 0);
        CHECK(pass_units(s.armies.at("continental_militia")) == joined);
        Army allied;
        allied.id = "stockbridge_company";
        allied.name = "Stockbridge Company";
        allied.faction = kContinental;
        allied.province = "concord";
        allied.previous_province = "concord";
        allied.role = "allied_company";
        allied.units["militia"] = 2;
        allied.allied["militia"] = 2;
        allied.morale = 60;
        s.armies[allied.id] = allied;
        std::string err;
        pass_order(s, json{{"order", "transfer_units"},
                           {"from_army", "stockbridge_company"},
                           {"to_army", "continental_militia"},
                           {"units", {{"militia", 1}}}}
                           .dump(),
                   &err);
        CHECK(err == "allied_company");
        Army turning;
        turning.id = "turning_back";
        turning.name = "Turning Back";
        turning.faction = kContinental;
        turning.province = "concord";
        turning.previous_province = "concord";
        turning.units["militia"] = 1;
        turning.returning_hours = 2;
        turning.returning_edge = "concord-lincoln";
        s.armies[turning.id] = turning;
        err.clear();
        pass_order(s, json{{"order", "transfer_units"},
                           {"from_army", "turning_back"},
                           {"to_army", "continental_militia"},
                           {"units", {{"militia", 1}}}}
                           .dump(),
                   &err);
        CHECK(err == "army_on_the_march");
        CHECK(s.armies.at("turning_back").units.at("militia") == 1);
    }
    // 7) 10 May: the Continental Congress sits; what the Provincial Congress left unvoted lapses.
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 5, 9, 23});
        s.pass.measures["stockbridge_company"] = MeasureState{};
        s.pass.measures["raise_the_army"].status = "passed";
        s.pass.measures["call_on_neighbours"].status = "passed";
        s.pass.measures.erase("stockbridge_company");
        CampaignState before = s;
        before.pass.measures["stockbridge_company"].retry_hour = before.now.hour_index + 24 * 400;  // held off the floor
        before = advance_hour(before).state;
        const json floor = json::parse(politics_json(before));
        CHECK(floor.at("body").at("name").get<std::string>() == "Continental Congress");
        CHECK(floor.at("blocs")[0].at("name").get<std::string>() == "New England");
        CHECK(floor.at("measures").size() == 7);
        CHECK(before.pass.measures.at("stockbridge_company").status == "lapsed");
        CHECK(unity(before) == 65);  // 30, -5, 0
        std::string err;
        pass_order(before, order_json({{"order", "lobby"}, {"action", "write"}, {"bloc", "committee_men"}}), &err);
        CHECK(err == "unknown_bloc");
    }
    // 8) The Continental measures: the army adopted (Congress pays part), the commander named
    //    (Washington after 17 days), bills of credit (gold now, prices later), the powder committee.
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 6, 13, 23});
        s.seed = pass_seed;
        for (const char* b : {"new_england", "middle_colonies", "southern_colonies"}) s.pass.bloc_lean[b] = 100;
        const json budget0 = json::parse(treasury_json(s));
        // Whatever the seed, with every bloc at 95 in 100 these pass on almost every draw; take a seed where all do.
        uint64_t seed = 0;
        for (uint64_t k = 1; k <= 60 && !seed; ++k) {
            CampaignState t = s;
            t.seed = k;
            for (int i = 0; i < 24 * 10; ++i) t = advance_hour(t).state;
            if (t.pass.measures["adopt_the_army"].status == "passed" && t.pass.measures["commander_in_chief"].status == "passed" &&
                t.pass.measures["bills_of_credit"].status == "passed" && t.pass.measures["rifle_companies"].status == "passed")
                seed = k;
        }
        CHECK(seed != 0);
        s.seed = seed;
        const int gold0 = s.player_gold;
        const int south0 = 100;
        for (int i = 0; i < 24 * 2 + 2; ++i) s = advance_hour(s).state;  // to 16 June
        CHECK(s.pass.measures.at("adopt_the_army").status == "passed");
        CHECK(s.pass.upkeep_share_pct.at(kContinental) == 40);
        const json budget1 = json::parse(treasury_json(s));
        auto pay = [](const json& b) {
            for (const auto& l : b.at("expenses"))
                if (l.at("label").get<std::string>().find("Army pay") != std::string::npos) return -l.at("gold").get<int>();
            return 0;
        };
        CHECK(pay(budget0) > 0 && pay(budget1) == pay(budget0) - pay(budget0) * 40 / 100);
        CHECK(s.pass.measures.at("commander_in_chief").status == "passed");
        CHECK(s.pass.bloc_lean.at("southern_colonies") == south0);  // already at the top: clamped
        CHECK(!s.pass.persons["george_washington"].unlocked);
        std::string err;
        pass_order(s, order_json({{"order", "assign_person"}, {"person", "george_washington"}, {"role", "envoy"}}), &err);
        CHECK(err == "person_not_available");
        for (int i = 0; i < 24 * 17; ++i) s = advance_hour(s).state;  // 2 July
        CHECK(to_ymdh(s.now).m == 7 && to_ymdh(s.now).d <= 3);
        CHECK(s.pass.persons.at("george_washington").unlocked);
        s.armies.at("continental_militia").province = "charlestown";
        // Colonel Barrett still has the militia: a second general is refused until he is relieved.
        CHECK(validate_campaign_order(s, order_json({{"order", "form_army"}, {"person", "george_washington"}, {"province", "charlestown"}})) == "army_has_general");
        s = relieved(s);
        s = pass_order(s, order_json({{"order", "form_army"}, {"person", "george_washington"}, {"province", "charlestown"}}));
        CHECK(general_of(s, "continental_militia") == "george_washington");
        // Bills of credit: +120 on 22 June, and prices a quarter higher 90 days on.
        CHECK(s.pass.measures.at("bills_of_credit").status == "passed");
        bool gold_line = false;
        for (const auto& ch : s.resource_changes) gold_line = gold_line || (ch.reason.find("Emit bills of credit passes") != std::string::npos && ch.gold == 120);
        CHECK(gold_line && s.player_gold >= gold0);
        CHECK(s.pass.price_rise_pct == 0 && purchase_cost_now(s, 0, 20, 0, 0, 0, 0, 0) == purchase_cost(0, 20, 0, 0, 0, 0, 0));
        s.treaties.push_back({"ceasefire", 24 * 500, "", ""});
        s.now = GameTime{from_ymdh({1775, 9, 20, 0}).hour_index - 1};
        s = advance_hour(s).state;
        CHECK(s.pass.price_rise_pct == 25);
        CHECK(purchase_cost_now(s, 0, 20, 0, 0, 0, 0, 0) == (purchase_cost(0, 20, 0, 0, 0, 0, 0) * 125 + 99) / 100);
        // The rifle companies marched in at the start of August: allied units.
        int rifles = 0, allied = 0;
        for (const auto& ak : s.armies)
            if (ak.second.faction == kContinental) {
                rifles += (ak.second.units.count("rifleman") ? ak.second.units.at("rifleman") : 0);
                allied += (ak.second.allied.count("rifleman") ? ak.second.allied.at("rifleman") : 0);
            }
        CHECK(rifles == 3 && allied == 3);
        // The powder committee (18 September): 3 powder a month at Cambridge from 1 October.
        s.pass.measures["powder_committee"].status = "passed";
        s.pass.grants.push_back({"A committee to import powder", kContinental, "cambridge", 3, 0, 0});
        const int powder = province_ledger(s, "cambridge").powder;
        s.now = GameTime{from_ymdh({1775, 10, 1, 0}).hour_index - 1};
        s = advance_hour(s).state;
        CHECK(province_ledger(s, "cambridge").powder >= powder + 3);
    }
    // 9) Unity under its floor: the troops of other colonies begin to go home, one unit a day.
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 7, 1, 0});
        s.armies.at("continental_militia").units["line_infantry"] += 4;
        s.armies.at("continental_militia").allied["line_infantry"] = 4;
        const int before = s.armies.at("continental_militia").units.at("line_infantry");
        CampaignState calm = s;
        for (int i = 0; i < 24 * 3; ++i) calm = advance_hour(calm).state;
        CHECK(calm.armies.at("continental_militia").units.at("line_infantry") == before);
        s.pass.bloc_lean["new_england"] = 60;
        s.pass.bloc_lean["middle_colonies"] = -30;
        CHECK(unity(s) == 10);
        for (int i = 0; i < 24 * 3; ++i) s = advance_hour(s).state;
        CHECK(s.armies.at("continental_militia").units.at("line_infantry") == before - 3);
        CHECK(s.armies.at("continental_militia").allied.at("line_infantry") == 1);
        bool told = false;
        for (const auto& ch : s.resource_changes) told = told || ch.reason.find("Unity is low") != std::string::npos;
        CHECK(told);
    }
    // 10) The Stockbridge company joins the army in its town, stays marked allied, and goes home unpaid.
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 4, 30, 23});
        for (const char* b : {"committee_men", "country_towns", "seaport_merchants"}) s.pass.bloc_lean[b] = 100;
        s.player_gold = 100;
        uint64_t seed = 0;
        for (uint64_t k = 1; k <= 40 && !seed; ++k) {
            CampaignState t = s;
            t.seed = k;
            t.pass.measures["raise_the_army"].status = "passed";
            t.pass.measures["call_on_neighbours"].status = "passed";
            t = advance_hour(t).state;
            if (t.pass.measures["stockbridge_company"].status == "passed") seed = k;
        }
        CHECK(seed != 0);
        s.seed = seed;
        s.pass.measures["raise_the_army"].status = "passed";
        s.pass.measures["call_on_neighbours"].status = "passed";
        s.armies.at("continental_militia").province = "cambridge";
        const int militia_before = s.armies.at("continental_militia").units.count("militia")
                                       ? s.armies.at("continental_militia").units.at("militia") : 0;
        const int allied_before = s.armies.at("continental_militia").allied.count("militia")
                                      ? s.armies.at("continental_militia").allied.at("militia") : 0;
        s = advance_hour(s).state;
        // The company is raised and, the same hour, joins the army standing in the town.
        // The men stay marked allied, so they can be split off or sent home.
        CHECK(s.armies.count("stockbridge_company") == 0);
        CHECK(s.armies.at("continental_militia").units.at("militia") == militia_before + 2);
        CHECK(s.armies.at("continental_militia").allied.at("militia") == allied_before + 2);
        bool paid = false;
        for (const auto& ch : s.resource_changes) paid = paid || (ch.reason.find("Enlist the Stockbridge company passes") != std::string::npos && ch.gold == -12);
        CHECK(paid);
        for (int i = 0; i < 48; ++i) s = advance_hour(s).state;
        CHECK(s.armies.count("stockbridge_company") == 0);
        CHECK(s.armies.at("continental_militia").allied.at("militia") == allied_before + 2);
        std::string err;
        pass_order(s, order_json({{"order", "form_army"}, {"person", "william_heath"}, {"province", "cambridge"}, {"army_id", "stockbridge_company"}}), &err);
        CHECK(err == "no_army_here" || err == "no_muster");
        s.progress.pay_arrears_months = 1;
        for (int i = 0; i < 25; ++i) s = advance_hour(s).state;
        CHECK(s.armies.at("continental_militia").units.at("militia") == militia_before);
        CHECK(s.armies.at("continental_militia").allied.count("militia") == 0 || s.armies.at("continental_militia").allied.at("militia") == allied_before);
        bool went = false;
        for (const auto& ch : s.resource_changes) went = went || ch.reason.find("go home") != std::string::npos;
        CHECK(went);
    }
    // 11) A victory warms the blocs and earns a favor; a mission done earns one too.
    {
        CampaignState s = battle_at("lexington");
        s.pass.seeded = true;
        s.pass.ministry_confidence = 60;
        const CampaignState won = apply_battle_result(s, ended_with("lexington", kContinental, "army_broke"));
        CHECK(won.pass.favors == s.pass.favors + 1);
        CHECK(won.pass.bloc_lean.at("country_towns") == 5 + 6 && won.pass.bloc_lean.at("colonial_office") == 15 - 8);
        CHECK(won.pass.ministry_confidence == 52);
        const CampaignState lost = apply_battle_result(s, ended_with("lexington", kBritish, "army_broke"));
        CHECK(lost.pass.favors == s.pass.favors && lost.pass.bloc_lean.at("country_towns") == 5 - 8);
        CHECK(lost.pass.ministry_confidence == 64);
        CampaignState m = pass_campaign(YMDH{1775, 4, 20, 0});
        const int favors = m.pass.favors;
        m.progress.missions["p1_save_the_stores"].status = "complete";
        m = advance_hour(m).state;
        m = advance_hour(m).state;
        CHECK(m.pass.favors == favors + 1);
    }
    // 12) The British side: the reinforcement already at sea lands on 25 May with three generals.
    {
        CampaignState s = pass_campaign(YMDH{1775, 4, 20, 0});
        CHECK(s.pass.measures.at("reinforcement").status == "sent");
        const int garrison = pass_units(s.armies.at("british_garrison"));
        CHECK(!s.pass.persons["william_howe"].unlocked);
        s.now = GameTime{from_ymdh({1775, 5, 25, 0}).hour_index - 1};
        s = advance_hour(s).state;
        CHECK(s.pass.measures.at("reinforcement").status == "passed");
        int british = 0;
        for (const auto& ak : s.armies)
            if (ak.second.faction == kBritish && ak.second.province == "boston") british += pass_units(ak.second);
        CHECK(british >= garrison + 8);
        for (const char* g : {"william_howe", "henry_clinton", "john_burgoyne"}) CHECK(s.pass.persons.at(g).unlocked);
        bool news = false;
        for (const auto& ch : s.resource_changes) news = news || ch.reason.find("British news: Answer from London: Reinforcement with three generals is granted") != std::string::npos;
        CHECK(news);
    }
    // 13) A dispatch as the British player sends it: 38 days out, voted in London, 38 days back.
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 6, 1, 0});
        s.player_faction = kBritish;
        s.pass.pending_decision.clear();
        const json floor = json::parse(politics_json(s));
        CHECK(floor.at("body").at("button").get<std::string>() == "Whitehall" && floor.at("body").at("dispatch").get<bool>());
        CHECK(floor.contains("ministry_confidence") && floor.at("ministry_confidence").get<int>() == 60);
        std::string err;
        pass_order(s, order_json({{"order", "vote_now"}, {"measure", "provision_convoy"}}), &err);
        CHECK(err == "is_a_dispatch");
        for (const char* b : {"colonial_office", "admiralty", "treasury"}) s.pass.bloc_lean[b] = 100;
        uint64_t seed = 0;
        for (uint64_t k = 1; k <= 40 && !seed; ++k) {
            CampaignState t = s;
            t.seed = k;
            t = pass_order(t, order_json({{"order", "send_dispatch"}, {"measure", "provision_convoy"}}));
            t.now = GameTime{t.pass.measures.at("provision_convoy").vote_hour - 1};
            t = advance_hour(t).state;
            if (t.pass.measures.at("provision_convoy").voted_yes) seed = k;
        }
        CHECK(seed != 0);
        s.seed = seed;
        const int sent = static_cast<int>(s.now.hour_index);
        const int provisions = province_ledger(s, "boston").provisions;
        s = pass_order(s, order_json({{"order", "send_dispatch"}, {"measure", "provision_convoy"}}));
        const MeasureState ms = s.pass.measures.at("provision_convoy");
        CHECK(ms.status == "sent" && ms.vote_hour == sent + 24 * 38 && ms.answer_hour == sent + 24 * 76);
        pass_order(s, order_json({{"order", "send_dispatch"}, {"measure", "provision_convoy"}}), &err);
        CHECK(err == "already_sent");
        CHECK(measure(s, "provision_convoy").at("when").get<std::string>().find("At sea") == 0);
        // Day 37: nothing yet. Day 38: London has voted, Boston does not know. Day 75: still not. Day 76: landed.
        s.pass.pending_decision.clear();
        auto to_hour = [&](int64_t hour) {
            s.now = GameTime{hour - 1};
            s.pass.pending_decision.clear();
            s = advance_hour(s).state;
            s.pass.pending_decision.clear();
        };
        to_hour(ms.vote_hour - 24);
        CHECK(!s.pass.measures.at("provision_convoy").voted_yes && s.pass.measures.at("provision_convoy").status == "sent");
        to_hour(ms.vote_hour);
        CHECK(s.pass.measures.at("provision_convoy").voted_yes && s.pass.measures.at("provision_convoy").status == "sent");
        CHECK(province_ledger(s, "boston").provisions <= provisions + 12);  // only what the sea subsidy may have landed
        CHECK(measure(s, "provision_convoy").at("when").get<std::string>().find("Answer at sea") == 0);
        to_hour(ms.answer_hour - 1);
        CHECK(s.pass.measures.at("provision_convoy").status == "sent");
        const int before_landing = province_ledger(s, "boston").provisions;
        to_hour(ms.answer_hour);
        CHECK(province_ledger(s, "boston").provisions == before_landing + 30);
        CHECK(s.pass.measures.at("provision_convoy").times == 1);
        // Repeatable, after its delay.
        pass_order(s, order_json({{"order", "send_dispatch"}, {"measure", "provision_convoy"}}), &err);
        CHECK(err == "too_early" || err == "not_before_it_returns");
        to_hour(ms.answer_hour + 24 * 40);
        pass_order(s, order_json({{"order", "send_dispatch"}, {"measure", "provision_convoy"}}), &err);
        CHECK(err.empty());
    }
    // 14) The ministry's confidence falls with defeats; under the floor the commander is recalled
    //     and the next general at Boston takes the command (and the parley).
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 6, 1, 0});
        CHECK(s.factions.at(kBritish).envoy_name == "General Thomas Gage");
        s.pass.ministry_confidence = 31;
        s = advance_hour(s).state;
        CHECK(s.pass.persons.at("thomas_gage").status.empty());
        CampaignState b = s;
        b.armies.at("continental_militia").province = "lexington";
        b.armies.at("british_regulars").province = "lexington";
        b.pending_battle = {true, "lexington", "british_regulars", "continental_militia"};
        b = apply_battle_result(b, ended_with("lexington", kContinental, "army_broke"));
        CHECK(b.pass.ministry_confidence == 23);
        b = advance_hour(b).state;
        CHECK(b.pass.persons.at("thomas_gage").status == "recalled" && b.pass.persons.at("thomas_gage").role.empty());
        CHECK(b.pass.persons.at("william_howe").role == "envoy");
        CHECK(b.factions.at(kBritish).envoy_name == "Major General William Howe");
        CHECK(b.pass.ministry_confidence == 55);
        bool told = false;
        for (const auto& ch : b.resource_changes) told = told || ch.reason.find("General Thomas Gage is recalled and Major General William Howe takes the command") != std::string::npos;
        CHECK(told);
        // Before the reinforcement there is nobody to take the command: he stays.
        CampaignState early = pass_campaign_decided(YMDH{1775, 5, 1, 0});
        early.pass.ministry_confidence = 10;
        early = advance_hour(early).state;
        CHECK(early.pass.persons["thomas_gage"].status.empty());
    }
    // 15) The pass state survives a save, and a campaign with it loaded is deterministic.
    {
        CampaignState s = pass_campaign_decided(YMDH{1775, 6, 20, 0});
        const CampaignState loaded = deserialize(serialize(s));
        CHECK(state_hash(loaded) == state_hash(s));
        CHECK(loaded.pass.measures.size() == s.pass.measures.size() && loaded.pass.bloc_lean == s.pass.bloc_lean);
        CampaignState a = s, b = loaded;
        for (int i = 0; i < 24 * 30; ++i) {
            a.pass.pending_decision.clear();
            b.pass.pending_decision.clear();
            a = advance_hour(a).state;
            b = advance_hour(b).state;
        }
        CHECK(state_hash(a) == state_hash(b));
    }
}

// Decisions: the clock stops, each option's cost and gain land at once, its consequence on its day.
static void test_pass_decisions() {
    CampaignPassData data;
    ensure_battle_rules();
    struct Case {
        const char* id;
        const char* side;
        YMDH at;
        const char* option[2];
        const char* person[2];
    };
    const Case cases[] = {
        {"which_heights", kContinental, {1775, 6, 13, 0}, {"charlestown", "dorchester"}, {"", ""}},
        {"canada_expedition", kContinental, {1775, 9, 5, 0}, {"send_expedition", "keep_the_men"}, {"benedict_arnold", ""}},
        {"reenlist_how", kContinental, {1775, 12, 1, 0}, {"bounty", "appeal"}, {"", "israel_putnam"}},
        {"assault_or_guns", kContinental, {1776, 2, 16, 0}, {"assault", "wait_for_guns"}, {"", ""}},
        {"take_the_heights_how", kBritish, {1775, 6, 17, 0}, {"frontal_assault", "land_behind"}, {"", ""}},
        {"burn_charlestown", kBritish, {1775, 6, 17, 1}, {"burn", "spare"}, {"", ""}},
        {"hold_or_sail", kBritish, {1775, 11, 10, 0}, {"hold", "sail"}, {"", ""}},
    };
    for (const Case& c : cases) {
        for (int k = 0; k < 2; ++k) {
            CampaignState s = quiet_campaign();
            s.ai_objective.clear();
            s.player_faction = c.side;
            s.player_gold = 200;
            s.progress.enlistment_warned = s.progress.enlistments_expired = true;  // these cases are not about enlistments
            // What each decision needs to be put.
            if (std::string(c.id) == "burn_charlestown") s.pass.decisions["take_the_heights_how"] = "frontal_assault";
            if (std::string(c.id) == "hold_or_sail") s.pass.flags.push_back("leave_to_evacuate");
            // Earlier decisions of this side are taken as answered, so only this one is put.
            for (const Case& earlier : cases) {
                if (&earlier == &c) break;
                if (std::string(earlier.side) == c.side && !s.pass.decisions.count(earlier.id)) s.pass.decisions[earlier.id] = earlier.option[0];
            }
            s.now = GameTime{from_ymdh(c.at).hour_index - 1};
            TickOutput out = advance_hour(s);
            s = out.state;
            if (s.pass.pending_decision != c.id) g_reporter()->message(std::string(c.id) + ": pending is '" + s.pass.pending_decision + "'");
            CHECK(s.pass.pending_decision == c.id);
            CHECK(std::find(out.interrupts.begin(), out.interrupts.end(), Interrupt::Decision) != out.interrupts.end());
            // The clock is stopped: no hour passes until it is answered.
            const TickOutput blocked = advance_hour(s);
            CHECK(blocked.state.now.hour_index == s.now.hour_index);
            CHECK(blocked.interrupts.size() == 1 && blocked.interrupts[0] == Interrupt::Decision);
            const json shown = json::parse(decision_json(s));
            CHECK(shown.at("pending").get<bool>() && shown.at("options").size() == 2);
            CHECK(!shown.at("title").get<std::string>().empty() && !shown.at("painting").get<std::string>().empty());
            for (const auto& o : shown.at("options")) {
                CHECK(!o.at("cost").get<std::string>().empty() && !o.at("gain").get<std::string>().empty());
                CHECK(o.at("later").get<std::string>().find("After ") == 0);
                for (const char* key : {"cost", "gain", "later", "label"}) CHECK(raw_ids_in(s, o.at(key).get<std::string>()).empty());
            }
            CHECK(raw_ids_in(s, shown.at("text").get<std::string>()).empty());
            const uint64_t before_hash = state_hash(s);
            const int gold = s.player_gold, support = s.local_support, favors = s.pass.favors, conf = s.pass.ministry_confidence;
            const size_t delayed = s.pass.delayed.size(), factors = s.pass.town_factors.size();
            std::map<std::string, int> forts;
            for (const auto& pk : s.provinces) forts[pk.first] = pk.second.fort_level;
            int units = 0, reenlisted = 0;
            for (const auto& ak : s.armies)
                if (ak.second.faction == c.side) units += pass_units(ak.second);
            json order{{"order", "decide"}, {"decision", c.id}, {"option", c.option[k]}};
            if (std::string(c.person[k]) != "") order["person"] = c.person[k];
            const CampaignState after = pass_order(s, order.dump());
            CHECK(state_hash(after) != before_hash);
            CHECK(after.pass.pending_decision.empty() && after.pass.decisions.at(c.id) == c.option[k]);
            CHECK(after.pass.delayed.size() == delayed + 1);  // the consequence is on its way
            // Something moved at once: gold, Support, favors, confidence, a fort, a factor, a lure, men, a flag.
            int units_after = 0;
            for (const auto& ak : after.armies)
                if (ak.second.faction == c.side) {
                    units_after += pass_units(ak.second);
                    for (const auto& r : ak.second.reenlisted) reenlisted += r.second;
                }
            bool fort_moved = false, morale_moved = false, stores_moved = false, blocs_moved = after.pass.bloc_lean != s.pass.bloc_lean;
            for (const auto& pk : after.provinces) fort_moved = fort_moved || pk.second.fort_level != forts[pk.first];
            for (const auto& ak : after.armies)
                if (s.armies.count(ak.first)) morale_moved = morale_moved || ak.second.morale != s.armies.at(ak.first).morale || ak.second.readiness != s.armies.at(ak.first).readiness;
            stores_moved = province_ledger(after, "boston").provisions != province_ledger(s, "boston").provisions;
            const bool moved = after.player_gold != gold || after.local_support != support || after.pass.favors != favors ||
                               after.pass.ministry_confidence != conf || fort_moved || after.pass.town_factors.size() != factors ||
                               !after.pass.lures.empty() || units_after != units || reenlisted > 0 || morale_moved || stores_moved ||
                               blocs_moved || after.progress.evacuation_hours_remaining > 0;
            if (!moved) g_reporter()->message(std::string(c.id) + "." + c.option[k] + ": nothing moved at once");
            CHECK(moved);
            // A committed person leaves the map and returns.
            if (std::string(c.person[k]) != "") {
                const PersonState st = after.pass.persons.at(c.person[k]);
                CHECK(st.status == "away" && st.role.empty() && st.away_until_hour > after.now.hour_index);
                const json people = json::parse(persons_json(after));
                bool away = false;
                for (const auto& p : people.at("persons"))
                    if (p.at("id").get<std::string>() == c.person[k]) away = p.at("where").get<std::string>().find("Away: back ") == 0 && !p.at("present").get<bool>();
                CHECK(away || std::string(c.side) != after.player_faction);
                CampaignState back = after;
                back.now = GameTime{st.away_until_hour - 1};
                back = advance_hour(back).state;
                CHECK(back.pass.persons.at(c.person[k]).status.empty());
            } else {
                std::string err;
                json with = order;
                with["person"] = "artemas_ward";
                pass_order(s, with.dump(), &err);
                const bool needs = shown.at("options")[k].at("commit_role").get<std::string>() != "";
                CHECK(needs || err == "no_person_needed");
            }
            // The consequence falls due on its day, with its own ledger line.
            CampaignState later = after;
            const DelayedEffect due = later.pass.delayed.back();
            CHECK(due.due_hour > later.now.hour_index);
            const size_t lines = later.resource_changes.size();
            (void)lines;
            later.now = GameTime{due.due_hour - 1};
            later.pass.decisions["__hold__"] = "";  // no other decision is put while this one is watched
            for (const Case& other : cases) later.pass.decisions[other.id] = later.pass.decisions.count(other.id) ? later.pass.decisions[other.id] : "x";
            const uint64_t pre = state_hash(later);
            later = advance_hour(later).state;
            CHECK(state_hash(later) != pre);
            bool still_waiting = false;
            for (const DelayedEffect& d : later.pass.delayed) still_waiting = still_waiting || (d.due_hour == due.due_hour && d.source == due.source);
            CHECK(!still_waiting);
            bool told = false;
            for (const auto& ch : later.resource_changes) told = told || ch.reason.find(due.source) != std::string::npos;
            const bool skipped = due.condition == "boston_not_taken" && later.provinces.at("boston").owner == c.side;
            CHECK(told || skipped || std::string(c.side) != later.player_faction);
            for (const auto& ch : later.resource_changes) CHECK(raw_ids_in(later, ch.reason).empty());
            // Not put twice.
            CHECK(json::parse(decision_json(after)).at("pending").get<bool>() == false);
        }
    }
    // Which heights: the fort is raised at once, and the British are drawn to Charlestown after.
    {
        CampaignState s = pass_campaign(YMDH{1775, 6, 13, 0});
        CHECK(s.pass.pending_decision == "which_heights");
        const int fort = s.provinces.at("dorchester").fort_level;
        s = pass_order(s, order_json({{"order", "decide"}, {"decision", "which_heights"}, {"option", "dorchester"}}));
        CHECK(s.provinces.at("dorchester").fort_level == fort + 1);
        CHECK(s.pass.lures.empty());
        s.pass.decisions["canada_expedition"] = "x";
        for (int i = 0; i < 24 * 5 + 1; ++i) s = advance_hour(s).state;
        bool drawn = false;
        for (const AiLure& l : s.pass.lures) drawn = drawn || (l.province == "charlestown" && l.faction == kBritish);
        CHECK(drawn);
        s.treaties.clear();
        CHECK(choose_ai_target(s, "british_regulars") == "charlestown");
    }
    // The other side's commander decides for himself and the clock does not stop for it.
    {
        CampaignState s = pass_campaign(YMDH{1775, 6, 16, 23});
        s.pass.decisions["which_heights"] = "charlestown";
        s.pass.pending_decision.clear();
        s.provinces.at("charlestown").fort_level = 1;
        const TickOutput out = advance_hour(s);
        CHECK(out.state.pass.decisions.count("take_the_heights_how") && out.state.pass.decisions.at("take_the_heights_how") == "frontal_assault");
        CHECK(out.state.pass.pending_decision.empty());
        CHECK(std::find(out.interrupts.begin(), out.interrupts.end(), Interrupt::Decision) == out.interrupts.end());
        const TickOutput next = advance_hour(out.state);
        CHECK(next.state.pass.decisions.count("burn_charlestown") && next.state.pass.decisions.at("burn_charlestown") == "burn");
        bool news = false;
        for (const auto& ch : next.state.resource_changes) news = news || ch.reason.find("British news: Burn Charlestown?") == 0;
        CHECK(news);
    }
    // The whole campaign with everything loaded: an idle player, twice, the same.
    {
        CampaignState s = fresh();
        s.player_gold = 120;
        const CampaignState a = run_idle_campaign(s, 24 * 400);
        const CampaignState b = run_idle_campaign(s, 24 * 400);
        CHECK(a.progress.outcome.decided);
        CHECK(state_hash(a) == state_hash(b));
        const YMDH end = to_ymdh(a.now);
        g_reporter()->message("idle campaign with people, lobbying and decisions: " + a.progress.outcome.winner + " by " + a.progress.outcome.reason + " on " +
                              std::to_string(end.y) + "-" + std::to_string(end.m) + "-" + std::to_string(end.d) + ", " +
                              std::to_string(a.progress.battles_fought) + " battles, hash " + state_hash_hex(a));
        for (const auto& ch : a.resource_changes) CHECK(raw_ids_in(a, ch.reason).empty());
    }
}

// M5: every answer to every letter does something, and says what it costs and gains. The case walks
// the whole catalog, the detached commander's letter, the aftermath letter and Knox's, takes each
// choice on a fresh copy and fails on one that leaves the campaign as it was. It prints the table.
struct LetterMark {
    int gold = 0, support = 0, attitude = 0, favors = 0, clerk = 0, debt = 0;
    long stores = 0, donor = 0, in_transit = 0, committed = 0;
    int morale = 0, readiness = 0, held = 0, factors = 0;
    std::string orders, knox;
    bool capacity = false;
};
static LetterMark letter_mark(const CampaignState& s, const std::string& letter_id) {
    LetterMark m;
    m.gold = s.player_gold;
    m.support = s.local_support;
    m.attitude = s.relations.count(kBritish) ? s.relations.at(kBritish) : 0;
    m.favors = s.pass.favors;
    m.clerk = s.staff.letters_processed_today;
    m.debt = s.progress.treasury_debt;
    for (const auto& st : s.province_stores)
        m.stores += st.second.powder * 1000L + st.second.cartridges * 100L + st.second.muskets * 10L + st.second.provisions;
    for (const auto& d : s.donor_reserves) m.donor += d.second.powder + d.second.arms + d.second.provisions;
    for (const auto& sh : s.shipments)
        if (sh.status != "delivered" && sh.status != "lost") m.in_transit += sh.powder * 1000L + sh.cartridges * 100L + sh.muskets * 10L + sh.provisions;
    for (const auto& c : s.staff.queue)
        if (c.id == letter_id) m.committed = c.committed_powder * 1000L + c.committed_arms * 10L + c.committed_provisions;
    for (const auto& ak : s.armies)
        if (ak.second.faction == s.player_faction) {
            m.morale += ak.second.morale;
            m.readiness += ak.second.readiness;
        }
    for (const auto& pc : s.progress.personnel_commitments) m.held += pc.count;
    m.factors = static_cast<int>(s.pass.town_factors.size());
    m.orders = s.progress.commander.objective + "/" + s.progress.commander.risk;
    m.knox = s.progress.knox.status;
    m.capacity = s.progress.capacity_level > 0 || s.progress.capacity_ready_hour > 0;
    return m;
}
// What changed, in words; empty when nothing did.
static std::string letter_diff(const LetterMark& a, const LetterMark& b, const CampaignState& after) {
    std::vector<std::string> parts;
    auto num = [&](const char* what, long x, long y) {
        if (x != y) parts.push_back(std::string(what) + " " + (y > x ? "+" : "") + std::to_string(y - x));
    };
    num("gold", a.gold, b.gold);
    num("Support", a.support, b.support);
    num("British attitude", a.attitude, b.attitude);
    num("favors", a.favors, b.favors);
    num("clerk slots used", a.clerk, b.clerk);
    num("debt", a.debt, b.debt);
    num("army morale", a.morale, b.morale);
    num("army readiness", a.readiness, b.readiness);
    num("militia units held back", a.held, b.held);
    num("donor reserve", a.donor, b.donor);
    if (a.stores != b.stores) parts.push_back("magazine stores changed now");
    if (a.in_transit != b.in_transit) parts.push_back("a shipment on the road");
    if (a.committed != b.committed) parts.push_back("stores to be delivered later");
    if (b.factors > a.factors) {
        for (size_t i = static_cast<size_t>(a.factors); i < after.pass.town_factors.size(); ++i) {
            const TownFactor& f = after.pass.town_factors[i];
            parts.push_back("battle factor at " + after.provinces.at(f.province).name + ": " + f.label + " " + (f.value > 0 ? "+" : "") + std::to_string(f.value) +
                            (f.one_battle ? " (next battle there)" : ""));
        }
    }
    if (a.orders != b.orders) parts.push_back("the commander's orders: " + b.orders);
    if (a.knox != b.knox) parts.push_back("the siege guns: " + b.knox);
    if (a.capacity != b.capacity) parts.push_back("procurement capacity begun");
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "; ") + p;
    return out;
}

static void test_letters_have_consequences() {
    ensure_opening_war_catalog();
    ensure_battle_rules();
    // The catalog as shipped (another case may have loaded it earlier: load it again to be sure).
    load_correspondence_catalog(read_text_file(g_data_dir() + "/opening_war_correspondence.json"));
    int choices_walked = 0, letters_walked = 0;
    auto walk = [&](const std::string& letter_name, const CampaignState& base, const std::string& letter_id, const std::string& choice,
                    const std::string& label, const std::string& summary) {
        const LetterMark before = letter_mark(base, letter_id);
        const CampaignState after = choose_logistics(base, letter_id, choice);
        const std::string diff = letter_diff(before, letter_mark(after, letter_id), after);
        ++choices_walked;
        g_reporter()->message("LETTER | " + letter_name + " | " + label + " | " + (diff.empty() ? "NO EFFECT" : diff));
        if (diff.empty()) g_reporter()->message("a choice with no effect: " + letter_name + " / " + choice);
        CHECK(!diff.empty());
        CHECK(!summary.empty());  // its cost and gain are printed on the button
        // Answered: the letter no longer waits, and answering it twice changes nothing more.
        bool waiting = false;
        for (const auto& c : after.staff.queue) waiting = waiting || (c.id == letter_id && c.awaiting_choice);
        CHECK(!waiting);
        const CampaignState twice = choose_logistics(after, letter_id, choice);
        CHECK(letter_diff(letter_mark(after, letter_id), letter_mark(twice, letter_id), twice).empty());
        // Set aside is the one answer that does nothing but file the letter.
        const CampaignState aside = choose_logistics(base, letter_id, "dismiss");
        const LetterMark am = letter_mark(aside, letter_id);
        CHECK(am.gold == before.gold && am.support == before.support && am.favors == before.favors && am.factors == before.factors);
    };
    // 1) Every catalog letter that asks a question, every choice.
    for (const CorrespondenceCatalogEntry& e : correspondence_catalog_entries()) {
        if (!e.activate_decisions || e.choices.empty()) continue;
        ++letters_walked;
        CampaignState s = fresh();
        s.ai_objective.clear();
        s.treaties.push_back({"ceasefire", 24 * 400, "", ""});
        s.progress.lexington_fought = true;
        s.progress.chapter_continental = true;
        s.progress.aftermath_pending = false;
        s.progress.concord_followup_seeded = true;
        s.pass.favors = 2;
        s.player_gold = 200;
        for (auto& ak : s.armies) ak.second.readiness = 70;  // below its ceiling, so a gain shows
        province_ledger(s, default_depot_province(s, kContinental)).provisions = 40;
        for (const auto& other : correspondence_catalog_entries()) {
            if (other.source_id != e.source_id) s.progress.received_source_ids.push_back(other.source_id);
            s.progress.resolved_event_ids.push_back(other.event_id);
        }
        s.progress.resolved_event_ids.erase(std::remove(s.progress.resolved_event_ids.begin(), s.progress.resolved_event_ids.end(), e.event_id),
                                            s.progress.resolved_event_ids.end());
        s.now = from_ymdh(e.eligible_from);
        clear_staff_desk(s);
        std::vector<Interrupt> ints;
        tick_logistics(s, ints);
        std::string id;
        for (const auto& c : s.staff.queue)
            if (c.source_id == e.source_id) id = c.id;
        if (id.empty()) g_reporter()->message("letter never arrived: " + e.source_id);
        CHECK(!id.empty());
        if (id.empty()) continue;
        for (const CorrespondenceChoiceDef& ch : e.choices) walk(e.source_id + " (" + e.author + ")", s, id, ch.id, ch.label, ch.effects_summary);
    }
    CHECK(letters_walked == 7);
    // 2) The detached commander's letter after the first fight: preserve, contest, escalate.
    {
        CampaignState s = fresh();
        s.ai_objective.clear();
        s.treaties.push_back({"ceasefire", 24 * 400, "", ""});
        s.progress.battles_fought = 1;
        s.progress.lexington_fought = true;
        s.progress.aftermath_pending = false;
        s.progress.concord_followup_seeded = true;
        for (auto& ak : s.armies) ak.second.readiness = 70;
        clear_staff_desk(s);
        std::vector<Interrupt> ints;
        tick_logistics(s, ints);
        std::string id;
        for (const auto& c : s.staff.queue)
            if (c.kind == "commander_report" && c.subject == "Detached command: timing at Concord") id = c.id;
        CHECK(!id.empty());
        const auto choices = commander_letter_choices();
        CHECK(choices.size() == 3 && commander_letter_province() == "concord");
        for (const CommanderLetterChoice& ch : choices) walk("Detached command: timing at Concord", s, id, ch.id, ch.label, ch.effects_summary);
        ++letters_walked;
        // Preserve: at the next Concord battle the company withdraws early and half the magazine is saved.
        CampaignState preserve = choose_logistics(s, id, "preserve_force");
        CHECK(preserve.armies.at("continental_militia").readiness == s.armies.at("continental_militia").readiness + 10);
        preserve.treaties.clear();
        preserve.armies.at("continental_militia").province = "concord";
        preserve.armies.at("british_regulars").province = "concord";
        preserve.armies.at("british_regulars").previous_province = "lincoln";
        preserve.pending_battle = {true, "concord", "british_regulars", "continental_militia"};
        const BattleSetup pre = build_battle_setup(preserve, "concord");
        bool early = false, saved = false;
        for (const SideFactor& f : pre.sides.at(kContinental).factors) {
            if (f.label == "withdraw_early" && f.value == 50) early = f.source.find("Letter, Detached command: timing at Concord: Preserve the company") == 0;
            if (f.label == "magazine_saved" && f.value == 50) saved = true;
            CHECK(raw_ids_in(preserve, f.source).empty());
        }
        CHECK(early && saved);
        for (const SideFactor& f : pre.sides.at(kBritish).factors) CHECK(f.label != "withdraw_early" && f.label != "magazine_saved");
        const ProvinceStores mag = province_ledger(preserve, "concord");
        CHECK(mag.cartridges >= 2);
        const CampaignState lost = apply_battle_result(preserve, ended_with("concord", kBritish, "army_broke"));
        CHECK(lost.provinces.at("concord").owner == kBritish);
        // The same fight with no such orders loses the whole magazine with the town.
        CampaignState plain = preserve;
        plain.pass.town_factors.clear();
        const CampaignState all_lost = apply_battle_result(plain, ended_with("concord", kBritish, "army_broke"));
        const ProvinceStores left = province_ledger(lost, "concord"), all = province_ledger(all_lost, "concord");
        CHECK(all.cartridges > 0 && left.cartridges == all.cartridges - all.cartridges / 2);
        CHECK(left.powder == all.powder - all.powder / 2 && left.muskets == all.muskets - all.muskets / 2);
        bool shipped = false, told = false;
        for (const auto& sh : lost.shipments)
            shipped = shipped || (sh.source_province == "concord" && sh.dest_province == default_depot_province(lost, kContinental) &&
                                  sh.cartridges == all.cartridges / 2 && sh.powder == all.powder / 2 && sh.status == "in_transit");
        for (const auto& l : lost.progress.last_battle_report.map_changes) {
            told = told || l.find("Carried off before Concord fell") == 0;
            CHECK(raw_ids_in(lost, l).empty());
        }
        CHECK(shipped && told);
        for (const auto& sh : all_lost.shipments) CHECK(sh.source_province != "concord");
        for (const TownFactor& f : lost.pass.town_factors) CHECK(f.province != "concord");  // spent by the battle
        // It arrives at headquarters twelve hours on.
        {
            CampaignState later = lost;
            const int at_hq = province_ledger(later, default_depot_province(later, kContinental)).cartridges;
            later.treaties.push_back({"ceasefire", 24 * 30, "", ""});
            for (int i = 0; i < 13; ++i) later = advance_hour(later).state;
            CHECK(province_ledger(later, default_depot_province(later, kContinental)).cartridges >= at_hq + all.cartridges / 2);
        }
        // The sim's own resolver: withdrawn early the company loses half what it would have.
        auto units = [](const CampaignState& st) {
            return st.armies.count("continental_militia") ? pass_units(st.armies.at("continental_militia")) : 0;
        };
        const int held = units(preserve);
        CampaignArc arc;  // the resolver reads its loss percentages from the campaign rules
        BattleResult ra = auto_resolve_battle(preserve, build_battle_setup(preserve, "concord"));
        BattleResult rb = auto_resolve_battle(plain, build_battle_setup(plain, "concord"));
        auto lost_units = [](const BattleResult& r) {
            int n = 0;
            if (r.losses.count(kContinental))
                for (const auto& kv : r.losses.at(kContinental)) n += kv.second;
            return n;
        };
        g_reporter()->message("Concord by the resolver: " + std::to_string(lost_units(ra)) + " units lost withdrawing early, " + std::to_string(lost_units(rb)) +
                              " without those orders (of " + std::to_string(held) + ")");
        CHECK(lost_units(ra) < lost_units(rb));
        // Contest: a morale bonus in that fight, and it stands to the end with higher losses.
        CampaignState contest = choose_logistics(s, id, "contest_crossing");
        contest.treaties.clear();
        contest.armies.at("continental_militia").province = "concord";
        contest.armies.at("british_regulars").province = "concord";
        contest.armies.at("british_regulars").previous_province = "lincoln";
        contest.pending_battle = {true, "concord", "british_regulars", "continental_militia"};
        const BattleSetup con = build_battle_setup(contest, "concord");
        CHECK(con.sides.at(kContinental).morale == std::min(100, build_battle_setup(plain, "concord").sides.at(kContinental).morale + 5 + 8));
        bool stands = false;
        for (const SideFactor& f : con.sides.at(kContinental).factors) stands = stands || (f.label == "stand_fast" && f.value == 125);
        CHECK(stands);
        BattleResult rc = auto_resolve_battle(contest, con);
        if (rc.winner == kBritish) CHECK(lost_units(rc) > lost_units(rb));
        // Escalate: a clerk slot and Support, and the old risk to the ground stays.
        const CampaignState esc = choose_logistics(s, id, "escalate");
        CHECK(esc.local_support == s.local_support + 4);
        CHECK(esc.staff.letters_processed_today == s.staff.letters_processed_today + 1);
        CHECK(esc.progress.commander.objective == "hold_stores");
    }
    // 3) The aftermath letter (purchase, request aid, requisition, capacity) and Knox's.
    {
        CampaignState s = fresh();
        s.ai_objective.clear();
        s.progress.battles_fought = 1;
        s.progress.lexington_fought = true;
        s.progress.aftermath_pending = true;
        s.progress.commander.last_request = "answered";  // only the aftermath letter is on the desk
        clear_staff_desk(s);
        std::vector<Interrupt> ints;
        tick_logistics(s, ints);
        std::string id;
        for (const auto& c : s.staff.queue)
            if (c.kind == "request_assistance" && c.awaiting_choice) id = c.id;
        CHECK(!id.empty());
        if (!id.empty()) {
            ++letters_walked;
            for (const char* choice : {"purchase", "request_assistance", "requisition", "capacity_investment"})
                walk("After the battle: supply", s, id, choice, choice, aftermath_choice_preview(s, choice));
        }
        CampaignState k = fresh();
        k.ai_objective.clear();
        k.progress.chapter_continental = k.progress.chapter_washington = true;
        k.now = from_ymdh(YMDH{1775, 12, 17, 2});
        clear_staff_desk(k);
        ints.clear();
        tick_logistics(k, ints);
        std::string kid;
        for (const auto& c : k.staff.queue)
            if (c.kind == "knox_support") kid = c.id;
        CHECK(!kid.empty());
        if (!kid.empty()) {
            ++letters_walked;
            walk("Knox expedition: continued support", k, kid, "knox_support", "Support the expedition", "80 gold for the guns of Ticonderoga");
        }
    }
    g_reporter()->message("letters walked: " + std::to_string(letters_walked) + ", choices: " + std::to_string(choices_walked));
    CHECK(letters_walked == 10 && choices_walked == 29);
}

// Letters L3, L5 and L7 kept their numbers in the sim's code until 2026-11-15 (user decision). Now
// each number is read from the catalog: the case takes every such choice as shipped and checks the
// state against the catalog's own values, then loads a catalog with the numbers changed and checks
// the sim follows the file, not a constant.
static void test_letter_numbers_are_data() {
    ensure_opening_war_catalog();
    const std::string shipped = read_text_file(g_data_dir() + "/opening_war_correspondence.json");
    load_correspondence_catalog(shipped);
    CHECK(shipped.find("hardcoded") == std::string::npos);
    // A campaign with one letter on the desk, as letters_have_consequences sets it.
    auto desk = [](const std::string& source_id, std::string& letter_id) {
        const CorrespondenceCatalogEntry* e = find_correspondence_entry(source_id);
        CampaignState s = fresh();
        s.ai_objective.clear();
        s.treaties.push_back({"ceasefire", 24 * 400, "", ""});
        s.progress.lexington_fought = true;
        s.progress.chapter_continental = true;
        s.progress.aftermath_pending = false;
        s.progress.concord_followup_seeded = true;
        s.player_gold = 200;
        for (auto& ak : s.armies) ak.second.readiness = 70;
        province_ledger(s, default_depot_province(s, kContinental)).provisions = 40;
        for (const auto& other : correspondence_catalog_entries()) {
            if (other.source_id != source_id) s.progress.received_source_ids.push_back(other.source_id);
            if (other.event_id != e->event_id) s.progress.resolved_event_ids.push_back(other.event_id);
        }
        s.now = from_ymdh(e->eligible_from);
        clear_staff_desk(s);
        std::vector<Interrupt> ints;
        tick_logistics(s, ints);
        letter_id.clear();
        for (const auto& c : s.staff.queue)
            if (c.source_id == source_id) letter_id = c.id;
        CHECK(!letter_id.empty());
        return s;
    };
    auto letter_of = [](const CampaignState& s, const std::string& id) {
        for (const auto& c : s.staff.queue)
            if (c.id == id) return c;
        return CorrespondenceItem{};
    };
    auto effects_of = [](const std::string& source_id, const std::string& choice) {
        for (const auto& ch : find_correspondence_entry(source_id)->choices)
            if (ch.id == choice) return ch.effects;
        CHECK(false);
        return CorrespondenceChoiceEffects{};
    };
    // One choice against the effects the catalog gives it: every field of the closed set that moves
    // stores, gold, men, the donor reserve or the offensive.
    int walked = 0;
    auto follows_catalog = [&](const std::string& source_id, const std::string& choice) {
        std::string id;
        const CampaignState before = desk(source_id, id);
        if (id.empty()) return;
        const CorrespondenceChoiceEffects fx = effects_of(source_id, choice);
        CHECK(fx.any());
        const CampaignState after = choose_logistics(before, id, choice);
        const CorrespondenceItem letter = letter_of(after, id);
        const std::string depot = default_depot_province(before, kContinental);
        const bool later = fx.delivery_hours > 0 && (fx.provisions > 0 || fx.powder > 0);
        ++walked;
        const std::string name = source_id + "." + choice;
        auto say = [&](bool ok, const std::string& what) {
            if (!ok) g_reporter()->message(name + ": " + what + " does not follow the catalog");
            CHECK(ok);
        };
        say(after.player_gold == before.player_gold + fx.gold, "gold");
        say(after.local_support == before.local_support + fx.local_support, "Support");
        say(after.staff.letters_processed_today == before.staff.letters_processed_today + fx.clerk_load, "clerk slots");
        say(province_ledger(after, depot).provisions == province_ledger(before, depot).provisions + fx.provisions_now + (later ? 0 : fx.provisions),
            "provisions now");
        say(province_ledger(after, depot).powder == province_ledger(before, depot).powder + (later ? 0 : fx.powder), "powder now");
        say(letter.committed_provisions == (later ? fx.provisions : 0) && letter.committed_powder == (later ? fx.powder : 0), "what is committed");
        if (later) say(letter.resolve_on_hour == before.now.hour_index + fx.delivery_hours && letter.status == "approved", "the delivery hour");
        say(static_cast<int>(after.progress.personnel_commitments.size()) == (fx.hold_count > 0 ? 1 : 0), "units held");
        if (fx.hold_count > 0 && !after.progress.personnel_commitments.empty()) {
            const PersonnelCommitment& pc = after.progress.personnel_commitments[0];
            say(pc.army_id == fx.hold_army && pc.unit_class == fx.hold_class && pc.count == fx.hold_count && pc.reason == fx.hold_reason &&
                    pc.release_on_hour == before.now.hour_index + fx.hold_hours,
                "the units held and for how long");
        }
        if (fx.donor_powder > 0) {
            say(after.donor_reserves.at(fx.donor_reserve).powder == before.donor_reserves.at(fx.donor_reserve).powder - fx.donor_powder, "the donor reserve");
            say(after.shipments.size() == before.shipments.size() + 1 && after.shipments.back().powder == fx.donor_powder &&
                    after.shipments.back().deliver_on_hour == before.now.hour_index + fx.donor_hours && after.shipments.back().dest_province == depot,
                "the shipment");
        } else {
            say(after.shipments.size() == before.shipments.size(), "no shipment");
        }
        say(after.progress.offensive_postponed == fx.postpone_offensive, "the offensive");
        for (const auto& ak : before.armies) {
            if (ak.second.faction != kContinental) continue;
            say(after.armies.at(ak.first).morale == ak.second.morale + fx.army_morale && after.armies.at(ak.first).readiness == ak.second.readiness + fx.army_readiness,
                "army morale and readiness");
        }
    };
    const std::pair<const char*, const char*> moved[] = {
        {"L3", "purchase_provisions"}, {"L3", "shared_lodging"}, {"L3", "retain_workers"},
        {"L5", "guarded_removal"},     {"L5", "negotiate_local"}, {"L5", "evacuate_preserve_boats"},
        {"L7", "request_transfer"},    {"L7", "fund_procurement"}, {"L7", "conserve_ammunition"}};
    for (const auto& m : moved) follows_catalog(m.first, m.second);
    CHECK(walked == 9);
    // The numbers as shipped are the ones the code used to hold.
    CHECK(effects_of("L3", "purchase_provisions").gold == -25 && effects_of("L3", "retain_workers").hold_count == 3 && effects_of("L3", "retain_workers").hold_hours == 72);
    CHECK(effects_of("L5", "guarded_removal").provisions_now == -4 && effects_of("L5", "guarded_removal").hold_count == 2 && effects_of("L5", "guarded_removal").hold_hours == 48);
    CHECK(effects_of("L7", "request_transfer").donor_powder == 8 && effects_of("L7", "request_transfer").donor_hours == 12 && effects_of("L7", "fund_procurement").gold == -35);

    // The same catalog with other numbers: the sim does what the file says.
    {
        json cat = json::parse(shipped);
        for (auto& e : cat.at("entries")) {
            if (!e.contains("choices")) continue;
            for (auto& ch : e.at("choices")) {
                const std::string id = ch.at("id").get<std::string>();
                if (id == "purchase_provisions") ch["effects"] = {{"gold", -40}, {"provisions", 11}, {"delivery_hours", 5}};
                if (id == "retain_workers") ch["effects"]["hold_units"]["count"] = 2, ch["effects"]["hold_units"]["hours"] = 30;
                if (id == "guarded_removal") ch["effects"]["provisions_now"] = -7;
                if (id == "request_transfer") ch["effects"]["donor_transfer"] = {{"reserve", "ny_powder"}, {"powder", 5}, {"hours", 20}};
                if (id == "fund_procurement") ch["effects"] = {{"gold", -50}, {"powder", 9}, {"delivery_hours", 3}};
            }
        }
        load_correspondence_catalog(cat.dump());
        std::string id;
        CampaignState s = desk("L3", id);
        const std::string depot = default_depot_province(s, kContinental);
        const int gold0 = s.player_gold, prov0 = province_ledger(s, depot).provisions;
        CampaignState buy = choose_logistics(s, id, "purchase_provisions");
        CHECK(buy.player_gold == gold0 - 40 && province_ledger(buy, depot).provisions == prov0);
        for (int i = 0; i < 4; ++i) buy = advance_hour(buy).state;
        CHECK(province_ledger(buy, depot).provisions == prov0);  // four hours: not yet
        for (int i = 0; i < 2; ++i) buy = advance_hour(buy).state;
        CHECK(province_ledger(buy, depot).provisions >= prov0 + 11 - 2);  // delivered (the army eats meanwhile)
        const CampaignState held = choose_logistics(s, id, "retain_workers");
        CHECK(held.progress.personnel_commitments.size() == 1 && held.progress.personnel_commitments[0].count == 2 &&
              held.progress.personnel_commitments[0].release_on_hour == s.now.hour_index + 30);
        CampaignState l5 = desk("L5", id);
        const int prov5 = province_ledger(l5, depot).provisions;
        CHECK(province_ledger(choose_logistics(l5, id, "guarded_removal"), depot).provisions == prov5 - 7);
        CampaignState l7 = desk("L7", id);
        const int donor0 = l7.donor_reserves.at("ny_powder").powder;
        const CampaignState sent = choose_logistics(l7, id, "request_transfer");
        CHECK(sent.donor_reserves.at("ny_powder").powder == donor0 - 5 && sent.shipments.back().powder == 5 &&
              sent.shipments.back().deliver_on_hour == l7.now.hour_index + 20);
        CHECK(choose_logistics(l7, id, "fund_procurement").player_gold == l7.player_gold - 50);
        // A reserve that holds less sends what it has and says so; an empty one refuses and takes nothing.
        CampaignState thin = l7;
        thin.donor_reserves.at("ny_powder").powder = 3;
        const CampaignState part = choose_logistics(thin, id, "request_transfer");
        CHECK(part.donor_reserves.at("ny_powder").powder == 0 && part.shipments.back().powder == 3);
        CHECK(letter_of(part, id).reason.find("could spare only 3 powder") != std::string::npos);
        CampaignState empty = l7;
        empty.donor_reserves.at("ny_powder").powder = 0;
        const CampaignState refused = choose_logistics(empty, id, "request_transfer");
        CHECK(letter_of(refused, id).status == "refused" && refused.shipments.size() == empty.shipments.size());
        // And a purchase the treasury cannot meet is refused before anything is spent.
        CampaignState poor = l7;
        poor.player_gold = 49;
        const CampaignState no = choose_logistics(poor, id, "fund_procurement");
        CHECK(no.player_gold == 49 && letter_of(no, id).status == "refused" && letter_of(no, id).committed_powder == 0);
    }
    load_correspondence_catalog(shipped);  // as shipped again, for the cases that follow
    CHECK(effects_of("L3", "purchase_provisions").gold == -25);
}
