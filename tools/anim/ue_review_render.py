"""Review reels for the animation pass: a flat test level, Level Sequences, Movie Render Queue.

Two stages, both in the scratch project (the clips, the soldier and the cannon must be there):
  build   UnrealEditor-Cmd <project> -run=pythonscript -script=tools/anim/ue_review_render.py -unattended -nullrhi
          makes /Game/Developers/AnimReview/L_Review_<Family> and SEQ_<Family>_<Camera> (old ones are replaced)
          (delete Content/Developers/AnimReview in the project first when rebuilding)
          and a Movie Render Queue preset MRQ_<Family>_<Camera> for each; render_plan.json lists them
  render  one process per reel, in the logged-in desktop session with a GPU (tools/anim/render_review.ps1):
          UnrealEditor-Cmd <project> <level> -game -LevelSequence=<SEQ> -MoviePipelineConfig=<MRQ> -windowed -ResX=1280 -ResY=720
          writes JPG frames to <AnimationPass>/review/<Family>/frames_<Camera>/ and exits; ffmpeg makes the MP4s.
          (The in-editor PIE executor below ends at once in an unattended editor here; it is kept for reference.)
Environment: PA250_ANIM_PASS, PA250_REVIEW_STAGE=build|render, PA250_REVIEW_ONLY=Gun,Volley,React

Reels (30 fps, 1280x720, three cameras each: Front, Side, High):
  Gun     the cannon at the origin and one man per role at his station, the drill played in order across all roles
  Volley  a ten-man line, Make Ready -> Present -> Fire, each man 0 to 4 frames (0 to 133 ms) late, seeded
  React   a twelve-man block and a blast at a marked point: the nearest men are knocked down, lie, and get up
          (knockdown -> transition -> get-up); the next ring staggers; the far men duck or are deafened
Props are separate actors keyed every frame from the hands (not attached to a bone, so no grip offsets are
guessed): the sponge-and-rammer staff, linstock, handspike and cartridge from tools/anim/make_props.py, and the
game's musket (SM_HS_WP01_MusketRifle_X_0) for the volley line. A two-handed prop lies along the line from the
right hand's grip to the left's; a prop carried in one hand stands upright in it. The musket is upright at
Make Ready and lies along the hands through Present and Fire, with its root in the right hand.
"""
import json
import math
import os
import random

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
STAGE = os.environ.get("PA250_REVIEW_STAGE", "build")
ONLY = [k for k in os.environ.get("PA250_REVIEW_ONLY", "Gun,Volley,React").split(",") if k]
DIR = "/Game/Developers/AnimReview"
GEN = "/Game/PA250/Anim/Generated"
SOLDIER = "/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER"
CANNON = "/Game/ThirdParty/PTM_Cannon/Mesh"
CAMERAS = ("Front", "Side", "High")
FPS = 30
eal = unreal.EditorAssetLibrary
manifest = json.load(open(os.path.join(PASS, "manifest.json"), encoding="utf-8"))
geo = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gun_geometry.json"), encoding="utf-8"))
clips, transitions = manifest["clips"], manifest.get("transitions", {})


def anim(key):
    fam = clips[key]["family"] if key in clips else clips[transitions[key]["from"]]["family"]
    a = unreal.load_asset("%s/%s/%s" % (GEN, fam, key))
    if not a:
        raise RuntimeError("no clip " + key)
    return a


def frames(key):
    return int(clips[key]["frames"] if key in clips else transitions[key]["frames"])


def spawn(cls, loc, yaw=0.0, label=None):
    a = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw))
    if label:
        a.set_actor_label(label)
    return a


def soldier_at(mesh, loc, yaw, label):
    a = spawn(unreal.SkeletalMeshActor, loc, yaw, label)
    a.skeletal_mesh_component.set_skinned_asset_and_update(mesh)
    return a


def face_yaw(frm, to):
    """Actor yaw that turns the soldier (mesh forward is +Y) from `frm` to look at `to`."""
    return math.degrees(math.atan2(to[1] - frm[1], to[0] - frm[0])) - 90.0


