// Internal helpers shared by PA250Sim sources. All inline so one translation unit can include everything.
#pragma once
#include <algorithm>
#include <deque>
#include <set>

#include "pa250/sim.hpp"

namespace pa250 {
namespace detail {

inline int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline std::string other_faction(const std::string& f) {
    return f == kBritish ? std::string(kContinental) : std::string(kBritish);
}

// Words for player-facing text. Ids ("british", "line_infantry", "concord") never reach the player.
inline std::string faction_word(const std::string& f) {
    if (f == kBritish) return "British";
    if (f == kContinental) return "Patriot";
    return f.empty() ? std::string("nobody's") : f;
}
inline std::string class_words(const std::string& unit_class) {
    std::string out = unit_class;
    for (char& c : out)
        if (c == '_') c = ' ';
    return out;
}

inline uint64_t mix64(uint64_t x) {  // splitmix64 finalizer
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// Text that came from outside the sim (a parser's error message quotes the bytes it choked on)
// can hold bytes that are not UTF-8, and the save cannot be written with those. Anything outside
// plain printable ASCII and well-formed multi-byte UTF-8 becomes '?'.
inline std::string printable_utf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = n > 0 && i + n <= text.size() && !(n == 1 && c < 0x20 && c != '\n' && c != '\t');
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (ok) {
            out.append(text, i, n);
            i += n;
        } else {
            out.push_back('?');
            ++i;
        }
    }
    return out;
}

inline void add_log(CampaignState& s, const std::string& line) {
    s.log.push_back("h" + std::to_string(s.now.hour_index) + ": " + printable_utf8(line));
    constexpr size_t kMaxLog = 48;
    if (s.log.size() > kMaxLog) s.log.erase(s.log.begin(), s.log.begin() + (s.log.size() - kMaxLog));
}

inline std::string format_delta_part(const char* label, int delta) {
    if (delta == 0) return {};
    return std::string(label) + (delta > 0 ? "+" : "") + std::to_string(delta);
}

// Records an attributed resource delta and mirrors a short line into CampaignState.log.
inline void record_resource_change(CampaignState& s, ResourceChange c) {
    c.hour_index = s.now.hour_index;
    if (c.reason.empty()) {
        std::string parts;
        auto append = [&](const std::string& p) {
            if (p.empty()) return;
            if (!parts.empty()) parts += ", ";
            parts += p;
        };
        append(format_delta_part("gold ", c.gold));
        append(format_delta_part("powder ", c.powder));
        append(format_delta_part("cart ", c.cartridges));
        append(format_delta_part("muskets ", c.muskets));
        append(format_delta_part("prov ", c.provisions));
        append(format_delta_part("support ", c.local_support));
        c.reason = parts.empty() ? "resource change" : parts;
    }
    s.resource_changes.push_back(c);
    constexpr size_t kMaxChanges = 64;
    if (s.resource_changes.size() > kMaxChanges)
        s.resource_changes.erase(s.resource_changes.begin(),
                                 s.resource_changes.begin() + (s.resource_changes.size() - kMaxChanges));
    add_log(s, c.reason);
}

template <typename V>
inline V map_get(const std::map<std::string, V>& m, const std::string& k) {
    auto it = m.find(k);
    return it == m.end() ? V{} : it->second;
}

inline bool has_ceasefire(const CampaignState& s) {
    for (const auto& t : s.treaties)
        if ((t.type == "ceasefire" || t.type == "vassalage") && t.hours_remaining > 0) return true;
    return false;
}

inline bool has_safe_passage(const CampaignState& s, const std::string& army, const std::string& prov) {
    for (const auto& t : s.treaties)
        if (t.type == "safe_passage" && t.hours_remaining > 0 && t.army_id == army && t.province_id == prov)
            return true;
    return false;
}

inline int total_units(const Army& a) {
    int n = 0;
    for (const auto& kv : a.units) n += kv.second;
    return n;
}

// BFS shortest path, neighbours visited in sorted id order so ties are deterministic.
inline std::vector<std::string> shortest_path(const CampaignState& s, const std::string& from,
                                              const std::string& to) {
    if (!s.provinces.count(from) || !s.provinces.count(to)) return {};
    std::map<std::string, std::string> prev;
    std::deque<std::string> q{from};
    std::set<std::string> seen{from};
    while (!q.empty()) {
        std::string cur = q.front();
        q.pop_front();
        if (cur == to) break;
        for (const auto& n : s.provinces.at(cur).adjacent) {
            if (seen.insert(n).second) {
                prev[n] = cur;
                q.push_back(n);
            }
        }
    }
    if (!seen.count(to)) return {};
    std::vector<std::string> path{to};
    while (path.back() != from) path.push_back(prev.at(path.back()));
    std::reverse(path.begin(), path.end());
    return path;
}

inline bool contains(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

// ---- Campaign pass (people.cpp, politics.cpp) ----
std::string pass_to_json(const CampaignPass& pass);
CampaignPass pass_from_json(const std::string& json_text);
// Battle hooks. build_battle_setup calls the first for each side once its own numbers stand; the
// second once both sides are built. apply_battle_result calls the third as its last act.
void pass_side_setup(const CampaignState& s, const Province& ground, const Army& army, bool is_attacker,
                     bool owns_ground, SideSetup& side);
void pass_setup_done(const CampaignState& s, BattleSetup& setup);
void pass_after_battle(CampaignState& s, const CampaignState& before, const BattleResult& r, const PendingBattle& pb);
// The opening roles and what is already at sea, once per campaign: load_scenario calls it, and the
// first tick or order does for a state made before the data was loaded. Nothing without pass data.
void pass_seed(CampaignState& s);
// Hourly, from tick_campaign's place in advance_hour.
void pass_tick(CampaignState& s, std::vector<Interrupt>& interrupts);
// An army with no general is a detachment: it cannot besiege. True when persons are not loaded.
bool army_can_besiege(const CampaignState& s, const Army& army);
// Most units an army may hold (a detachment's cap, or an army's under a general); 0 = no cap.
int army_unit_cap(const CampaignState& s, const Army& army);
// A side's body pays this share of its army's pay (0 without the measure).
int upkeep_share_pct(const CampaignState& s, const std::string& faction);
// Hours a company takes to muster in this town, after an organizer's help.
int organizer_muster_hours(const CampaignState& s, const std::string& province, const std::string& faction, int hours);
// The boats an agent found let this army use water edges without the harbour.
bool army_has_boats(const CampaignState& s, const std::string& army_id);
// Units of an enemy army as `viewer` counts them: exact when scouted, else a rough figure.
int units_as_seen(const CampaignState& s, const std::string& viewer, const Army& army);
// Extra value of a town to the AI of `faction` (a decision drew it there).
long ai_lure_value(const CampaignState& s, const std::string& faction, const std::string& province);

inline std::string err(const std::string& code, const std::string& detail = std::string()) {
    return detail.empty() ? code : code + ": " + detail;
}

}  // namespace detail
}  // namespace pa250
