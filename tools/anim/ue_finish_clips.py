"""Finish the retargeted clips in Unreal: hand fix-up bake, anim notifies, verification.

  UnrealEditor-Cmd <project> -run=pythonscript -script=tools/anim/ue_finish_clips.py -unattended -nullrhi -nosplash
Environment:
  PA250_ANIM_PASS   the AnimationPass folder (manifest.json; the report is written to ue_verify_report.json)
  PA250_STAGE       fixup | musket | notifies | verify | all (default all; verify alone changes nothing)
  PA250_ONLY        comma-separated clip keys

Hand fix-up. The soldier's arms are shorter than the SOMA body's, so after the retarget the hands are off
the gun. For every clip with hand targets in the manifest, each arm is solved as a two-bone chain
(upper arm, forearm; the hand keeps its orientation; where the soldier's shorter arm cannot reach, the pelvis
moves by the shortfall and the legs are solved so the ankles stay where they were) so the grip point (midway hand bone to the last middle
finger bone) is on its target at each constraint frame, eased in and out over 8 frames. Where both hands
have targets on one line (the staff on the bore) they are also held on that line between the first and
last constraint frame, free to slide along it. The result is written into the clip's bone tracks: an
editor-time bake, no runtime bone writes. (A Control Rig asset was the plan; this does the same solve in
editor Python because the scratch project has no way to author and bake a Control Rig headless.)

Musket hold (stage `musket`). In the Present and Fire clips (standing and kneeling) the musket is taken to lie
level from the right hand's grip (the game snaps the musket's root to the right hand), pointing forward turned
toward the soldier's left by the smallest angle at which his left hand can reach the fore-end (the generated
poses stand square to the front; the angle is stored on the clip as metadata tag PA250MusketYawDeg), and the left
arm is solved so the left grip is on the fore-end, MUSKET_FORE_CM ahead and MUSKET_DROP_CM below that line;
Present eases into it over its first MUSKET_EASE frames, Fire holds it throughout. Baked once per clip.

Notifies are instances of small Blueprint notify classes in /Game/PA250/Anim/Generated/Notifies whose
names are the notify names (Notify_GunStep_Done, ...); times come from manifest.json (plan_notifies.py).
"""
import json
import math
import os

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
STAGE = os.environ.get("PA250_STAGE", "all")
ONLY = [k for k in os.environ.get("PA250_ONLY", "").split(",") if k]
OUT = "/Game/PA250/Anim/Generated"
NOTIFY_DIR = OUT + "/Notifies"
NOTIFY_NAMES = ("Notify_GunStep_Done", "Notify_Volley_Discharge", "Notify_Reload_PhaseEnd",
                "Notify_Ragdoll_Handoff", "Notify_FootPlant")
ARMS = {"left_hand": ("clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "middle_03_l"),
        "right_hand": ("clavicle_r", "upperarm_r", "lowerarm_r", "hand_r", "middle_03_r")}
EASE = 8
HAND_LIMIT_CM = 2.0
# Not used: the Present and Fire clips now carry hand targets for the aim (clip_specs.yaml), so the ordinary
# fix-up pins both hands. Tried first on the unconstrained clips and failed: their arms were pushed forward side
# by side and the fore-end was out of the left arm's reach at any angle up to 70 degrees.
MUSKET_CLIPS = ()
MUSKET_FORE_CM, MUSKET_DROP_CM, MUSKET_EASE = 38.0, 4.0, 12
eal = unreal.EditorAssetLibrary
al = unreal.AnimationLibrary
ape = unreal.AnimPoseExtensions


# ---------- small vector / quaternion helpers (tuples; Unreal's Python has no numpy) ----------
def v(t): return (t.x, t.y, t.z)
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a): return math.sqrt(dot(a, a))
def unit(a):
    n = norm(a)
    return mul(a, 1.0 / n) if n > 1e-9 else (0.0, 0.0, 0.0)