def base_level(name, sun_yaw):
    path = "%s/L_Review_%s" % (DIR, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(path):
        # Seen 2026-11-15: replacing a level that exists fails in a commandlet, the actors then go into an unsaved
        # world and the reel renders the old level with nothing bound. Delete Content/Developers/AnimReview first.
        raise RuntimeError("could not make %s: delete the old review assets on disk and build again" % path)
    floor = spawn(unreal.StaticMeshActor, (0, 0, 0), 0, "Floor")
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    floor.set_actor_scale3d(unreal.Vector(80, 80, 1))
    sun = spawn(unreal.DirectionalLight, (0, 0, 500), 0, "Sun")
    sun.set_actor_rotation(unreal.Rotator(0.0, -48.0, sun_yaw), False)  # the light travels this way: from the front camera onto the men
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(6.0)
    sky = spawn(unreal.SkyLight, (0, 0, 600), 0, "Sky")
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), 0, "Atmosphere")
    return path


def camera(label, loc, look_at, fov=60.0):
    cam = spawn(unreal.CineCameraActor, loc, 0, "Cam_" + label)
    d = (look_at[0] - loc[0], look_at[1] - loc[1], look_at[2] - loc[2])
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    cam.set_actor_rotation(unreal.Rotator(0.0, pitch, yaw), False)
    cc = cam.get_cine_camera_component()
    cc.set_editor_property("current_focal_length", 0.5 * cc.filmback.sensor_width / math.tan(math.radians(fov) / 2.0))
    cc.focus_settings.focus_method = unreal.CameraFocusMethod.DISABLE
    return cam


PROPS = "/Game/PA250/Props/GunTools"
MUSKET = "/Game/HS_WeaponPack_01/Meshes/MusketRifle/Meshes/SM_HS_WP01_MusketRifle_X_0"
_poses = {}
_rep = os.path.join(PASS, "ue_verify_report.json")
MUSKET_YAW = {k: r.get("musket_yaw_deg", 0.0) for k, r in json.load(open(_rep, encoding="utf-8"))["clips"].items()} \
    if os.path.exists(_rep) else {}


def clip_at(items, f):
    """Which clip and which of its frames a figure shows at reel frame f (a section repeats its clip)."""
    cur = items[0]
    for it in items:
        if it[0] <= f:
            cur = it
    return cur[1], max(0, f - cur[0]) % frames(cur[1])


def grips(loc, yaw, key, lf):
    """World positions of the right and left grip points (midway hand bone to last middle-finger bone)."""
    if (key, lf) not in _poses:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim(key), lf, unreal.AnimPoseEvaluationOptions())
        out = []
        for side in ("r", "l"):
            a = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_" + side, unreal.AnimPoseSpaces.WORLD).translation
            b = unreal.AnimPoseExtensions.get_bone_pose(pose, "middle_03_" + side, unreal.AnimPoseSpaces.WORLD).translation
            out.append(((a.x + b.x) / 2.0, (a.y + b.y) / 2.0, (a.z + b.z) / 2.0))
        _poses[(key, lf)] = out
    c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return [(loc[0] + c * g[0] - s_ * g[1], loc[1] + s_ * g[0] + c * g[1], loc[2] + g[2]) for g in _poses[(key, lf)]]


