"""Write README_clips.md for /Game/PA250/Anim/Generated from manifest.json (run after plan_notifies.py)."""
import json
import sys
from pathlib import Path

out_root = Path(sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
dst = Path(__file__).resolve().parents[2] / "unreal/PlayAmerica250/Content/PA250/Anim/Generated/README_clips.md"
m = json.loads((out_root / "manifest.json").read_text(encoding="utf-8"))
by = {}
for key, c in sorted(m["clips"].items()):
    e = by.setdefault(c["name"], {"family": c["family"], "variants": 0, "dur": c["duration_s"], "rm": c["root_motion"],
                                  "loop": c["loop"], "notifies": set(), "hands": False, "chain": c.get("chain_to")})
    e["variants"] += 1
    e["notifies"] |= {n["name"] for n in c.get("notifies", [])}
    e["hands"] |= any(r.get("type") == "hands" for r in c["constraints"])
lines = ["# Generated clips: gun drill, volley, reactions",
         "",
         "Offline-generated animation for the soldier (`SK_AMERICAN_SOLDIER_Skeleton`). Each clip `AM_<Clip>` has variants",
         "`AM_<Clip>_v0 .. _v<N-1>` in `Gun/`, `Volley/` or `React/`. 30 fps. Made with Kimodo `%s` (text + keypose" % next(iter(m["clips"].values()))["model"],
         "constraints), retargeted through `/Game/PA250/Anim/Rig/RTG_SOMA_to_Soldier`; seeds, prompts and constraints are in",
         "`manifest.json` in the AnimationPass source pack (outside the repository). Tools: `tools/anim`.",
         "",
         "Blast suffixes: `_F` blast in front (thrown backward), `_B` behind (thrown forward), `_L` on his left (thrown",
         "right), `_R` on his right (thrown left). Notifies are Blueprint notify classes in `Notifies/`, on track `PA250`.",
         "Hands = the clip's hands were pinned to gun targets (baked). Any variant of a clip may follow any variant of the",
         "clip before it in a chain; where the join is not direct, a transition clip below bridges it.",
         ""]
for fam in ("Gun", "Volley", "React"):
    lines += ["## %s" % fam, "", "| Clip | Variants | Seconds | Root motion | Loop | Hands | Chains to | Notifies |", "|---|---|---|---|---|---|---|---|"]
    for name, e in by.items():
        if e["family"] != fam:
            continue
        lines.append("| `%s` | %d | %.1f | %s | %s | %s | %s | %s |" % (
            name, e["variants"], e["dur"], "yes" if e["rm"] else "no", "yes" if e["loop"] else "no",
            "yes" if e["hands"] else "no", "`%s`" % e["chain"] if e["chain"] else "", ", ".join(sorted(e["notifies"]))))
    lines.append("")
lines += ["## Transitions (MotionBricks in-betweening, conditioned only on the clips above)", "",
          "Named `AM_<X>_to_<Y>_v<a>_v<b>`: from variant a of X to variant b of Y. No notifies, no root motion.",
          "Ragdoll joins are in the knockdown's space: the get-up that follows must be placed where the transition ends.", "",
          "| From | To | Clips | Frames |", "|---|---|---|---|"]
tr = {}
for key, t in sorted(m.get("transitions", {}).items()):
    a, b = m["clips"][t["from"]]["name"], m["clips"][t["to"]]["name"]
    e = tr.setdefault((a, b), {"n": 0, "lo": 999, "hi": 0})
    e["n"] += 1; e["lo"] = min(e["lo"], t["frames"]); e["hi"] = max(e["hi"], t["frames"])
for (a, b), e in tr.items():
    lines.append("| `%s` | `%s` | %d | %d to %d |" % (a, b, e["n"], e["lo"], e["hi"]))
dst.write_text("\n".join(lines) + "\n", encoding="utf-8")
print("wrote", dst, len(by), "clips,", sum(e["n"] for e in tr.values()), "transitions")
