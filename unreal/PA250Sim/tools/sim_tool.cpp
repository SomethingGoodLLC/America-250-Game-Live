// pa250sim_tool: sim-only runs for checks and tuning. Never used by the game.
//   pa250sim_tool idle-run [--data DIR] [--out SAVE.json] [--seed N] [--max-hours N]
//       A player who never acts, across the campaign window. Battles are settled by
//       auto_resolve_battle. Writes the final save and prints the outcome and its hash.
//   pa250sim_tool income-report [--data DIR] [--months N]
//       The opening holdings left alone under a truce (no campaign arc, no battles): the treasury
//       on the 1st of each month, with what came in and what went out.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "nlohmann/json.hpp"
#include "pa250/sim.hpp"

using namespace pa250;

namespace {

std::string arg(int argc, char** argv, const char* name, const std::string& fallback) {
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return fallback;
}

bool load_optional(const std::string& path, void (*fn)(const std::string&)) {
    try {
        fn(read_text_file(path));
        return true;
    } catch (const std::exception&) {
        std::fprintf(stderr, "warning: %s not loaded; built-in defaults apply\n", path.c_str());
        return false;
    }
}

void load_tuning(const std::string& dir) {
    load_optional(dir + "/logistics_tuning.json", &load_logistics_tuning);
    load_optional(dir + "/buildings_catalog.json", &load_buildings_catalog);
    load_optional(dir + "/province_sizes.json", &load_province_sizes);
    load_optional(dir + "/battle_rules.json", &load_battle_rules);
    load_optional(dir + "/opening_war_correspondence.json", &load_correspondence_catalog);
}

std::string date(const CampaignState& s) {
    const YMDH y = to_ymdh(s.now);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02dh", y.y, static_cast<int>(y.m), static_cast<int>(y.d), static_cast<int>(y.h));
    return buf;
}

int idle_run(int argc, char** argv) {
    const std::string dir = arg(argc, argv, "--data", PA250_DATA_DIR);
    load_tuning(dir);
    load_campaign_rules(read_text_file(dir + "/campaign_rules_boston_1775.json"));
    const auto errors = load_missions(read_text_file(dir + "/missions_boston_1775.json"));
    for (const auto& e : errors) std::fprintf(stderr, "missions: %s\n", e.c_str());
    if (!campaign_rules_loaded() || !errors.empty()) {
        std::fprintf(stderr, "campaign rules or missions did not load\n");
        return 2;
    }
    CampaignState s = load_scenario(read_text_file(dir + "/boston1775.json"));
    const std::string seed = arg(argc, argv, "--seed", "");
    if (!seed.empty()) s.seed = std::strtoull(seed.c_str(), nullptr, 10);
    const int window = static_cast<int>(from_ymdh(campaign_rules().window_end).hour_index - s.now.hour_index) + 48;
    const int max_hours = std::atoi(arg(argc, argv, "--max-hours", std::to_string(window)).c_str());
    std::printf("idle run: seed %llu from %s, up to %d hours\n", static_cast<unsigned long long>(s.seed), date(s).c_str(), max_hours);
    const CampaignState end = run_idle_campaign(s, max_hours);
    const CampaignOutcome& o = end.progress.outcome;
    std::printf("ended %s: %s\n", date(end).c_str(),
                o.decided ? (o.winner + " win (" + o.reason + ")").c_str() : "NOT DECIDED");
    for (const auto& line : o.summary) std::printf("  %s\n", line.c_str());
    for (const auto& m : mission_defs()) {
        auto it = end.progress.missions.find(m.id);
        std::printf("  mission %-26s %s\n", m.id.c_str(), it == end.progress.missions.end() ? "locked" : it->second.status.c_str());
    }
    std::printf("final hash %s\n", state_hash_hex(end).c_str());
    const std::string out = arg(argc, argv, "--out", "");
    if (!out.empty() && !write_text_file(out, serialize(end))) {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 2;
    }
    return o.decided ? 0 : 1;
}

int income_report(int argc, char** argv) {
    const std::string dir = arg(argc, argv, "--data", PA250_DATA_DIR);
    load_tuning(dir);
    const int months = std::max(1, std::atoi(arg(argc, argv, "--months", "12").c_str()));
    CampaignState s = load_scenario(read_text_file(dir + "/boston1775.json"));
    // A truce for the whole report: nobody marches, nothing is fought, only the accounts move.
    s.treaties.push_back({"ceasefire", 24 * 31 * (months + 2), "", ""});
    std::printf("income report: the opening holdings left alone for %d months (truce, no purchases, no building)\n", months);
    std::printf("start %s: %d gold, projected net %+d a month\n", date(s).c_str(), s.player_gold, monthly_net_gold(s));
    {
        const nlohmann::json t = nlohmann::json::parse(treasury_json(s));
        for (const char* side : {"income", "expenses"})
            if (t.contains(side))
                for (const auto& line : t.at(side))
                    std::printf("    %-9s %-44s %+5d\n", side, line.value("label", "").c_str(), line.value("gold", 0));
    }
    std::printf("%-12s %8s %8s %8s %8s %8s\n", "month (1st)", "gold", "change", "debt", "arrears", "support");
    int last_gold = s.player_gold;
    int total = 0;
    for (int m = 0; m < months; ++m) {
        for (int guard = 0; guard < 24 * 32; ++guard) {
            s = advance_hour(s).state;
            if (s.pending_battle.active) s.pending_battle = PendingBattle{};
            const YMDH y = to_ymdh(s.now);
            if (y.d == 1 && y.h == 0) break;
        }
        const YMDH y = to_ymdh(s.now);
        const int change = s.player_gold - last_gold;
        total += change;
        std::printf("%04d-%02d       %8d %+8d %8d %8d %8d\n", y.y, static_cast<int>(y.m), s.player_gold, change,
                    s.progress.treasury_debt, s.progress.pay_arrears_months, s.local_support);
        last_gold = s.player_gold;
    }
    std::printf("average change %+d gold a month; projected net now %+d\n", total / months, monthly_net_gold(s));
    // Every level-1 building against the average month: the retune aims at about two months each.
    std::printf("level-1 buildings, gold and months of the average change:\n");
    for (const char* id : {"farm", "workshop", "depot", "fort", "powder_mill", "church", "militia_muster"}) {
        const int cost = building_cost(id, 0);
        std::printf("    %-16s %4d gold", id, cost);
        if (total > 0) std::printf("  %4.1f months", cost * static_cast<double>(months) / total);
        // Only the tracks have levels; the Meeting House and the Muster are built once.
        if (building_cost(id, 1) != cost) std::printf("   (level 2: %d, level 3: %d)", building_cost(id, 1), building_cost(id, 2));
        std::printf("\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "idle-run") == 0) return idle_run(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "income-report") == 0) return income_report(argc, argv);
    std::fprintf(stderr, "usage: pa250sim_tool idle-run [--data DIR] [--out SAVE.json] [--seed N] [--max-hours N]\n"
                         "       pa250sim_tool income-report [--data DIR] [--months N]\n");
    return 2;
}