def vsub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def vadd(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def vmul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def vlen(a): return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
def vunit(a):
    n = vlen(a)
    return vmul(a, 1.0 / n) if n > 1e-6 else (1.0, 0.0, 0.0)


def along(origin, direction):
    """Key for a prop whose +X lies along `direction` with its origin at `origin`: (loc, (roll, pitch, yaw))."""
    d = vunit(direction)
    return origin, (0.0, math.degrees(math.atan2(d[2], math.hypot(d[0], d[1]))), math.degrees(math.atan2(d[1], d[0])))


def upright(x, y, z):
    return (x, y, z), (0.0, 90.0, 0.0)


def prop_actor(mesh_path, label):
    a = spawn(unreal.StaticMeshActor, (0, 0, -500), 0, label)
    a.set_mobility(unreal.ComponentMobility.MOVABLE)
    a.static_mesh_component.set_static_mesh(unreal.load_asset(mesh_path))
    return a


def make_sequence(name, total, cam, plays, hides, props=()):
    """plays: [(actor, [(start_frame, clip_key), ...])]; hides: [(actor, visible_from, visible_to)];
    props: [(actor, [(frame, loc, (roll, pitch, yaw)), ...])] keyed every frame."""
    path = "%s/%s" % (DIR, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    seq = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(FPS, 1))
    seq.set_playback_start(0)
    seq.set_playback_end(total)
    cb = seq.add_possessable(cam)
    cut = seq.add_track(unreal.MovieSceneCameraCutTrack)
    cs = cut.add_section()
    cs.set_range(0, total)
    cs.set_camera_binding_id(seq.get_binding_id(cb))
    for actor, items in plays:
        b = seq.add_possessable(actor)
        track = b.add_track(unreal.MovieSceneSkeletalAnimationTrack)
        for i, (start, key) in enumerate(items):
            end = items[i + 1][0] if i + 1 < len(items) else total
            sec = track.add_section()
            sec.set_range(start, max(start + 1, end))
            p = sec.get_editor_property("params")
            p.set_editor_property("animation", anim(key))
            sec.set_editor_property("params", p)
    bound = {}
    for actor, keys in props:
        b = bound[actor] = seq.add_possessable(actor)
        tt = b.add_track(unreal.MovieScene3DTransformTrack)
        ts = tt.add_section()
        ts.set_range(0, total)
        ch = ts.get_all_channels()
        lin = unreal.MovieSceneKeyInterpolation.LINEAR
        for f, loc, rot in keys:
            fn = unreal.FrameNumber(f)
            for i in range(3):
                ch[i].add_key(fn, loc[i], 0.0, unreal.MovieSceneTimeUnit.DISPLAY_RATE, lin)
                ch[3 + i].add_key(fn, rot[i], 0.0, unreal.MovieSceneTimeUnit.DISPLAY_RATE, lin)
    for actor, t0, t1 in hides:
        b = bound[actor] if actor in bound else seq.add_possessable(actor)
        vt = b.add_track(unreal.MovieSceneVisibilityTrack)
        vs = vt.add_section()
        vs.set_range(0, total)
        ch = vs.get_all_channels()[0]
        ch.add_key(unreal.FrameNumber(0), t0 <= 0)
        if t0 > 0:
            ch.add_key(unreal.FrameNumber(t0), True)
        if t1 < total:
            ch.add_key(unreal.FrameNumber(t1), False)
    eal.save_loaded_asset(seq)
    return path


def build_gun(mesh):
    level = base_level("Gun", 160.0)
    gun = spawn(unreal.StaticMeshActor, (0, 0, 0), 0, "Cannon")
    gun.static_mesh_component.set_static_mesh(unreal.load_asset(CANNON))
    muzzle = geo["bore_axis"]["origin_cm"]  # measured centre of the muzzle face
    vent = geo.get("vent_measured_cm") or geo["sockets"]["Vent"]
    bore = tuple(geo["bore_axis"]["direction"])  # out of the muzzle; the barrel is elevated, not level
    st = {k: v["cm"] for k, v in geo["stations"].items()}
    roles = {  # role: (station, what he faces, [(seconds, clip)])
        "Sponger": ("station_MuzzleR", muzzle, [(0, "AM_Gun_Sponge_v0"), (4, "AM_Gun_Idle_Sponger_v0"), (7, "AM_Gun_Ram_v0"),
                                               (10, "AM_Gun_Idle_Sponger_v1"), (18.5, "AM_Gun_Flinch_v0"), (20, "AM_Gun_Idle_Sponger_v2")]),
        "Ventsman": ("station_Vent", vent, [(0, "AM_Gun_Vent_v0"), (4, "AM_Gun_Idle_Ventsman_v0"), (10, "AM_Gun_Prime_v0"),
                                            (13, "AM_Gun_Idle_Ventsman_v1"), (18.5, "AM_Gun_Flinch_v1"), (20, "AM_Gun_Idle_Ventsman_v2")]),
        "Loader": ("station_MuzzleL", muzzle, [(0, "AM_Gun_Idle_Loader_v0"), (4, "AM_Gun_Load_v0"), (7, "AM_Gun_Idle_Loader_v1"),
                                               (18.5, "AM_Gun_Flinch_v2"), (20, "AM_Gun_Idle_Loader_v2")]),
        "Captain": ("Trail", muzzle, [(0, "AM_Gun_Observe_v0"), (4, "AM_Gun_Idle_Captain_v0"), (13, "AM_Gun_Lay_v0"),
                                      (16, "AM_Gun_Idle_Captain_v1"), (19, "AM_Gun_Observe_v1")]),
        "Handspike": ("TrailL", (st["Trail"][0] + 40, 0, 0), [(0, "AM_Gun_Idle_Handspike_v0"), (13, "AM_Gun_Traverse_v0"),
                                                             (16, "AM_Gun_Idle_Handspike_v1")]),
        "Firer": ("Linstock", vent, [(0, "AM_Gun_Idle_Firer_v0"), (16, "AM_Gun_Fire_v0"), (18.5, "AM_Gun_Idle_Firer_v1")]),
        "RunUpL": ("Crew_3", (st["Crew_3"][0] + 100, st["Crew_3"][1], 0), [(0, "AM_Gun_Idle_Ventsman_v1"), (20, "AM_Gun_RunUp_v0")]),
        "RunUpR": ("Crew_4", (st["Crew_4"][0] + 100, st["Crew_4"][1], 0), [(0, "AM_Gun_Idle_Ventsman_v2"), (20, "AM_Gun_RunUp_v1")]),
    }
    total = 23 * FPS
    plays, who = [], {}
    for role, (station, face, items) in roles.items():
        p = st[station]
        yaw = face_yaw(p, face)
        a = soldier_at(mesh, (p[0], p[1], 0), yaw, role)
        its = [(int(round(t * FPS)), k) for t, k in items]
        plays.append((a, its))
        who[role] = ((p[0], p[1], 0.0), yaw, its)
    props, hides = [], []
    staff, linstock, spike, cart = [], [], [], []
    load_start = next(s0 for s0, k in who["Loader"][2] if k.startswith("AM_Gun_Load"))
    load_frame = next(int(r["frame"]) for r in clips["AM_Gun_Load_v0"]["constraints"] if r.get("type") == "hands")
    for f in range(total + 1):
        loc, yaw, its = who["Sponger"]
        key, lf = clip_at(its, f)
        R, L = grips(loc, yaw, key, lf)
        if key.startswith(("AM_Gun_Sponge", "AM_Gun_Ram")) and vlen(vsub(L, R)) < 80.0:
            d = vunit(vsub(L, R))  # toward the muzzle: the left hand is the nearer one
            # How far the hands are off the bore axis (the +X line through the muzzle). On it, the staff is put
            # exactly on the axis; off it, the staff follows the hands but its head may never be past the muzzle
            # face, so it cannot pass through the barrel (user review 2026-11-16).
            def on_bore(g):  # the point of the bore line nearest g
                t = sum((g[i] - muzzle[i]) * bore[i] for i in range(3))
                return vadd(tuple(muzzle), vmul(bore, t))
            off = max(vlen(vsub(g, on_bore(g))) for g in (R, L))
            w = max(0.0, min(1.0, 1.0 - (off - 3.0) / 12.0))
            axis = vmul(bore, -1.0)  # into the barrel
            d = vunit(vadd(vmul(d, 1.0 - w), vmul(axis, w)))
            anchor = vadd(vmul(R, 1.0 - w), vmul(on_bore(R), w))
            head = vadd(anchor, vmul(d, 150.0))  # the working head, 150 cm past the right hand
            if w < 1.0 and head[0] < muzzle[0] + 6.0 and abs(d[0]) > 1e-3:
                anchor = vadd(anchor, vmul(d, (muzzle[0] + 6.0 - head[0]) / d[0]))
            o, r = along(vadd(anchor, vmul(d, 30.0)), d if key.startswith("AM_Gun_Sponge") else vmul(d, -1.0))
        else:
            o, r = upright(R[0], R[1], 120.0)
        staff.append((f, o, r))
        loc, yaw, its = who["Firer"]
        key, lf = clip_at(its, f)
        R, L = grips(loc, yaw, key, lf)
        if key.startswith("AM_Gun_Fire"):
            d = vunit(vsub((vent[0], vent[1], vent[2]), R))
            o, r = along(vadd(R, vmul(d, 60.0)), d)
        else:
            o, r = upright(R[0], R[1], 80.0)
        linstock.append((f, o, r))
        loc, yaw, its = who["Handspike"]
        key, lf = clip_at(its, f)
        R, L = grips(loc, yaw, key, lf)
        mid = vmul(vadd(R, L), 0.5)
        if key.startswith("AM_Gun_Traverse") and vlen(vsub(L, R)) > 12.0:
            hi_, lo_ = (R, L) if R[2] >= L[2] else (L, R)
            d = vunit(vsub(lo_, hi_))  # the working end goes down, toward the trail
            o, r = along(vadd(mid, vmul(d, 20.0)), d)
        else:
            o, r = upright(mid[0], mid[1], 85.0)
        spike.append((f, o, r))
        if load_start <= f <= load_start + load_frame:
            loc, yaw, its = who["Loader"]
            key, lf = clip_at(its, f)
            R, L = grips(loc, yaw, key, lf)
            o, r = along(R, vsub(R, (muzzle[0], muzzle[1], R[2])))  # powder bag first into the muzzle, the ball outward
            cart.append((f, o, r))
    props.append((prop_actor(PROPS + "/SM_SpongeRammer", "Prop_Staff"), staff))
    props.append((prop_actor(PROPS + "/SM_Linstock", "Prop_Linstock"), linstock))
    props.append((prop_actor(PROPS + "/SM_Handspike", "Prop_Handspike"), spike))
    glass = []
    loc, yaw, its = who["Captain"]
    for f in range(total + 1):
        key, lf = clip_at(its, f)
        R, L = grips(loc, yaw, key, lf)
        d = vsub(L, R)
        if key.startswith("AM_Gun_Observe") and 12.0 < vlen(d) < 45.0:
            o, r = along(R, vunit(d))  # at his eye, along the hands
        else:
            o, r = along(R, (0.0, 0.0, -1.0))  # closed in his right hand, hanging at his side
        glass.append((f, o, r))
    props.append((prop_actor(PROPS + "/SM_Spyglass", "Prop_Spyglass"), glass))
    for k in range(6):  # ready round shot on the ground by the loader
        s_ = prop_actor(PROPS + "/SM_RoundShot", "Shot_%d" % k)
        s_.set_actor_location(unreal.Vector(235.0 + 9.5 * (k % 3), -128.0 - 9.5 * (k // 3), 4.5), False, False)
    c_actor = prop_actor(PROPS + "/SM_Cartridge", "Prop_Cartridge")
    props.append((c_actor, cart))
    hides.append((c_actor, load_start, load_start + load_frame))  # in his hand until it is in the muzzle
    cams = {"Front": camera("Front", (620, 0, 170), (0, 0, 100)), "Side": camera("Side", (0, 620, 170), (0, 0, 100)),
            "High": camera("High", (430, -430, 520), (-20, 0, 80))}
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    return level, {c: make_sequence("SEQ_Gun_%s" % c, total, cams[c], plays, hides, props) for c in CAMERAS}


def build_volley(mesh):
    level = base_level("Volley", -110.0)
    rng = random.Random(1775)
    total = 60 + 45 + 45 - 4  # the earliest man's Fire ends here
    plays, props = [], []
    for i in range(10):
        a = soldier_at(mesh, ((i - 4.5) * 70.0, 0, 0), 0.0, "Man_%02d" % i)
        j = rng.randint(0, 4)
        v = i % 4
        t = -j  # up to 4 frames ahead of the others; a section that began late left him in the reference pose
        # a man who is late holds the first frame of Make Ready: the section starts late and the pose before it is the clip's start
        items = [(t, "AM_Volley_MakeReady_v%d" % v)]
        t += frames("AM_Volley_MakeReady_v%d" % v)
        items.append((t, "AM_Volley_Present_v%d" % v))
        t += frames("AM_Volley_Present_v%d" % v)
        items.append((t, "AM_Volley_Fire_v%d" % v))
        plays.append((a, items))
        keys, line = [], (0.0, 1.0, 0.0)
        loc = ((i - 4.5) * 70.0, 0.0, 0.0)
        for f in range(total + 1):
            key, lf = clip_at(items, f)
            R, L = grips(loc, 0.0, key, lf)
            # With both hands on it the musket lies along them, root in the right hand (the recover, the aim).
            # Before the right hand has come to the stock (shouldered arms at the start of Make Ready) it stands
            # upright on the left hand, which carries the butt.
            d = vsub(L, R)
            x = max(0.0, min(1.0, (70.0 - vlen(d)) / 30.0)) if vlen(d) > 15.0 else 0.0
            w = x * x * (3.0 - 2.0 * x)  # 0: carried on the left hand; 1: in both hands
            carry_o = (L[0], L[1], L[2] + 26.0)  # the musket's root is 26 cm above the butt, which sits on the left hand
            both_d = vunit(d) if vlen(d) > 1.0 else (0.0, 0.0, 1.0)
            o = vadd(vmul(carry_o, 1.0 - w), vmul(R, w))
            o, r = along(o, vunit(vadd(vmul((0.0, 0.0, 1.0), 1.0 - w), vmul(both_d, w))))
            keys.append((f, o, r))
        props.append((prop_actor(MUSKET, "Musket_%02d" % i), keys))
    cams = {"Front": camera("Front", (0, 620, 160), (0, 0, 110)), "Side": camera("Side", (-620, 120, 170), (0, 0, 110)),
            "High": camera("High", (300, 520, 430), (0, 0, 90))}
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    return level, {c: make_sequence("SEQ_Volley_%s" % c, total, cams[c], plays, [], props) for c in CAMERAS}


def build_react(mesh):
    level = base_level("React", -110.0)
    blast = (0.0, 330.0)  # in front of the block's middle; the men face +Y
    mark = spawn(unreal.StaticMeshActor, (blast[0], blast[1], 10), 0, "BlastMark")
    mark.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cone"))
    mark.set_actor_scale3d(unreal.Vector(0.6, 0.6, 0.6))
    t_blast = 1 * FPS
    total = t_blast + 60 + 2 * FPS + 50 + 90  # knockdown, two seconds lying, a 50-frame transition, the get-up
    plays, hides = [], []
    men = [((c - 1.5) * 150.0, -r * 150.0) for r in range(3) for c in range(4)]
    order = sorted(range(len(men)), key=lambda i: math.hypot(men[i][0] - blast[0], men[i][1] - blast[1]))
    tr_by_from = {}
    for key, t in transitions.items():
        if clips[t["from"]]["name"].startswith("AM_React_BlastKnockdown"):
            tr_by_from.setdefault(t["from"], []).append((key, t))
    for rank, i in enumerate(order):
        x, y = men[i]
        dx, dy = blast[0] - x, blast[1] - y  # the blast, seen from a man facing +Y (his left is +X)
        side = "F" if abs(dy) >= abs(dx) and dy > 0 else "B" if abs(dy) >= abs(dx) else "L" if dx > 0 else "R"
        a = soldier_at(mesh, (x, y, 0), 0.0, "Man_%02d" % i)
        idle = "AM_Gun_Idle_Ventsman_v%d" % (i % 3)
        if rank < 4:
            kd = "AM_React_BlastKnockdown_%s_v%d" % (side, rank % 2)
            tkey, t = max(tr_by_from[kd], key=lambda kt: (kt[1]["frames"], kt[0]))  # the longest, so all four get up together
            t_tr = t_blast + frames(kd) + 2 * FPS  # lies still two seconds
            t_up = t_tr + t["frames"]
            plays.append((a, [(0, idle), (t_blast, kd), (t_tr, tkey)]))
            hides.append((a, 0, t_up))
            # the get-up is in its own space: a second figure stands where the transition ends, turned to match
            end = unreal.AnimPoseExtensions.get_bone_pose(
                unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim(tkey), t["frames"] - 1, unreal.AnimPoseEvaluationOptions()),
                "pelvis", unreal.AnimPoseSpaces.WORLD).translation
            start = unreal.AnimPoseExtensions.get_bone_pose(
                unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim(t["to"]), 0, unreal.AnimPoseEvaluationOptions()),
                "pelvis", unreal.AnimPoseSpaces.WORLD).translation
            turn = -float(t["ragdoll_alignment"]["turn_deg"])  # Kimodo turns about +Y; Unreal's yaw runs the other way
            c, s = math.cos(math.radians(turn)), math.sin(math.radians(turn))
            ox = end.x - (c * start.x - s * start.y)
            oy = end.y - (s * start.x + c * start.y)
            b = soldier_at(mesh, (x + ox, y + oy, 0), turn, "Man_%02d_up" % i)
            plays.append((b, [(t_up, t["to"])]))
            hides.append((b, t_up, total))
        elif rank < 8:
            plays.append((a, [(0, idle), (t_blast + 2, "AM_React_BlastStagger_%s_v%d" % (side, rank % 2)),
                              (t_blast + 2 + 2 * FPS, "AM_React_Deafened_v%d" % (rank % 2))]))
        else:
            plays.append((a, [(0, idle), (t_blast + 3, "AM_React_NearMiss_v%d" % (rank % 4)),
                              (t_blast + 3 + 45, "AM_Gun_Idle_Captain_v%d" % (rank % 3))]))
    cams = {"Front": camera("Front", (0, 900, 220), (0, -80, 80)), "Side": camera("Side", (-820, 60, 200), (0, -80, 80)),
            "High": camera("High", (520, 620, 620), (0, -80, 40))}
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    return level, {c: make_sequence("SEQ_React_%s" % c, total, cams[c], plays, hides) for c in CAMERAS}


def make_preset(fam, cam):
    """A saved Movie Render Queue config for one reel: deferred pass, JPG frames, 1280x720, its own folder."""
    name = "MRQ_%s_%s" % (fam, cam)
    path = "%s/%s" % (DIR, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    cfg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DIR, unreal.MoviePipelinePrimaryConfig, unreal.MoviePipelinePrimaryConfigFactory())
    cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
    cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_JPG)
    out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
    out.output_directory = unreal.DirectoryPath(os.path.join(PASS, "review", fam, "frames_" + cam).replace("\\", "/"))
    out.output_resolution = unreal.IntPoint(1280, 720)
    out.file_name_format = "{frame_number}"
    out.zero_pad_frame_numbers = 4
    out.override_existing_output = True
    eal.save_loaded_asset(cfg, only_if_is_dirty=False)
    return path


def build():
    mesh = unreal.load_asset(SOLDIER)
    plan = {}
    for fam, fn in (("Gun", build_gun), ("Volley", build_volley), ("React", build_react)):
        if fam in ONLY:
            level, seqs = fn(mesh)
            plan[fam] = {"level": level, "sequences": seqs, "presets": {c: make_preset(fam, c) for c in seqs}}
            unreal.log("PA250 review: built %s with %d sequences" % (level, len(seqs)))
    old = {}
    path = os.path.join(PASS, "review", "render_plan.json")
    if os.path.exists(path):
        old = json.load(open(path, encoding="utf-8"))
    old.update(plan)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(old, open(path, "w", encoding="utf-8"), indent=1)


def render():
    plan = json.load(open(os.path.join(PASS, "review", "render_plan.json"), encoding="utf-8"))
    sub = unreal.get_editor_subsystem(unreal.MoviePipelineQueueSubsystem)
    queue = sub.get_queue()
    queue.delete_all_jobs()
    for fam in ONLY:
        for cam, seq in plan[fam]["sequences"].items():
            job = queue.allocate_new_job(unreal.MoviePipelineExecutorJob)
            job.job_name = "%s_%s" % (fam, cam)
            job.sequence = unreal.SoftObjectPath(seq)
            job.map = unreal.SoftObjectPath(plan[fam]["level"])
            cfg = job.get_configuration()
            cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
            cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_JPG)
            out = cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
            out.output_directory = unreal.DirectoryPath(os.path.join(PASS, "review", fam, "frames_" + cam))
            out.output_resolution = unreal.IntPoint(1280, 720)
            out.file_name_format = "{frame_number}"
            out.zero_pad_frame_numbers = 4
            out.override_existing_output = True
    global _executor
    _executor = unreal.MoviePipelinePIEExecutor()

    def finished(executor, success):
        unreal.log("PA250 review: render queue finished, success=%s" % success)
        unreal.SystemLibrary.quit_editor()

    _executor.on_executor_finished_delegate.add_callable_unique(finished)
    sub.render_queue_with_executor_instance(_executor)
    unreal.log("PA250 review: %d render jobs started" % len(queue.get_jobs()))


if STAGE == "render":
    render()
else:
    build()