def q(t): return (t.x, t.y, t.z, t.w)
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)
def qinv(a): return (-a[0], -a[1], -a[2], a[3])
def qnorm(a):
    n = math.sqrt(sum(c * c for c in a))
    return tuple(c / n for c in a)
def from_to(a, b):
    """Shortest rotation taking direction a to direction b."""
    a, b = unit(a), unit(b)
    d = dot(a, b)
    if d > 0.999999:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -0.999999:
        axis = unit(cross(a, (1.0, 0.0, 0.0)) if abs(a[0]) < 0.9 else cross(a, (0.0, 1.0, 0.0)))
        return (axis[0], axis[1], axis[2], 0.0)
    c = cross(a, b)
    return qnorm((c[0], c[1], c[2], 1.0 + d))
def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def fail(text):
    unreal.log_error("PA250 finish: " + text)
    raise RuntimeError(text)


manifest = json.load(open(os.path.join(PASS, "manifest.json"), encoding="utf-8"))
clips = dict(manifest["clips"])
for key, t in manifest.get("transitions", {}).items():
    clips[key] = {"family": manifest["clips"][t["from"]]["family"], "constraints": [], "notifies": [],
                  "frames": t["frames"], "transition": True}
keys = sorted(k for k in clips if not ONLY or k in ONLY)


def path_of(key):
    return "%s/%s/%s" % (OUT, clips[key]["family"], key)


def pose_at(seq, frame):
    opt = unreal.AnimPoseEvaluationOptions()
    return ape.get_anim_pose_at_frame(seq, frame, opt)


def world(pose, bone):
    return ape.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)


def local(pose, bone):
    return ape.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL)


def grip(pose, hand):
    _, _, _, h, m = ARMS[hand]
    return mul(add(v(world(pose, h).translation), v(world(pose, m).translation)), 0.5)


def targets_cm(m):
    """Hand targets in the soldier's component space (cm): X left, Y forward, Z up; station at the origin."""
    out = []
    if not m.get("constraints"):
        return out
    k = 100.0 / float(m["scale_soma_over_soldier"])
    for r in m["constraints"]:
        if r.get("type") == "hands":
            out.append((int(r["frame"]), {h: (p[0] * k, p[2] * k, p[1] * k) for h, p in r["targets_kimodo_m"].items()}))
    return sorted(out)


def hand_errors(seq, m):
    worst = 0.0
    for frame, tg in targets_cm(m):
        pose = pose_at(seq, frame)
        for hand, t in tg.items():
            worst = max(worst, norm(sub(grip(pose, hand), t)))
    return worst


def two_bone(S, E, W, Wt):
    """Elbow and wrist of a two-bone chain from S reaching for Wt, bending the way it already bends."""
    la, lb = norm(sub(E, S)), norm(sub(W, E))
    dist = max(abs(la - lb) + 0.05, min(la + lb - 0.05, norm(sub(Wt, S))))
    dirn = unit(sub(Wt, S))
    pole = sub(sub(E, S), mul(dirn, dot(sub(E, S), dirn)))
    if norm(pole) < 1e-4:
        pole = cross(cross(sub(E, S), sub(W, S)), dirn)
    u = unit(pole)
    cosA = max(-1.0, min(1.0, (la * la + dist * dist - lb * lb) / (2.0 * la * dist)))
    E2 = add(S, mul(add(mul(dirn, cosA), mul(u, math.sqrt(max(0.0, 1.0 - cosA * cosA)))), la))
    return E2, add(S, mul(dirn, dist))


