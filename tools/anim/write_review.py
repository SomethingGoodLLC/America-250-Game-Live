"""Write <AnimationPass>/review/REVIEW.md: the reels, the clips and seeds behind them, known issues."""
import json
import subprocess
import sys
from pathlib import Path

out_root = Path(sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
rev = out_root / "review"
m = json.loads((out_root / "manifest.json").read_text(encoding="utf-8"))
ffprobe = next((out_root.parent / "Tooling" / "ffmpeg").rglob("ffprobe.exe"))
REELS = {
    "Gun": "Full drill reel, 23 s: the cannon at its real place and one man per role at his station. Sponge + Vent (0-4 s), Load (4-7), "
           "Ram (7-10), Prime (10-13), Lay + Traverse (13-16), Fire (16-18.5), Flinch (18.5-20), Run Up (20-23); idles between.",
    "Volley": "Ten-man line, Make Ready -> Present -> Fire, variants v0-v3 across the line, each man 0 to 4 frames (0-133 ms) late (seed 1775).",
    "React": "Twelve-man block, blast at the cone in front of it at 1 s. Nearest four: knockdown, two seconds lying, transition, get-up. "
             "Next four: stagger, then deafened. Far four: near-miss duck.",
}
ISSUES = [
    "Props are separate actors keyed every frame from the hands, not bone attachments: the sponge-and-rammer staff, linstock, "
    "handspike and cartridge of this pass (flat colours, no textures) and the game's musket. A two-handed prop lies along the line "
    "between the grips; one carried in one hand stands upright in it, so the idles hold the staff and linstock bolt upright.",
    "Volley: the musket is upright through Make Ready and swings level over the first 12 frames of Present; in the reload clips "
    "(not in a reel) no musket was fitted and their hands are not pinned to one.",
    "Volley kneeling clips: the fix-up moved the hips 12 to 15 cm to bring the hands onto the aim and held the ankles, but the "
    "knee on the ground is not pinned and may lift or slide. They are in no reel and have not been looked at on the soldier.",
    "Gun reel: the crew is crowded. The provisional ventsman station (10, 50) is inside the wheel track (the wheel's outer face is at "
    "y = 57 to 64 cm) and Crew_3/Crew_4 at (-90, +-75) stand at the wheels, so men overlap the carriage and each other.",
    "Gun reel: the sponger and loader step about half a metre from their stations to reach the bore targets (station and staff "
    "numbers disagree, see PROGRESS.md), and after a constrained clip the following idle starts back at the station: a visible pop, "
    "which the staff follows.",
    "Reels cut from clip to clip with no blending; a clip that ends before the reel does starts again (Volley: up to 4 frames).",
    "React reel: the get-up is played by a second figure placed where the transition ends; a one-frame mismatch at that cut is the "
    "reel's, not the clips'. Transitions are not foot-locked. The React reel has no props and was not rendered again in the prop pass.",
    "Volley: the men stand with their feet well apart. Not checked whether that is the generated motion or the retarget.",
    "Looked at by the agent: key-frame sheets of Volley front, Gun high and React high. No MP4 was watched in motion.",
]
lines = ["# Animation pass review: gun drill, volley, reactions", "",
         "Rendered with Movie Render Queue in the scratch project (`AnimationPass/UEHost`), 1280x720, 30 fps, soldier mesh with its "
         "materials, flat test level. Model for every clip: `%s`." % next(iter(m["clips"].values()))["model"], ""]
for fam, text in REELS.items():
    lines += ["## %s" % fam, "", text, ""]
    for f in sorted((rev / fam).glob("*.mp4")):
        dur = subprocess.check_output([str(ffprobe), "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(f)], text=True).strip()
        lines.append("- [%s](%s/%s) (%.1f s)" % (f.name, fam, f.name, float(dur)))
    for f in sorted((rev / fam).glob("*.jpg")):
        lines.append("- [%s](%s/%s)" % (f.name, fam, f.name))
    lines += ["", "| Clip | Seeds (v0, v1, ...) | Tries |", "|---|---|---|"]
    by = {}
    for key, c in sorted(m["clips"].items()):
        if c["family"] == fam:
            by.setdefault(c["name"], []).append((c["variant"], c["seed"], len(c.get("attempts", [1]))))
    for name, vs in by.items():
        vs.sort()
        lines.append("| `%s` | %s | %s |" % (name, ", ".join(str(s) for _, s, _ in vs), ", ".join(str(t) for _, _, t in vs)))
    lines.append("")
lines += ["## Transitions", "", "%d clips made with MotionBricks (revision `%s`), listed in `manifest.json` under `transitions`; the React reel uses four of them." % (
    len(m.get("transitions", {})), next(iter(m["transitions"].values()))["tool_revision"][:7]), "",
    "## Known issues", ""] + ["- " + i for i in ISSUES] + [""]
(rev / "REVIEW.md").write_text("\n".join(lines), encoding="utf-8")
print("wrote", rev / "REVIEW.md")
