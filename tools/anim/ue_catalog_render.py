"""A labelled catalogue reel of every clip variant on the soldier, so each one is seen at least once.

  build   UnrealEditor-Cmd <scratch project> -run=pythonscript -script=tools/anim/ue_catalog_render.py -unattended -nullrhi
  render  tools/anim/render_review.ps1 -Plan catalog_plan.json -Only Gun,Volley,React,Transitions -Cams Quarter
Environment: PA250_ANIM_PASS; PA250_CATALOG_ROOT (default /Game/PA250/Anim/Generated; /Game/PA250/Anim/Committed
shows the repository's clips when they are staged there).

It reads the clips from the folder, not from manifest.json. One level and one sequence per family. Up to four men
stand side by side; each clip gets a time slot as long as the clip, every variant plays in it at once (v0 on the
viewer's left), and a sign behind the men names the clip. A man with no variant in a slot is hidden. Transitions are
grouped by their two clips. The volley clips that hold a musket (Make Ready, Present, Fire, standing and kneeling)
are shown with the game's musket lying along the two hands, root in the right hand, because an aim seen bare from the
front reads as hands over the face; every other clip is bare. PA250_CATALOG_ONLY (comma-separated clip names) limits
the reel to those clips. Delete Content/Developers/AnimCatalog before a rebuild.
"""
import json
import math
import os
import re

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
ROOT = os.environ.get("PA250_CATALOG_ROOT", "/Game/PA250/Anim/Generated")
DIR = "/Game/Developers/AnimCatalog"
ONLY = [k for k in os.environ.get("PA250_CATALOG_ONLY", "").split(",") if k]
MUSKET = "/Game/HS_WeaponPack_01/Meshes/MusketRifle/Meshes/SM_HS_WP01_MusketRifle_X_0"
MUSKET_CLIPS = ("MakeReady", "Present", "Fire")
ape = unreal.AnimPoseExtensions
SOLDIER = "/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER"
FPS, MEN, SPACING = 30, 4, 170.0
eal = unreal.EditorAssetLibrary
al = unreal.AnimationLibrary


def spawn(cls, loc, yaw=0.0, label=None):
    a = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw))
    if label:
        a.set_actor_label(label)
    return a


def groups():
    """{family: [(label, [asset paths, variant order])]}; transitions grouped by their pair."""
    out = {"Gun": {}, "Volley": {}, "React": {}, "Transitions": {}}
    for fam in ("Gun", "Volley", "React"):
        for path in eal.list_assets("%s/%s" % (ROOT, fam), recursive=False):
            pkg = path.split(".")[0]
            key = pkg.rsplit("/", 1)[-1]
            if "_to_" in key:
                if ONLY:
                    continue
                out["Transitions"].setdefault(re.sub(r"_v\d+_v\d+$", "", key), []).append(pkg)
            else:
                name = re.sub(r"_v\d+$", "", key)
                if ONLY and name not in ONLY:
                    continue
                out[fam].setdefault(name, []).append(pkg)
    return {f: sorted((k, sorted(v)) for k, v in g.items()) for f, g in out.items()}