def fix_hands(seq, m):
    tg = targets_cm(m)
    if not tg:
        return None
    n = int(m["frames"])
    frames = [fr for fr, _ in tg]
    lo, hi = max(0, frames[0] - EASE), min(n - 1, frames[-1] + EASE)
    hands = sorted(tg[0][1].keys())
    line = None
    first = next(r for r in m["constraints"] if r.get("type") == "hands")
    if len(hands) == 2 and len(first.get("targets_gun_cm") or {}) == 2:
        # Both hands on the staff in the bore: its line is the line through the two targets. The hands are kept on
        # it for the whole clip wherever they are near it, not only at the constraint frames (user review
        # 2026-11-16: between them the staff swung through the barrel wall).
        a, b = tg[0][1][hands[0]], tg[0][1][hands[1]]
        line = (a, unit(sub(b, a)))
    ease_lo, ease_hi = lo, hi
    if line is not None:
        lo, hi = 0, n - 1
    poses = {f: pose_at(seq, f) for f in range(lo, hi + 1)}
    err = {h: {f: sub(t[h], grip(poses[f], h)) for f, t in tg} for h in hands}
    new_rot, pelvis_shift, worst_shift = {}, {}, 0.0
    for f in range(lo, hi + 1):
        pose = poses[f]
        want = {}
        for hand in hands:
            if f <= frames[0]:
                d = mul(err[hand][frames[0]], smooth((f - ease_lo) / float(frames[0] - ease_lo)) if frames[0] > ease_lo else 1.0)
            elif f >= frames[-1]:
                d = mul(err[hand][frames[-1]], smooth((ease_hi - f) / float(ease_hi - frames[-1])) if ease_hi > frames[-1] else 1.0)
            else:
                i0 = max(k for k, fr in enumerate(frames) if fr <= f)
                w = (f - frames[i0]) / float(frames[i0 + 1] - frames[i0])
                d = add(mul(err[hand][frames[i0]], 1.0 - w), mul(err[hand][frames[i0 + 1]], w))
            if line is not None:
                rel = sub(add(grip(pose, hand), d), line[0])
                perp = sub(rel, mul(line[1], dot(rel, line[1])))
                # Fully on the line within 12 cm of it or between the constraint frames; let go by 50 cm away.
                hold = 1.0 if frames[0] <= f <= frames[-1] else smooth(1.0 - (norm(perp) - 12.0) / 38.0)
                d = sub(d, mul(perp, hold))  # back onto the staff's line, free along it
            want[hand] = d
        # The soldier's arms are shorter than the source body's: what an arm cannot reach, the hips make up.
        shift = (0.0, 0.0, 0.0)
        for _ in range(6):  # move by the worse arm's shortfall until both can reach
            worst = None
            for hand in hands:
                _, upper, lower, hbone, _ = ARMS[hand]
                S, E, W = (v(world(pose, b).translation) for b in (upper, lower, hbone))
                Wt = add(W, want[hand])
                S2 = add(S, shift)
                short = norm(sub(Wt, S2)) - (norm(sub(E, S)) + norm(sub(W, E)) - 0.3)
                if short > 0.01 and (worst is None or short > worst[0]):
                    worst = (short, unit(sub(Wt, S2)))
            if worst is None:
                break
            shift = add(shift, mul(worst[1], worst[0]))
        shift = (shift[0], shift[1], max(-8.0, min(8.0, shift[2])))
        worst_shift = max(worst_shift, norm(shift))
        pelvis_shift[f] = shift
        for hand in hands:
            clav, upper, lower, hbone, _ = ARMS[hand]
            S, E, W = (add(v(world(pose, b).translation), shift) for b in (upper, lower, hbone))
            Wt = add(sub(W, shift), want[hand])
            E2, W2 = two_bone(S, E, W, Wt)
            g_clav, g_up, g_lo, g_hand = (q(world(pose, b).rotation) for b in (clav, upper, lower, hbone))
            g_up2 = qnorm(qmul(from_to(sub(E, S), sub(E2, S)), g_up))
            g_lo2 = qnorm(qmul(from_to(sub(W, E), sub(W2, E2)), g_lo))
            new_rot.setdefault(upper, {})[f] = qnorm(qmul(qinv(g_clav), g_up2))
            new_rot.setdefault(lower, {})[f] = qnorm(qmul(qinv(g_up2), g_lo2))
            new_rot.setdefault(hbone, {})[f] = qnorm(qmul(qinv(g_lo2), g_hand))
        if norm(shift) > 1e-3:
            g_pelvis = q(world(pose, "pelvis").rotation)
            for side in ("l", "r"):
                thigh, calf, foot = "thigh_" + side, "calf_" + side, "foot_" + side
                H, K, A = (v(world(pose, b).translation) for b in (thigh, calf, foot))
                H2, K0 = add(H, shift), add(K, shift)
                K2, A2 = two_bone(H2, K0, add(A, shift), A)  # the ankle stays where it was
                g_th, g_ca, g_ft = (q(world(pose, b).rotation) for b in (thigh, calf, foot))
                g_th2 = qnorm(qmul(from_to(sub(K, H), sub(K2, H2)), g_th))
                g_ca2 = qnorm(qmul(from_to(sub(A, K), sub(A2, K2)), g_ca))
                new_rot.setdefault(thigh, {})[f] = qnorm(qmul(qinv(g_pelvis), g_th2))
                new_rot.setdefault(calf, {})[f] = qnorm(qmul(qinv(g_th2), g_ca2))
                new_rot.setdefault(foot, {})[f] = qnorm(qmul(qinv(g_ca2), g_ft))
    all_poses = {f: (poses[f] if f in poses else pose_at(seq, f)) for f in range(n)}
    ctrl = seq.controller
    ctrl.open_bracket(unreal.Text("PA250 hand fix-up"), False)
    for bone, rots in new_rot.items():
        pos, rot, scl = [], [], []
        for f in range(n):
            t = local(all_poses[f], bone)
            pos.append(t.translation)
            scl.append(t.scale3d)
            r = rots.get(f)
            rot.append(unreal.Quat(r[0], r[1], r[2], r[3]) if r else t.rotation)
        ctrl.set_bone_track_keys(bone, pos, rot, scl, False)
    if worst_shift > 1e-3:  # in-place clips: the root sits at the origin unrotated, so component space is pelvis-parent space
        pos, rot, scl = [], [], []
        for f in range(n):
            t = local(all_poses[f], "pelvis")
            sh = pelvis_shift.get(f, (0.0, 0.0, 0.0))
            pos.append(unreal.Vector(t.translation.x + sh[0], t.translation.y + sh[1], t.translation.z + sh[2]))
            rot.append(t.rotation)
            scl.append(t.scale3d)
        ctrl.set_bone_track_keys("pelvis", pos, rot, scl, False)
    ctrl.close_bracket(False)
    return {"frames": [lo, hi], "bones": sorted(new_rot), "worst_pelvis_shift_cm": round(worst_shift, 2)}


