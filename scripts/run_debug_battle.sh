#!/usr/bin/env bash
# Headless debug battle on a battle level: bots only (-PA250Selftest=battle), 10x game time, no window.
# The campaign is not involved: the battle prepares its own setup and only that local copy is changed
# by the debug flags. Prints the BattleResult summary and the in-engine verdict.
#
#   scripts/run_debug_battle.sh <label> [extra game args...]
#   scripts/run_debug_battle.sh two_to_one "-PA250ExtraUnits=british:line_infantry:34,continental:militia:14"
#   scripts/run_debug_battle.sh low_morale "-PA250ExtraUnits=british:line_infantry:20,continental:militia:20" "-PA250Morale=continental:5,british:90"
#
# Debug flags: -PA250ExtraUnits=side:class:units,...  -PA250Morale=side:value,...  -PA250Rounds=N
#              -PA250Bots=N (fixed count a side)  -PA250Scenario=lexington_common|battle_road|concord_bridge
# Level: PA250_LEVEL (default L_Lexington). Output: Saved/PA250/debug_<label>.json and Saved/Logs/debug_<label>.log
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/_ue_env.sh"  # PA250_UE_CMD, PA250_PY, pa250_native (Mac and Windows)
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
SAVED="$ROOT/unreal/PlayAmerica250/Saved/PA250"
LOGDIR="$ROOT/unreal/PlayAmerica250/Saved/Logs"
LEVEL="${PA250_LEVEL:-L_Lexington}"
TIMEOUT="${BATTLE_TIMEOUT:-420}"
[ $# -ge 1 ] || { echo "usage: $0 <label> [extra game args...]"; exit 2; }
LABEL="$1"; shift
mkdir -p "$SAVED" "$LOGDIR"
rm -f "$SAVED/battle_result.json" "$SAVED/selftest_battle_result.json"
"$PA250_UE_CMD" "$(pa250_native "$PROJECT")" "/Game/PA250/Maps/$LEVEL" -game -nullrhi -unattended -nosplash -nosound \
  -PA250Selftest=battle "$@" -log "-abslog=$(pa250_native "$LOGDIR/debug_$LABEL.log")" >/dev/null 2>&1 &
PID=$!; waited=0
while kill -0 "$PID" 2>/dev/null; do
  sleep 3; waited=$((waited + 3))
  if [ "$waited" -ge "$TIMEOUT" ]; then echo "TIMEOUT after ${TIMEOUT}s"; kill -9 "$PID" 2>/dev/null; break; fi
done
wait "$PID" 2>/dev/null; RC=$?
echo "$LABEL: game exit code $RC (${waited}s)"
[ -f "$SAVED/battle_result.json" ] || { echo "no battle_result.json; see $LOGDIR/debug_$LABEL.log"; exit 1; }
cp "$SAVED/battle_result.json" "$SAVED/debug_$LABEL.json"
"$PA250_PY" - "$(pa250_native "$SAVED/debug_$LABEL.json")" <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); t = d["trace"]; r = d["result"]
print(f"  end reason {d['reason']} | winner {r['winner']} | {d['elapsed_game_seconds']:.0f} game-seconds")
print(f"  on field {t['field_strength']} | squads at peak {t['squads_peak']} | break clock peak {t['broke_peak_seconds']}")
print(f"  standing {t.get('standing')} | deaths {t.get('deaths')} | reserve {t['tickets']} | post flips {t['post_flips']}")
print(f"  posts {t['posts']} | verbs {dict((k, v) for k, v in t.get('verbs', {}).items() if v)}")
print("  ammo (rounds a man, start, left, refills):",
      {k: (v["rounds_per_man"], v["start"], v["left"], v["refills"]) for k, v in t["ammo"].items()})
cas = r.get("casualties", {})
men = {f: {k: sum(c.get(k, 0) for c in cls.values()) for k in ("killed", "wounded", "prisoners")} for f, cls in cas.items()}
print(f"  casualties in men {men} | shots {r.get('shots_fired', {})}")
for f, m in men.items():
    dead = (t.get("deaths") or {}).get(f, 0)
    print(f"  ledger {f}: killed + wounded {m['killed'] + m['wounded']} = deaths {dead}:", "OK" if m["killed"] + m["wounded"] == dead else "MISMATCH")
if t.get("support"):
    print(f"  support {t['support']}")
if t.get("generals") and (t["generals"]["british"] or t["generals"]["continental"] or t["generals"]["alarmed"]["british"] or t["generals"]["alarmed"]["continental"]):
    print(f"  generals {t['generals']} | result commander_events {r.get('commander_events', {})}")
if t.get("held"):
    h = t["held"]
    print(f"  held: first post flip at {h['first_flip_seconds']:.0f} s | down to half their field: {h['half_strength_seconds']} (-1 = never)")
if t.get("works"):
    print(f"  works {t['works']}")
if t.get("feel"):
    print(f"  feel {t['feel']}")
if t.get("mode", {}).get("name", "conquest") != "conquest":
    print(f"  mode {t['mode']}")
if t.get("guns", {}).get("on_field"):
    print(f"  guns {t['guns']} | taken (result) {r.get('guns_taken', {})}")
if t.get("led_volley", {}).get("side"):
    lv = t["led_volley"]
    print(f"  led volley ({lv['side']}): {lv['volleys']} fired on the leader's shot, {lv['shook']} shook the squad fired on")
print("  campaign morale", {k: v["morale"] for k, v in d["setup"]["sides"].items()})
PY
grep -a "PA250 sweep\|PA250 broke\|PA250 matchup\|PA250 FAIL\|unit classes: FALLBACK\|selftest_battle\|PA250 led volley probe\|PA250 works:" "$LOGDIR/debug_$LABEL.log" | sed 's/^\[[^]]*\]\[[^]]*\]//' | cut -c1-170 | tail -6
exit "$RC"