def build_family(fam, items, mesh):
    level = "%s/L_Catalog_%s" % (DIR, fam)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(level):
        raise RuntimeError("could not make %s: delete Content/Developers/AnimCatalog and build again" % level)
    floor = spawn(unreal.StaticMeshActor, (0, 0, 0), 0, "Floor")
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    floor.set_actor_scale3d(unreal.Vector(80, 80, 1))
    sun = spawn(unreal.DirectionalLight, (0, 0, 500), 0, "Sun")
    sun.set_actor_rotation(unreal.Rotator(0.0, -48.0, -125.0), False)
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(6.0)
    sky = spawn(unreal.SkyLight, (0, 0, 600), 0, "Sky")
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), 0, "Atmosphere")
    men = []
    for i in range(MEN):  # v0 on the viewer's left: the camera looks down -Y, so its left is -X
        a = spawn(unreal.SkeletalMeshActor, ((i - 1.5) * SPACING, 0, 0), 0.0, "Man_v%d" % i)
        a.skeletal_mesh_component.set_skinned_asset_and_update(mesh)
        men.append(a)
        tag = spawn(unreal.TextRenderActor, ((i - 1.5) * SPACING, 120.0, 2.0), 90.0, "Tag_v%d" % i)
        tag.text_render.set_text(unreal.Text("v%d" % i))
        tag.text_render.set_world_size(34.0)
        tag.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        tag.text_render.set_text_render_color(unreal.Color(20, 20, 20, 255))
        tag.set_actor_rotation(unreal.Rotator(0.0, 90.0, 90.0), False)  # flat on the ground, read from the camera
    # In-place families are framed tight; the reactions travel a metre or two, so they get more room.
    far = fam in ("React", "Transitions")
    cx, cy, cz = (-90.0, 720.0, 225.0) if far else (-70.0, 560.0, 185.0)
    cam = spawn(unreal.CineCameraActor, (cx, cy, cz), 0, "Cam_Quarter")
    look = (0.0, -20.0, 90.0)
    d = (look[0] - cx, look[1] - cy, look[2] - cz)
    cam.set_actor_rotation(unreal.Rotator(0.0, math.degrees(math.atan2(d[2], math.hypot(d[0], d[1]))),
                                          math.degrees(math.atan2(d[1], d[0]))), False)
    cc = cam.get_cine_camera_component()
    cc.set_editor_property("current_focal_length", 0.5 * cc.filmback.sensor_width / math.tan(math.radians(66.0) / 2.0))
    cc.focus_settings.focus_method = unreal.CameraFocusMethod.DISABLE

    slots, t = [], 0
    for label, paths in items:
        seqs = [unreal.load_asset(p) for p in paths[:MEN]]
        n = max(int(round(al.get_sequence_length(s) * FPS)) for s in seqs) + 1
        slots.append((t, n, label, seqs, len(paths)))
        t += n
    total = t
    signs = []
    for start, n, label, seqs, count in slots:
        # The far camera is above head height, so a sign well behind the men and above the horizon is clear of them
        # (at head height just behind them it lay across their faces, catalogue of 2026-11-18).
        s = spawn(unreal.TextRenderActor, (0.0, -900.0, 330.0) if far else (0.0, -150.0, 212.0), 90.0, "Sign_" + label)
        s.text_render.set_text(unreal.Text("%s   %d variant%s   %.1f s" % (label, count, "" if count == 1 else "s", (n - 1) / float(FPS))))
        s.text_render.set_world_size(64.0 if far else 30.0)
        s.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        s.text_render.set_text_render_color(unreal.Color(15, 15, 15, 255))
        signs.append(s)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()

    name = "SEQ_Catalog_%s" % fam
    seq = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(FPS, 1))
    seq.set_playback_start(0)
    seq.set_playback_end(total)
    cb = seq.add_possessable(cam)
    cut = seq.add_track(unreal.MovieSceneCameraCutTrack).add_section()
    cut.set_range(0, total)
    cut.set_camera_binding_id(seq.get_binding_id(cb))
    for i, man in enumerate(men):
        b = seq.add_possessable(man)
        track = b.add_track(unreal.MovieSceneSkeletalAnimationTrack)
        vis = b.add_track(unreal.MovieSceneVisibilityTrack).add_section()
        vis.set_range(0, total)
        ch = vis.get_all_channels()[0]
        for start, n, label, seqs, count in slots:
            ch.add_key(unreal.FrameNumber(start), i < len(seqs))
            if i < len(seqs):
                sec = track.add_section()
                sec.set_range(start, start + n)
                p = sec.get_editor_property("params")
                p.set_editor_property("animation", seqs[i])
                sec.set_editor_property("params", p)
    if fam == "Volley" and eal.does_asset_exist(MUSKET):
        lin = unreal.MovieSceneKeyInterpolation.LINEAR
        for i, man in enumerate(men):
            gun = spawn(unreal.StaticMeshActor, (0, 0, -500), 0, "Musket_v%d" % i)
            gun.set_mobility(unreal.ComponentMobility.MOVABLE)
            gun.static_mesh_component.set_static_mesh(unreal.load_asset(MUSKET))
            b = seq.add_possessable(gun)
            ts = b.add_track(unreal.MovieScene3DTransformTrack).add_section()
            ts.set_range(0, total)
            ch = ts.get_all_channels()
            vis = b.add_track(unreal.MovieSceneVisibilityTrack).add_section()
            vis.set_range(0, total)
            vch = vis.get_all_channels()[0]
            base = man.get_actor_location()
            for start, n, label, seqs, count in slots:
                held = i < len(seqs) and any(label.endswith(m) for m in MUSKET_CLIPS)
                vch.add_key(unreal.FrameNumber(start), held)
                if not held:
                    continue
                line = (0.0, 0.0, 1.0)
                for f in range(n):
                    pose = ape.get_anim_pose_at_frame(seqs[i], min(f, n - 2), unreal.AnimPoseEvaluationOptions())
                    g = []
                    for side in ("r", "l"):
                        a = ape.get_bone_pose(pose, "hand_" + side, unreal.AnimPoseSpaces.WORLD).translation
                        m = ape.get_bone_pose(pose, "middle_03_" + side, unreal.AnimPoseSpaces.WORLD).translation
                        g.append(((a.x + m.x) / 2.0, (a.y + m.y) / 2.0, (a.z + m.z) / 2.0))
                    d = (g[1][0] - g[0][0], g[1][1] - g[0][1], g[1][2] - g[0][2])
                    ln = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
                    if 15.0 < ln < 70.0:
                        line = (d[0] / ln, d[1] / ln, d[2] / ln)
                    loc = (base.x + g[0][0], base.y + g[0][1], base.z + g[0][2])
                    rot = (0.0, math.degrees(math.atan2(line[2], math.hypot(line[0], line[1]))),
                           math.degrees(math.atan2(line[1], line[0])))
                    fn = unreal.FrameNumber(start + f)
                    for k in range(3):
                        ch[k].add_key(fn, loc[k], 0.0, unreal.MovieSceneTimeUnit.DISPLAY_RATE, lin)
                        ch[3 + k].add_key(fn, rot[k], 0.0, unreal.MovieSceneTimeUnit.DISPLAY_RATE, lin)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    for (start, n, label, seqs, count), sign in zip(slots, signs):
        b = seq.add_possessable(sign)
        vis = b.add_track(unreal.MovieSceneVisibilityTrack).add_section()
        vis.set_range(0, total)
        ch = vis.get_all_channels()[0]
        if start > 0:
            ch.add_key(unreal.FrameNumber(0), False)
        ch.add_key(unreal.FrameNumber(start), True)
        if start + n < total:
            ch.add_key(unreal.FrameNumber(start + n), False)
    eal.save_loaded_asset(seq)

    preset_name = "MRQ_Catalog_%s" % fam
    cfg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        preset_name, DIR, unreal.MoviePipelinePrimaryConfig, unreal.MoviePipelinePrimaryConfigFactory())
    cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
    cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_JPG)
    out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
    out.output_directory = unreal.DirectoryPath(os.path.join(PASS, "review", "Catalog_" + fam, "frames_Quarter").replace("\\", "/"))
    out.output_resolution = unreal.IntPoint(1280, 720)
    out.file_name_format = "{frame_number}"
    out.zero_pad_frame_numbers = 4
    out.override_existing_output = True
    eal.save_loaded_asset(cfg, only_if_is_dirty=False)
    index = [{"clip": label, "start_s": round(start / float(FPS), 2), "seconds": round((n - 1) / float(FPS), 2),
              "variants": count, "shown": len(seqs)} for start, n, label, seqs, count in slots]
    unreal.log("PA250 catalog: %s  %d clips, %d frames (%.1f s)" % (fam, len(slots), total, total / float(FPS)))
    return {"level": level, "sequences": {"Quarter": "%s/%s" % (DIR, name)}, "presets": {"Quarter": "%s/%s" % (DIR, preset_name)},
            "index": index, "root": ROOT}


mesh = unreal.load_asset(SOLDIER)
plan = {}
for fam, items in groups().items():
    if items:
        plan["Catalog_" + fam] = build_family(fam, items, mesh)
os.makedirs(os.path.join(PASS, "review"), exist_ok=True)
with open(os.path.join(PASS, "review", "catalog_plan.json"), "w", encoding="utf-8") as f:
    json.dump(plan, f, indent=1)