def musket_weight(m, f):
    return smooth(f / float(MUSKET_EASE)) if "Present" in m["name"] else 1.0


def fore_grip(pose, yaw_deg):
    """Where the left grip belongs: MUSKET_FORE_CM from the right grip along the musket's line, a little below.
    The line is the soldier's forward (+Y) turned `yaw_deg` toward his left (+X)."""
    r = grip(pose, "right_hand")
    a = math.radians(yaw_deg)
    return (r[0] + MUSKET_FORE_CM * math.sin(a), r[1] + MUSKET_FORE_CM * math.cos(a), r[2] - MUSKET_DROP_CM)


def musket_error(seq, m, yaw_deg):
    """Worst distance of the left grip from the fore-grip over the frames where the hold is full."""
    worst = 0.0
    for f in range(int(m["frames"])):
        if musket_weight(m, f) >= 1.0:
            pose = pose_at(seq, f)
            worst = max(worst, norm(sub(grip(pose, "left_hand"), fore_grip(pose, yaw_deg))))
    return worst


def musket_yaw(poses, m):
    """The generated poses stand square to the front, so a musket pointing straight ahead from the right hand
    puts its fore-end out of the left arm's reach. Smallest turn of the musket toward his left, in steps of
    2 degrees, at which the left wrist can reach on every full-hold frame."""
    _, upper, lower, hbone, _ = ARMS["left_hand"]
    for yaw_deg in range(0, 72, 2):
        ok = True
        for f, pose in poses.items():
            if musket_weight(m, f) < 1.0:
                continue
            S, E, W = (v(world(pose, b).translation) for b in (upper, lower, hbone))
            Wt = add(W, sub(fore_grip(pose, yaw_deg), grip(pose, "left_hand")))
            if norm(sub(Wt, S)) > norm(sub(E, S)) + norm(sub(W, E)) - 0.5:
                ok = False
                break
        if ok:
            return float(yaw_deg)
    return 70.0


