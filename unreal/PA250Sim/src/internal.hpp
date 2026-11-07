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

inline void add_log(CampaignState& s, const std::string& line) {
    s.log.push_back("t" + std::to_string(s.turn) + ": " + line);
    constexpr size_t kMaxLog = 48;
    if (s.log.size() > kMaxLog) s.log.erase(s.log.begin(), s.log.begin() + (s.log.size() - kMaxLog));
}

inline bool has_ceasefire(const CampaignState& s) {
    for (const auto& t : s.treaties)
        if (t.type == "ceasefire" && t.turns_remaining > 0) return true;
    return false;
}

inline bool has_safe_passage(const CampaignState& s, const std::string& army, const std::string& prov) {
    for (const auto& t : s.treaties)
        if (t.type == "safe_passage" && t.turns_remaining > 0 && t.army_id == army && t.province_id == prov)
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

inline std::string err(const std::string& code, const std::string& detail = std::string()) {
    return detail.empty() ? code : code + ": " + detail;
}

}  // namespace detail
}  // namespace pa250
