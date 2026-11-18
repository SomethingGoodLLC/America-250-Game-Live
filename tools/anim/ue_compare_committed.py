"""Are the clips rebuilt in the scratch project the same motion as the clips committed to the repository?

  UnrealEditor-Cmd <scratch project> -run=pythonscript -script=tools/anim/ue_compare_committed.py -unattended -nullrhi -nosplash
Before running, copy the repository's Content/PA250/Anim/Generated/{Gun,Volley,React} into the scratch project's
Content/PA250/Anim/Committed/ (same sub-folders). Environment: PA250_ANIM_PASS, PA250_ONLY (comma-separated keys).

For every clip present in both places it samples every fourth frame and compares the component-space position of
fourteen bones. Writes <AnimationPass>/compare_committed.json and logs the count within 0.5 cm and the worst clips.
A rebuilt manifest is the record of a committed clip only where the two agree.
"""
import json
import os

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
ONLY = [k for k in os.environ.get("PA250_ONLY", "").split(",") if k]
NEW, OLD = "/Game/PA250/Anim/Generated", "/Game/PA250/Anim/Committed"
BONES = ("pelvis", "spine_03", "head", "hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "foot_l", "foot_r",
         "calf_l", "calf_r", "ball_l", "ball_r", "root")
LIMIT_CM = 0.5
eal = unreal.EditorAssetLibrary
ape = unreal.AnimPoseExtensions

rows = {}
for fam in ("Gun", "Volley", "React"):
    for path in eal.list_assets("%s/%s" % (NEW, fam), recursive=False):
        key = path.rsplit("/", 1)[-1].split(".")[0]
        if ONLY and key not in ONLY:
            continue
        old_path = "%s/%s/%s" % (OLD, fam, key)
        if not eal.does_asset_exist(old_path):
            rows[key] = {"committed": False}
            continue
        a, b = unreal.load_asset("%s/%s/%s" % (NEW, fam, key)), unreal.load_asset(old_path)
        if not isinstance(a, unreal.AnimSequence) or not isinstance(b, unreal.AnimSequence):
            continue
        la, lb = unreal.AnimationLibrary.get_sequence_length(a), unreal.AnimationLibrary.get_sequence_length(b)
        n = int(round(min(la, lb) * 30.0)) + 1
        worst, where = 0.0, ""
        for f in range(0, n, 4):
            pa = ape.get_anim_pose_at_frame(a, f, unreal.AnimPoseEvaluationOptions())
            pb = ape.get_anim_pose_at_frame(b, f, unreal.AnimPoseEvaluationOptions())
            for bone in BONES:
                ta = ape.get_bone_pose(pa, bone, unreal.AnimPoseSpaces.WORLD).translation
                tb = ape.get_bone_pose(pb, bone, unreal.AnimPoseSpaces.WORLD).translation
                d = ((ta.x - tb.x) ** 2 + (ta.y - tb.y) ** 2 + (ta.z - tb.z) ** 2) ** 0.5
                if d > worst:
                    worst, where = d, "%s f%d" % (bone, f)
        rows[key] = {"committed": True, "worst_cm": round(worst, 3), "where": where,
                     "length_new_s": round(la, 3), "length_committed_s": round(lb, 3), "same": worst <= LIMIT_CM and abs(la - lb) < 0.01}

both = {k: r for k, r in rows.items() if r.get("committed")}
same = sum(1 for r in both.values() if r["same"])
with open(os.path.join(PASS, "compare_committed.json"), "w", encoding="utf-8") as f:
    json.dump(rows, f, indent=1, sort_keys=True)
unreal.log("PA250 compare: %d clips in both, %d the same within %.1f cm, %d differ, %d not committed" % (
    len(both), same, LIMIT_CM, len(both) - same, len(rows) - len(both)))
for k, r in sorted(both.items(), key=lambda kr: -kr[1]["worst_cm"])[:8]:
    if not r["same"]:
        unreal.log_warning("PA250 compare differs: %s %.1f cm at %s" % (k, r["worst_cm"], r["where"]))