def fix_musket(seq, m):
    n = int(m["frames"])
    clav, upper, lower, hbone, _ = ARMS["left_hand"]
    poses = {f: pose_at(seq, f) for f in range(n)}
    yaw_deg = musket_yaw(poses, m)
    new_rot = {upper: {}, lower: {}, hbone: {}}
    for f in range(n):
        pose = poses[f]
        d = mul(sub(fore_grip(pose, yaw_deg), grip(pose, "left_hand")), musket_weight(m, f))
        S, E, W = (v(world(pose, b).translation) for b in (upper, lower, hbone))
        E2, W2 = two_bone(S, E, W, add(W, d))
        g_clav, g_up, g_lo, g_hand = (q(world(pose, b).rotation) for b in (clav, upper, lower, hbone))
        g_up2 = qnorm(qmul(from_to(sub(E, S), sub(E2, S)), g_up))
        g_lo2 = qnorm(qmul(from_to(sub(W, E), sub(W2, E2)), g_lo))
        new_rot[upper][f] = qnorm(qmul(qinv(g_clav), g_up2))
        new_rot[lower][f] = qnorm(qmul(qinv(g_up2), g_lo2))
        new_rot[hbone][f] = qnorm(qmul(qinv(g_lo2), g_hand))
    ctrl = seq.controller
    ctrl.open_bracket(unreal.Text("PA250 musket hold"), False)
    for bone, rots in new_rot.items():
        pos, rot, scl = [], [], []
        for f in range(n):
            t = local(poses[f], bone)
            pos.append(t.translation)
            scl.append(t.scale3d)
            r = rots[f]
            rot.append(unreal.Quat(r[0], r[1], r[2], r[3]))
        ctrl.set_bone_track_keys(bone, pos, rot, scl, False)
    ctrl.close_bracket(False)
    return yaw_deg


def notify_class(name):
    path = "%s/%s" % (NOTIFY_DIR, name)
    if not eal.does_asset_exist(path):
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.AnimNotify)
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, NOTIFY_DIR, unreal.Blueprint, factory)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        eal.save_loaded_asset(bp)
    return eal.load_blueprint_class(path)


def set_notifies(seq, m, classes):
    track = "PA250"
    if track in [str(t) for t in al.get_animation_notify_track_names(seq)]:
        al.remove_animation_notify_track(seq, track)
    al.add_animation_notify_track(seq, track)
    length = al.get_sequence_length(seq)
    for nfy in m.get("notifies", []):
        al.add_animation_notify_event(seq, track, min(float(nfy["time"]), length), classes[nfy["name"]])
    return len(m.get("notifies", []))


