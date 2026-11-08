#include "internal.hpp"

#include <limits>
#include <queue>

namespace pa250 {

PathPlan plan_path(const CampaignState& s, const std::string& army_id, const std::string& dest) {
    PathPlan out;
    auto ait = s.armies.find(army_id);
    if (ait == s.armies.end()) {
        out.error = "unknown_army";
        return out;
    }
    if (!s.provinces.count(dest)) {
        out.error = "unknown_province";
        return out;
    }
    const Army& army = ait->second;
    const std::string start = army.province;
    out.provinces = {start};
    if (start == dest) {
        out.ok = true;
        out.arrival = s.now;
        return out;
    }

    const bool harbor = [&] {
        auto fit = s.factions.find(army.faction);
        return fit != s.factions.end() && fit->second.controls_harbor;
    }();

    // Dijkstra on march_hours. Node = province id. Parent stores (prev_province, edge_id).
    struct Node {
        int hours = 0;
        std::string prev;
        std::string via_edge;
        std::vector<std::string> prov_seq;  // for lex tie-break
    };
    std::map<std::string, Node> best;
    using Key = std::tuple<int, std::string, std::string>;  // hours, lex path key, province
    std::priority_queue<Key, std::vector<Key>, std::greater<Key>> pq;

    auto path_key = [](const std::vector<std::string>& seq) {
        std::string k;
        for (const auto& p : seq) {
            k.push_back('\1');
            k += p;
        }
        return k;
    };

    best[start] = Node{0, "", "", {start}};
    pq.push({0, path_key(best[start].prov_seq), start});

    while (!pq.empty()) {
        auto [h, pk, cur] = pq.top();
        pq.pop();
        auto it = best.find(cur);
        if (it == best.end() || h != it->second.hours || pk != path_key(it->second.prov_seq)) continue;
        if (cur == dest) break;

        // Collect outbound edges sorted by edge id for determinism.
        std::vector<const Edge*> outs;
        for (const auto& ek : s.edges) {
            const Edge& e = ek.second;
            if (e.a == cur || e.b == cur) outs.push_back(&e);
        }
        std::sort(outs.begin(), outs.end(), [](const Edge* a, const Edge* b) { return a->id < b->id; });

        for (const Edge* e : outs) {
            if ((e->type == "ferry" || e->type == "boat") && !harbor) continue;
            const std::string nxt = e->a == cur ? e->b : e->a;
            const int nh = h + e->march_hours;
            auto nseq = it->second.prov_seq;
            nseq.push_back(nxt);
            const std::string nkey = path_key(nseq);
            auto bit = best.find(nxt);
            bool take = false;
            if (bit == best.end())
                take = true;
            else if (nh < bit->second.hours)
                take = true;
            else if (nh == bit->second.hours && nkey < path_key(bit->second.prov_seq))
                take = true;
            if (!take) continue;
            best[nxt] = Node{nh, cur, e->id, nseq};
            pq.push({nh, nkey, nxt});
        }
    }

    if (!best.count(dest)) {
        out.error = harbor ? "no_route" : "no_route_requires_harbor_control";
        return out;
    }

    // Reconstruct edges from dest back to start.
    std::vector<std::string> edges_rev;
    std::string cur = dest;
    while (cur != start) {
        const Node& n = best.at(cur);
        edges_rev.push_back(n.via_edge);
        cur = n.prev;
    }
    std::reverse(edges_rev.begin(), edges_rev.end());
    out.edges = edges_rev;
    out.provinces = best.at(dest).prov_seq;
    out.total_march_hours = best.at(dest).hours;

    // Arrival matches advance_hour: the order applies on the next tick, and that tick marches
    // only when the clock hour is in [0, march_hours_per_day). Rest hours still pass.
    const int mph = std::max(1, s.march_hours_per_day);
    int remaining = out.total_march_hours;
    GameTime arrive = s.now;
    while (remaining > 0) {
        arrive.hour_index += 1;
        const YMDH y = to_ymdh(arrive);
        if (static_cast<int>(y.h) < mph) --remaining;
    }
    out.arrival = arrive;
    out.ok = true;
    return out;
}

}  // namespace pa250
