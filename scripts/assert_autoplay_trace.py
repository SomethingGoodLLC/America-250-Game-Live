#!/usr/bin/env python3
"""Assert the vertical-slice autoplay result.

Usage:
  assert_autoplay_trace.py TRACE.json [--battle BATTLE.json] \\
    [--data boston1775.json] [--compare PREV_TRACE.json]
Exit status 0 = every assertion passed.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
errors: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(("PASS  " if cond else "FAIL  ") + msg)
    if not cond:
        errors.append(msg)


def ordered_subsequence(events: list[dict[str, Any]], wanted: list[str]) -> bool:
    it = iter(e["event"] for e in events)
    return all(any(w == e for e in it) for w in wanted)


def pre_battle_hashes(events: list[dict[str, Any]]) -> list[tuple[str, int, str]]:
    out = []
    for e in events:
        if e["event"] == "begin_battle":
            break
        if e["event"] in ("new_campaign", "advance_hour", "interrupt"):
            out.append((e["event"], e["hour_index"], e["hash"]))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--battle")
    ap.add_argument(
        "--data",
        default=str(ROOT / "unreal" / "PA250Sim" / "data" / "boston1775.json"),
    )
    ap.add_argument("--compare")
    a = ap.parse_args()

    trace = json.loads(Path(a.trace).read_text())
    events = trace["events"]
    notes = len(trace.get("notes", []))
    check(
        trace.get("passed") is True,
        f"in-engine checks all passed ({notes} notes)",
    )
    for f in trace.get("failures", []):
        check(False, "engine check failed: " + f)

    flow = [
        "new_campaign",
        "advance_hour",
        "interrupt",
        "begin_battle",
        "finish_battle",
        "begin_diplomacy",
        "finish_diplomacy",
        "advance_hour",
        "save",
        "load",
    ]
    check(
        ordered_subsequence(events, flow),
        "flow order: campaign -> hours -> contact -> battle -> result -> "
        "diplomacy -> result -> hours -> save -> load",
    )
    fd = [e for e in events if e["event"] == "finish_diplomacy"]
    check(
        len(fd) == 1 and fd[0]["detail"] == "applied",
        "exactly one DiplomacyResult, and it changed state",
    )
    no_change = any(
        e["event"] == "finish_diplomacy" and e["detail"] == "no_change" for e in events
    )
    check(not no_change, "no rejected/empty result was applied")
    saves = [e for e in events if e["event"] == "save"]
    loads = [e for e in events if e["event"] == "load"]
    check(
        bool(saves) and bool(loads) and saves[-1]["hash"] == loads[-1]["hash"],
        "save/load hash identical",
    )
    check(
        bool(loads) and loads[-1]["hash"] == trace.get("final_hash"),
        "final hash equals the loaded hash",
    )
    pre = pre_battle_hashes(events)
    hours = [h for _, h, _ in pre]
    check(
        len(pre) >= 2
        and all(b == a + 1 for a, b in zip(hours, hours[1:], strict=False)),
        f"hour boundaries consecutive before the battle ({len(pre)} boundaries)",
    )
    after = [i for i, e in enumerate(events) if e["event"] == "finish_diplomacy"]
    post = [
        e
        for e in events[after[0] if after else len(events) :]
        if e["event"] in ("advance_hour", "interrupt")
    ]
    check(len(post) >= 24, f"24 hours advanced after diplomacy ({len(post)})")

    if a.battle:
        b = json.loads(Path(a.battle).read_text())
        setup, res = b["setup"], b["result"]
        check(
            res["winner"] in ("british", "continental"),
            "battle winner set: " + res["winner"],
        )
        ok = True
        for faction, classes in res["losses"].items():
            for cls, n in classes.items():
                tickets = setup["sides"][faction]["tickets"].get(cls, 0)
                ok &= 0 <= n <= tickets
        check(ok, f"battle losses <= ticket pools: {res['losses']}")
        data = json.loads(Path(a.data).read_text())
        adj: dict[str, set[str]] = {p["id"]: set() for p in data["provinces"]}
        for edge in data["edges"]:
            adj[edge["a"]].add(edge["b"])
            adj[edge["b"]].add(edge["a"])
        retreat = res["retreat_province"]
        province = res["province_id"]
        check(
            retreat in adj[province],
            f"retreat province {retreat} adjacent to {province}",
        )

    if a.compare:
        prev = json.loads(Path(a.compare).read_text())
        now_h = pre_battle_hashes(events)
        prev_h = pre_battle_hashes(prev["events"])
        check(
            bool(now_h) and now_h == prev_h,
            "state hashes identical at every hour boundary before "
            f"the battle ({len(now_h)} boundaries)",
        )

    status = "OK" if not errors else "FAILED"
    print(f"\n{status} ({len(errors)} failure(s))")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