report, problems = {}, []
classes = {n: notify_class(n) for n in NOTIFY_NAMES} if STAGE in ("notifies", "all") else {}
for key in keys:
    m = clips[key]
    row = {"asset": path_of(key)}
    report[key] = row
    seq = unreal.load_asset(path_of(key)) if eal.does_asset_exist(path_of(key)) else None
    if not isinstance(seq, unreal.AnimSequence):
        row["missing"] = True
        problems.append("%s: no AnimSequence" % key)
        continue
    dirty = False
    if STAGE in ("fixup", "all") and targets_cm(m):
        if not eal.get_metadata_tag(seq, "PA250HandFix"):  # baked once; a second pass would start from the fixed pose
            row["hand_cm_before"] = round(hand_errors(seq, m), 2)
            row["fixup"] = fix_hands(seq, m)
            eal.set_metadata_tag(seq, "PA250HandFix", "1")
            dirty = True
    if STAGE in ("musket", "all") and m.get("name") in MUSKET_CLIPS and not eal.get_metadata_tag(seq, "PA250MusketFix"):
        yaw_deg = fix_musket(seq, m)
        eal.set_metadata_tag(seq, "PA250MusketFix", "1")
        eal.set_metadata_tag(seq, "PA250MusketYawDeg", "%.1f" % yaw_deg)
        dirty = True
    if STAGE in ("notifies", "all") and not m.get("transition"):
        row["notifies_set"] = set_notifies(seq, m, classes)
        dirty = True
    if dirty:
        eal.save_loaded_asset(seq, only_if_is_dirty=False)
    # ---- verification (always)
    row["skeleton"] = str(seq.get_skeleton().get_name())
    if "SK_AMERICAN_SOLDIER_Skeleton" not in row["skeleton"]:
        problems.append("%s: on skeleton %s" % (key, row["skeleton"]))
    row["length_s"] = round(al.get_sequence_length(seq), 3)
    want_len = (int(m["frames"]) - 1) / 30.0
    if abs(row["length_s"] - want_len) > 0.05:
        problems.append("%s: length %.3f s, expected %.3f" % (key, row["length_s"], want_len))
    have = sorted(str(n) for n in al.get_animation_notify_event_names(seq))
    want = sorted(n["name"] for n in m.get("notifies", []))
    row["notifies"] = have
    if not m.get("transition") and sorted(set(want)) != sorted(set(h for h in have)):
        problems.append("%s: notifies %s, expected %s" % (key, sorted(set(have)), sorted(set(want))))
    if not m.get("transition"):
        row["root_motion"] = bool(seq.get_editor_property("enable_root_motion"))
        if row["root_motion"] != bool(m.get("root_motion")):
            problems.append("%s: root motion flag %s" % (key, row["root_motion"]))
    if m.get("name") in MUSKET_CLIPS:
        row["musket_yaw_deg"] = float(str(eal.get_metadata_tag(seq, "PA250MusketYawDeg")) or 0.0)
        row["musket_cm"] = round(musket_error(seq, m, row["musket_yaw_deg"]), 2)
        if row["musket_cm"] > HAND_LIMIT_CM:
            problems.append("%s: left hand %.1f cm from the musket's fore-grip" % (key, row["musket_cm"]))
    if targets_cm(m):
        row["hand_cm"] = round(hand_errors(seq, m), 2)
        if row["hand_cm"] > HAND_LIMIT_CM:
            problems.append("%s: hand %.1f cm from its target" % (key, row["hand_cm"]))

with open(os.path.join(PASS, "ue_verify_report.json"), "w", encoding="utf-8") as f:
    json.dump({"clips": report, "problems": problems}, f, indent=1)
muskets = [r for r in report.values() if "musket_cm" in r]
unreal.log("PA250 finish: %d musket clips, worst left hand %.2f cm from the fore-grip" % (
    len(muskets), max([r["musket_cm"] for r in muskets] or [0.0])))
constrained = [r for r in report.values() if "hand_cm" in r]
unreal.log("PA250 finish: %d clips, %d with an AnimSequence, %d constrained (worst hand %.2f cm), %d problems" % (
    len(keys), sum(1 for r in report.values() if not r.get("missing")), len(constrained),
    max([r["hand_cm"] for r in constrained] or [0.0]), len(problems)))
for p in problems[:12]:
    unreal.log_warning("PA250 finish problem: " + p)
