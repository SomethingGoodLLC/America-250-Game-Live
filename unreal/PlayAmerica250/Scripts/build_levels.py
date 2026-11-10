"""Editor Python: builds PA250 levels and imports assets. Idempotent.

Run headless:

  UnrealEditor-Cmd PlayAmerica250.uproject \\
    -run=pythonscript -script=Scripts/build_levels.py \\
    -unattended -nullrhi

Selects which levels with env PA250_LEVELS (comma list of: campaign,
lexington, diplomacy; default all).
"""
import math
import os

import unreal

LOG = unreal.log
MAPS = "/Game/PA250/Maps"
PROJECT = unreal.SystemLibrary.get_project_directory()

# Dawn exposure pair (tuned from rendered previews; see PROGRESS.md "Visual pass").
DAWN_SUN_LUX = 10.0
DAWN_EXPOSURE_EV = 1.6
# Shared dressing materials, created in build_lexington().
STONE_MAT = WOOD_DARK_MAT = BRICK_MAT = GLASS_MAT = None


def import_texture(src, dest_dir, name):
    if not os.path.exists(src):
        LOG("PA250: texture source missing: " + src)
        return None
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.EditorAssetLibrary.load_asset(dest_dir + "/" + name)
    if asset:
        asset.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        asset.set_editor_property(
            "mip_gen_settings",
            unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
        )
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def new_level(path):
    """Create (or recreate) an empty level and leave it loaded."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    disk = os.path.join(
        PROJECT,
        "Content",
        path.replace("/Game/", "").lstrip("/") + ".umap",
    )
    # Unload + delete asset + wipe disk so NewLevel validation cannot see a leftover.
    try:
        les.load_level("/Engine/Maps/Templates/Template_Default")
    except Exception:  # noqa: BLE001
        pass
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    for extra in (disk, disk.replace(".umap", ".uexp"), disk.replace(".umap", ".ubulk")):
        if os.path.isfile(extra):
            try:
                os.remove(extra)
            except OSError as e:
                LOG("PA250: could not remove %s (%s)" % (extra, e))
    ok = les.new_level(path)
    if not ok:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            les.load_level(path)
            # Clear actors so rebuild is idempotent when NewLevel is blocked.
            try:
                for a in unreal.get_editor_subsystem(
                    unreal.EditorActorSubsystem
                ).get_all_level_actors():
                    a.destroy_actor()
            except Exception as e:  # noqa: BLE001
                LOG("PA250: clear-level failed: %s" % e)
            LOG("PA250: reused existing level %s" % path)
            return les
        raise RuntimeError("new_level failed: " + path)
    return les


def set_game_mode(cls_name):
    world = unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem
    ).get_editor_world()
    ws = world.get_world_settings()
    ws.set_editor_property(
        "default_game_mode",
        getattr(unreal, cls_name).static_class(),
    )


def save(les):
    les.save_current_level()


# ---------------------------------------------------------------- shared helpers

_TOOLS = None


def tools():
    global _TOOLS
    if _TOOLS is None:
        _TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
    return _TOOLS


def make_material(name, rgb):
    """Material instance of BasicShapeMaterial with a flat Color."""
    path = "/Game/PA250/Materials/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mic = unreal.EditorAssetLibrary.load_asset(path)
    else:
        mic = tools().create_asset(
            name,
            "/Game/PA250/Materials",
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
        parent = unreal.EditorAssetLibrary.load_asset(
            "/Engine/BasicShapes/BasicShapeMaterial"
        )
        mic.set_editor_property("parent", parent)
    color = unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        mic, "Color", color
    )
    unreal.EditorAssetLibrary.save_loaded_asset(mic)
    return mic


def make_tuning():
    path = "/Game/PA250/Data/DA_MusketTuning"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)
    else:
        asset = tools().create_asset(
            "DA_MusketTuning",
            "/Game/PA250/Data",
            unreal.MusketTuning,
            unreal.DataAssetFactory(),
        )
    v = unreal.MusketTuningValues()
    v.set_editor_property("reload_time", 7.0)  # sweet-path = sum of active-reload phases
    v.set_editor_property("projectile_speed", 3000.0)
    v.set_editor_property("damage", 40.0)
    # Nested conquest defaults (BF2-style).
    try:
        conquest = unreal.ConquestTuning()
        conquest.set_editor_property("capture_seconds", 3.5)
        conquest.set_editor_property("capture_per_attacker_scale", 0.35)
        conquest.set_editor_property("bleed_interval_seconds", 5.0)
        conquest.set_editor_property("post_radius", 1000.0)
        conquest.set_editor_property("time_limit_seconds", 720.0)
        conquest.set_editor_property("bots_per_side", 16)
        v.set_editor_property("conquest", conquest)
    except Exception as e:
        LOG("PA250: nested ConquestTuning not applied yet (%s)" % e)
    asset.set_editor_property("values", v)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    make_conquest_data_assets()


def make_conquest_data_assets():
    """Create DA_WeaponTuning / DA_BotTuning / DA_MeleeTuning / DA_FactionVisuals with C++ defaults."""
    specs = (
        ("DA_WeaponTuning", "PA250WeaponTuning"),
        ("DA_BotTuning", "PA250BotTuning"),
        ("DA_MeleeTuning", "PA250MeleeTuning"),
        ("DA_FactionVisuals", "PA250FactionVisuals"),
        ("DA_CombatProfiles", "PA250CombatProfiles"),
    )
    for name, cls_name in specs:
        path = "/Game/PA250/Data/%s" % name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            continue
        cls = getattr(unreal, cls_name, None)
        if cls is None:
            LOG("PA250: skip %s (class %s not bound yet)" % (name, cls_name))
            continue
        asset = tools().create_asset(name, "/Game/PA250/Data", cls, unreal.DataAssetFactory())
        if asset:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            LOG("PA250: created %s" % path)


def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0), label=None, scale=None):
    a = unreal.EditorLevelLibrary.spawn_actor_from_class(
        cls,
        unreal.Vector(*loc),
        unreal.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]),
    )
    if label:
        a.set_actor_label(label)
    if scale:
        a.set_actor_scale3d(unreal.Vector(*scale))
    return a


def tag(actor, name):
    actor.set_editor_property("tags", [unreal.Name(name)])


def set_props(obj, pairs, what):
    """Set editor properties, logging (not hiding) the ones this engine lacks."""
    for prop, val in pairs:
        try:
            obj.set_editor_property(prop, val)
        except Exception as exc:  # noqa: BLE001
            LOG("PA250: %s.%s skipped (%s)" % (what, prop, exc))


# ---------------------------------------------------------------- ThirdParty lookup

THIRD_PARTY_ROOT = "/Game/ThirdParty"
# Open World Demo Collection (Fab), installed at Content/KiteDemo. Used when present,
# after anything the user drops into Content/ThirdParty.
KITE = "/Game/KiteDemo/Environments/"
KITE_ASSETS = {
    "tree": (KITE + "Trees/HillTree_02/HillTree_02", KITE + "Trees/HillTree_Tall_02/HillTree_Tall_02"),
    "wall_rock": (
        KITE + "Rocks/River_Rock_01/SM_River_Rock_01",
        KITE + "Rocks/Medium_Boulder_002/Medium_Boulder_LowPoly",
        KITE + "Rocks/Boulder_05a/SM_Boulder05a",
    ),
    "field_boulder": (
        KITE + "Rocks/LargePlainsBoulder002/SM_LargePlainsBoulder002",
        KITE + "Rocks/Medium_Boulder_001/Medium_Boulder_001",
        KITE + "Rocks/GroundRevealRock001/SM_GroundRevealRock001",
    ),
    "grass_clump": (KITE + "Foliage/Grass/FieldGrass/SM_FieldGrass_01",),
    "flowers": (
        KITE + "Foliage/Flowers/Buttercup/SM_Buttercup_Patch_01",
        KITE + "Foliage/Flowers/Yarrow/SM_Yarrow",
    ),
    "stump": (KITE + "Trees/Tree_Stump_01/Tree_Stump_01",),
    "tex_grass": (KITE + "GroundTiles/Grass/T_GDC_Grass01_D_NoisyAlpha",),
    "tex_grass_n": (KITE + "GroundTiles/Grass/T_Ground_Grass_N",),
    "tex_dirt": (KITE + "GroundTiles/ForestPath_001/T_Forest_Path_001A_D_R",),
    "tex_dirt_n": (KITE + "GroundTiles/ForestPath_001/T_Forest_Path_001A_N",),
    "tex_macro": (KITE + "GroundTiles/Grass/T_MacroVariation",),
}


# KiteDemo's legacy UE4 foliage materials (SpeedTree / subsurface / two-sided foliage)
# render with white base colour on this UE 5.8 Mac setup, even in the pack's own
# Showcase map. Foliage therefore gets simple PA250 masked materials built from the
# pack's textures (also cheaper: no SpeedTree wind or subsurface).
KITE_FOLIAGE_MATS = {
    # mesh role/path suffix: [(slot-name substring, material name, base tex, mask tex or None, roughness)]
    "HillTree_02": [
        ("Leaves", "MI_PA250_OakLeaves", "Trees/HillTree_02/HillTree_01_Atlas_Tex", None, 0.7),
        ("Fronds", "MI_PA250_OakLeaves", "Trees/HillTree_02/HillTree_01_Atlas_Tex", None, 0.7),
        ("Branches", "MI_PA250_OakBark", "Trees/HillTree_02/T_Craghead_Oak_Tile_01_D_Tex", "", 0.9),
        ("Billboard", "MI_PA250_OakBillboard", "Trees/HillTree_02/HillTree_01_Atlas_Billboards_Tex", None, 0.8),
    ],
    "HillTree_Tall_02": [
        ("Leaves", "MI_PA250_OakLeaves", "Trees/HillTree_02/HillTree_01_Atlas_Tex", None, 0.7),
        ("Fronds", "MI_PA250_OakLeaves", "Trees/HillTree_02/HillTree_01_Atlas_Tex", None, 0.7),
        ("Branches", "MI_PA250_OakBark", "Trees/HillTree_02/T_Craghead_Oak_Tile_01_D_Tex", "", 0.9),
        ("Billboard", "MI_PA250_OakTallBillboard", "Trees/HillTree_Tall_02/HillTree_Tall_01_Atlas_Billboards_Tex", None, 0.8),
    ],
    "SM_FieldGrass_01": [
        ("", "MI_PA250_FieldGrass", "Foliage/Grass/FieldGrass/T_FieldGrass_01_D", None, 0.85),
    ],
    "SM_Yarrow": [("", "MI_PA250_Yarrow", "Foliage/Flowers/Yarrow/T_Yarrow_D", "Foliage/Flowers/Yarrow/T_Yarrow_M", 0.85)],
    "SM_Buttercup_Patch_01": [
        ("", "MI_PA250_Buttercup", "Foliage/Flowers/Buttercup/T_Buttercup_Patch_01_D",
         "Foliage/Flowers/Buttercup/T_Buttercup_Patch_01_M", 0.85),
    ],
}
_FOLIAGE_MAT_CACHE = {}


def make_foliage_material(name, base_tex, mask_tex, roughness):
    """Masked, two-sided, default-lit material: base = texture RGB, mask = texture alpha
    (or a separate mask texture's R). mask_tex == "" means opaque (bark)."""
    if name in _FOLIAGE_MAT_CACHE:
        return _FOLIAGE_MAT_CACHE[name]
    path = "/Game/PA250/Materials/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = tools().create_asset(name, "/Game/PA250/Materials", unreal.Material, unreal.MaterialFactoryNew())
    M = unreal
    opaque = mask_tex == ""
    set_props(mat, (
        ("blend_mode", M.BlendMode.BLEND_OPAQUE if opaque else M.BlendMode.BLEND_MASKED),
        ("two_sided", not opaque),
        ("opacity_mask_clip_value", 0.4),
        ("used_with_instanced_static_meshes", True),
    ), name)
    base = _expr(mat, M.MaterialExpressionTextureSample, -600, 0,
                 texture=unreal.EditorAssetLibrary.load_asset(KITE + base_tex))
    M.MaterialEditingLibrary.connect_material_property(base, "RGB", M.MaterialProperty.MP_BASE_COLOR)
    if not opaque:
        if mask_tex:
            mask = _expr(mat, M.MaterialExpressionTextureSample, -600, 250,
                         texture=unreal.EditorAssetLibrary.load_asset(KITE + mask_tex),
                         sampler_type=M.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            M.MaterialEditingLibrary.connect_material_property(mask, "R", M.MaterialProperty.MP_OPACITY_MASK)
        else:
            M.MaterialEditingLibrary.connect_material_property(base, "A", M.MaterialProperty.MP_OPACITY_MASK)
    rough = _expr(mat, M.MaterialExpressionConstant, -300, 200, r=float(roughness))
    M.MaterialEditingLibrary.connect_material_property(rough, "", M.MaterialProperty.MP_ROUGHNESS)
    M.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    _FOLIAGE_MAT_CACHE[name] = mat
    return mat


def foliage_overrides(mesh_path):
    """[(slot index, material)] replacing KiteDemo's foliage materials on this mesh."""
    spec = KITE_FOLIAGE_MATS.get(mesh_path.rsplit("/", 1)[-1])
    if not spec:
        return []
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    out = []
    for i, sm in enumerate(mesh.static_materials):
        slot = str(sm.material_slot_name)
        for key, mname, base, mask, rough in spec:
            if key in slot:
                out.append((i, make_foliage_material(mname, base, mask, rough)))
                break
    return out


def kite_all(role):
    """Every existing KiteDemo asset for a role (for variety); empty if not installed."""
    return [p for p in KITE_ASSETS.get(role, ()) if unreal.EditorAssetLibrary.does_asset_exist(p)]
_ASSET_CACHE = {}
_CHOSEN = {}


def _third_party_index():
    """Sorted list of (lower-case asset name, object path, class) under Content/ThirdParty."""
    if "_index" in _ASSET_CACHE:
        return _ASSET_CACHE["_index"]
    rows = []
    if unreal.EditorAssetLibrary.does_directory_exist(THIRD_PARTY_ROOT):
        reg = unreal.AssetRegistryHelpers.get_asset_registry()
        for data in reg.get_assets_by_path(THIRD_PARTY_ROOT, recursive=True):
            cls = str(data.asset_class_path.asset_name)
            path = str(data.package_name)
            rows.append((str(data.asset_name).lower(), path, cls))
    rows.sort(key=lambda r: r[1])
    _ASSET_CACHE["_index"] = rows
    return rows


def resolve_asset(role, candidates, keywords=(), cls="StaticMesh", min_extent=0.0, max_extent=1e9):
    """Pick a ThirdParty asset for `role`: exact candidate paths first, then the first
    (path-sorted) asset of class `cls` whose name contains any keyword and whose bounds
    fit [min_extent, max_extent] cm (largest half-extent). None means placeholder.
    Cached per role and recorded in Saved/PA250/lexington_assets.json."""
    if role in _ASSET_CACHE:
        return _ASSET_CACHE[role]
    found = None
    for path in candidates:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            found = path
            break
    if not found and keywords:
        found = _keyword_pick(keywords, cls, min_extent, max_extent)
    if not found:
        kite = kite_all(role)
        found = kite[0] if kite else None
    _ASSET_CACHE[role] = found
    _CHOSEN[role] = found or "placeholder"
    LOG("PA250: asset %s -> %s" % (role, found or "placeholder"))
    return found


def _keyword_pick(keywords, cls, min_extent, max_extent):
    """First path-sorted Content/ThirdParty asset of `cls` matching a keyword and size."""
    for name, path, c in _third_party_index():
        if c != cls or not any(k in name for k in keywords):
            continue
        if cls == "Texture2D" and not any(
            t in name for t in ("_d", "_bc", "basecolor", "albedo", "diffuse", "_col")
        ):
            continue  # base colour only, never normal/roughness/AO maps
        if cls == "StaticMesh":
            mesh = unreal.EditorAssetLibrary.load_asset(path)
            ext = mesh.get_bounds().box_extent if mesh else None
            big = max(ext.x, ext.y, ext.z) if ext else 0.0
            if not (min_extent <= big <= max_extent):
                continue
        return path
    return None


def write_asset_choices():
    import json

    out = os.path.join(PROJECT, "Saved", "PA250", "lexington_assets.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(dict(sorted(_CHOSEN.items())), f, indent=2)
        f.write("\n")


# ---------------------------------------------------------------- placement


def fit_mesh(label, mesh_path, center, size, mat, rot=(0, 0, 0), mobility=None):
    """Place any static mesh so its bounds box has `size` (cm) centred on `center`.
    Works for engine BasicShapes, LevelPrototyping and arbitrary Fab meshes
    (pivot and native size come from the mesh bounds). mat=None keeps the
    mesh's own materials."""
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    a = spawn(unreal.StaticMeshActor, (0, 0, 0), rot, label)
    comp = a.static_mesh_component
    comp.set_mobility(mobility or unreal.ComponentMobility.STATIC)
    comp.set_static_mesh(mesh)
    b = mesh.get_bounds()
    ext, org = b.box_extent, b.origin
    scale = unreal.Vector(
        size[0] / max(2.0 * ext.x, 1e-3),
        size[1] / max(2.0 * ext.y, 1e-3),
        size[2] / max(2.0 * ext.z, 1e-3),
    )
    a.set_actor_scale3d(scale)
    rotator = unreal.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2])
    off = unreal.MathLibrary.greater_greater_vector_rotator(
        unreal.Vector(org.x * scale.x, org.y * scale.y, org.z * scale.z), rotator
    )
    a.set_actor_location(
        unreal.Vector(center[0] - off.x, center[1] - off.y, center[2] - off.z), False, False
    )
    if mat:
        comp.set_material(0, mat)
    if max(size) < 300.0:
        # Detail pieces (windows, rails, posts, stones): no Lumen cards / distance
        # fields. Hundreds of them cost ~11 ms/frame of surface-cache captures on an M3.
        set_props(comp, (
            ("affect_dynamic_indirect_lighting", False),
            ("affect_distance_field_lighting", False),
        ), label)
    return a


def fit_native(label, mesh_path, ground_point, height, rot=(0, 0, 0)):
    """ThirdParty mesh: uniform scale to `height` cm, base on the ground, own materials."""
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    ext = mesh.get_bounds().box_extent
    s = height / max(2.0 * ext.z, 1e-3)
    size = (2.0 * ext.x * s, 2.0 * ext.y * s, height)
    x, y, z = ground_point
    a = fit_mesh(label, mesh_path, (x, y, z + height * 0.5), size, None, rot)
    for i, mat in foliage_overrides(mesh_path):
        a.static_mesh_component.set_material(i, mat)
    return a


def block(label, loc, size, mat, rot_yaw=0.0, mesh="/Engine/BasicShapes/Cube", rot=None):
    """Graybox block. `size` in cm (x, y, z); loc is the centre."""
    return fit_mesh(label, mesh, loc, size, mat, rot if rot else (0, rot_yaw, 0))


def strip_on_ground(label, p0, p1, width, height, mat, lift=0.0):
    """Segment between two ground points, resting on the terrain at its midpoint."""
    cx, cy = (p0[0] + p1[0]) / 2.0, (p0[1] + p1[1]) / 2.0
    length = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    yaw = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
    z = ground_z(cx, cy) + lift + height / 2.0
    return block(label, (cx, cy, z), (length, width, height), mat, rot_yaw=yaw)


# ---------------------------------------------------------------- terrain

GREEN_XY = (-500.0, -1000.0)  # centroid of the road triangle around the Common
TRIANGLE = ((-3000, -4000), (2500, -2500), (-1000, 3500))
# (p0, p1, width cm). The Concord road is the widest, most worn track.
ROADS = (
    (TRIANGLE[0], TRIANGLE[1], 450),
    (TRIANGLE[1], TRIANGLE[2], 450),
    (TRIANGLE[2], TRIANGLE[0], 450),
    ((2500, -2500), (13500, -700), 600),
    ((2500, -2500), (30000, 1500), 600),
    ((-3000, -4000), (-14000, -5200), 750),
    ((-14000, -5200), (-30000, -6500), 750),
    ((-1000, 3500), (-14000, 2600), 520),
)
# Building footprints (centre, size w,d,h). Kept clear of the capture posts.
MEETINGHOUSE = ((-3400, -5650), (2200, 1500, 900))
TAVERN = ((-5300, 1500), (1900, 1100, 760))
# Flat pads (x, y, radius): buildings, posts, spawn clusters.
PADS = (
    (MEETINGHOUSE[0][0], MEETINGHOUSE[0][1], 1700),
    (TAVERN[0][0], TAVERN[0][1], 1400),
    (GREEN_XY[0], GREEN_XY[1], 1600),
    (-2800, -4800, 900),
    (-3600, 1400, 900),
    (-5800, 300, 1100),
    (10100, -900, 1300),
)
TERRAIN_X = (-30000.0, 32000.0)  # asymmetric on purpose: import axis check
TERRAIN_Y = (-25000.0, 26000.0)
TERRAIN_CELL = 250.0


def _smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def _raw_height(x, y):
    d = math.hypot(x - GREEN_XY[0], y - GREEN_XY[1])
    amp = 45.0 + 255.0 * _smoothstep(3500.0, 12000.0, d) + 700.0 * _smoothstep(16000.0, 28000.0, d)
    n = (
        0.55 * math.sin(x / 2900.0 + 0.7) * math.cos(y / 3400.0 - 0.4)
        + 0.30 * math.sin((0.6 * x + y) / 2100.0 + 1.3)
        + 0.15 * math.cos((x - 0.8 * y) / 1300.0)
    )
    return amp * n


def ground_z(x, y):
    """Terrain height (cm) used for the mesh and for every placed actor."""
    h = _raw_height(x, y)
    for px, py, r in PADS:
        w = 1.0 - _smoothstep(r, r * 1.9, math.hypot(x - px, y - py))
        if w > 0.0:
            h = h * (1.0 - w) + _raw_height(px, py) * w
    return h


def _mask_hlsl():
    """HLSL for the Custom node: float2(road, trampled) from absolute world XY (cm).
    Generated from ROADS / GREEN_XY / post locations so level and material agree."""
    lines = [
        "float2 p = WP.xy;",
        "float road = 0;",
        "float2 a, b, ab; float t, d, hw;",
    ]
    for p0, p1, w in ROADS:
        lines += [
            "a = float2(%.1f, %.1f); b = float2(%.1f, %.1f); hw = %.1f;" % (p0[0], p0[1], p1[0], p1[1], w * 0.5),
            "ab = b - a; t = saturate(dot(p - a, ab) / dot(ab, ab)); d = length(p - (a + ab * t));",
            "road = max(road, 1 - smoothstep(hw * 0.55, hw * 1.2, d));",
        ]
    lines += [
        "float tr = 0.55 * (1 - smoothstep(900, 2600, length(p - float2(%.1f, %.1f))));" % GREEN_XY,
        "tr = max(tr, 0.45 * (1 - smoothstep(300, 1100, length(p - float2(-2800, -4800)))));",
        "tr = max(tr, 0.45 * (1 - smoothstep(300, 1100, length(p - float2(-3600, 1400)))));",
        "return float2(road, tr);",
    ]
    return "\n".join(lines)


def write_terrain_gltf(path):
    """Heightfield as glTF (Y-up metres), UV0 = world XY / 10 m. Road and trampled
    masks are computed in the material from world position (see _mask_hlsl), so they
    never depend on importer handling of vertex colours or extra UV sets."""
    import base64
    import json
    import struct

    nx = int((TERRAIN_X[1] - TERRAIN_X[0]) / TERRAIN_CELL) + 1
    ny = int((TERRAIN_Y[1] - TERRAIN_Y[0]) / TERRAIN_CELL) + 1
    pos, nrm, col = bytearray(), bytearray(), bytearray()
    lo = [1e9, 1e9, 1e9]
    hi = [-1e9, -1e9, -1e9]
    e = TERRAIN_CELL
    for j in range(ny):
        y = TERRAIN_Y[0] + j * e
        for i in range(nx):
            x = TERRAIN_X[0] + i * e
            z = ground_z(x, y)
            # UE (x, y, z) cm -> glTF (x, z, y) m; UE's importer swaps Y/Z back.
            g = (x / 100.0, z / 100.0, y / 100.0)
            for k in range(3):
                lo[k] = min(lo[k], g[k])
                hi[k] = max(hi[k], g[k])
            pos += struct.pack("<3f", *g)
            dzdx = (ground_z(x + e, y) - ground_z(x - e, y)) / (2 * e)
            dzdy = (ground_z(x, y + e) - ground_z(x, y - e)) / (2 * e)
            ln = math.sqrt(dzdx * dzdx + dzdy * dzdy + 1.0)
            nrm += struct.pack("<3f", -dzdx / ln, 1.0 / ln, -dzdy / ln)
            col += struct.pack("<2f", x / 1000.0, y / 1000.0)
    idx = bytearray()
    for j in range(ny - 1):
        for i in range(nx - 1):
            a = j * nx + i
            b, c, d = a + 1, a + nx, a + nx + 1
            # Counter-clockwise seen from +Y (up) in glTF space.
            idx += struct.pack("<3I", a, c, b)
            idx += struct.pack("<3I", b, c, d)
    nv = nx * ny
    blobs = [pos, nrm, col, idx]
    offsets, total = [], 0
    for blob in blobs:
        offsets.append(total)
        total += len(blob)
    buf = b"".join(bytes(b) for b in blobs)
    views = [
        {"buffer": 0, "byteOffset": offsets[k], "byteLength": len(blobs[k])}
        for k in range(4)
    ]
    views[3]["target"] = 34963
    for k in range(3):
        views[k]["target"] = 34962
    doc = {
        "asset": {"version": "2.0", "generator": "PA250 build_levels.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "SM_PA250_Terrain"}],
        "meshes": [
            {
                "name": "SM_PA250_Terrain",
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 3,
                    }
                ],
            }
        ],
        "buffers": [
            {
                "byteLength": len(buf),
                "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode(),
            }
        ],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": nv, "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": nv, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": nv, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(idx) // 4, "type": "SCALAR"},
        ],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(doc, f)
    LOG("PA250: terrain glTF %dx%d verts -> %s" % (nx, ny, path))


def import_terrain_mesh():
    """Generate + import the rolling ground; complex collision so nav and pawns follow it."""
    dest = "/Game/PA250/Meshes"
    src = os.path.join(PROJECT, "Saved", "PA250", "gen", "SM_PA250_Terrain.gltf")
    write_terrain_gltf(src)
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(dest, recursive=True):
        unreal.EditorAssetLibrary.delete_asset(str(data.package_name))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = None
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(dest, recursive=True):
        if str(data.asset_class_path.asset_name) == "StaticMesh":
            mesh = unreal.EditorAssetLibrary.load_asset(str(data.package_name))
            break
    if not mesh:
        raise RuntimeError("terrain import produced no StaticMesh")
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    mesh.set_editor_property("body_setup", body)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    b = mesh.get_bounds()
    LOG(
        "PA250: terrain bounds origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"
        % (b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z)
    )
    return mesh


# ---------------------------------------------------------------- materials


def _expr(mat, cls, x, y, **props):
    e = unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        try:
            e.set_editor_property(k, v)
        except Exception as exc:  # noqa: BLE001
            LOG("PA250: %s.%s skipped (%s)" % (cls.__name__, k, exc))
    return e


def _link(a, a_out, b, b_in):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, a_out, b, b_in)


def make_meadow_material():
    """Meadow for the terrain mesh.

    Colour = grass/dry-grass by macro noise (~60 m), broken up by fine noise (~3 m),
    trampled toward worn grass and dirt roads from world-position masks. When
    ThirdParty grass/dirt textures exist they replace the flat colours (world-
    aligned UVs), keeping the noise and road masks so tiling never reads.
    """
    path = "/Game/PA250/Materials/M_PA250_Meadow"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = tools().create_asset(
        "M_PA250_Meadow", "/Game/PA250/Materials", unreal.Material, unreal.MaterialFactoryNew()
    )
    M = unreal

    def noise(scale, levels, y):
        # Texture-based gradient noise: a few ALU + one fetch per level. The default
        # computational Simplex cost ~13 fps on the full-screen ground on this Mac.
        return _expr(
            mat, M.MaterialExpressionNoise, -900, y,
            scale=scale, levels=levels, output_min=0.0, output_max=1.0, quality=1,
            noise_function=M.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX,
        )

    def tex_xy(path, tile_cm, y, normal=False):
        """World-aligned (XY / tile) sample of a texture asset."""
        wp = _expr(mat, M.MaterialExpressionWorldPosition, -1400, y)
        xy = _expr(mat, M.MaterialExpressionComponentMask, -1250, y, r=True, g=True, b=False, a=False)
        _link(wp, "", xy, "")
        div = _expr(mat, M.MaterialExpressionDivide, -1100, y, const_b=float(tile_cm))
        _link(xy, "", div, "A")
        props = {"texture": unreal.EditorAssetLibrary.load_asset(path)}
        if normal:
            props["sampler_type"] = M.MaterialSamplerType.SAMPLERTYPE_NORMAL
        smp = _expr(mat, M.MaterialExpressionTextureSample, -900, y, **props)
        _link(div, "", smp, "UVs")
        return smp

    macro_tex = resolve_asset("tex_macro", (), cls="Texture2D")
    if macro_tex:
        # Two scales of the pack's macro-variation map: cheaper than procedural noise
        # and breaks the grass tile completely.
        macro_s = tex_xy(macro_tex, 9000.0, -300)
        macro = _expr(mat, M.MaterialExpressionComponentMask, -700, -300, r=True, g=False, b=False, a=False)
        _link(macro_s, "", macro, "")
        fine_s0 = tex_xy(macro_tex, 700.0, 0)
        fine = _expr(mat, M.MaterialExpressionComponentMask, -700, 0, r=False, g=True, b=False, a=False)
        _link(fine_s0, "", fine, "")
    else:
        macro = noise(1.0 / 6000.0, 2, -300)
        fine = noise(1.0 / 450.0, 2, 0)
    wp_abs = _expr(mat, M.MaterialExpressionWorldPosition, -1100, 300)
    wp_in = M.CustomInput()
    wp_in.set_editor_property("input_name", "WP")
    vc = _expr(
        mat, M.MaterialExpressionCustom, -900, 300,
        code=_mask_hlsl(), output_type=M.CustomMaterialOutputType.CMOT_FLOAT2,
        description="PA250 road+trampled masks",
        inputs=[wp_in],
    )
    _link(wp_abs, "", vc, "WP")

    def colour(rgb, role, keywords, y, tile_cm):
        tex = resolve_asset(role, (), keywords, cls="Texture2D")
        if not tex:
            return _expr(mat, M.MaterialExpressionConstant3Vector, -600, y,
                         constant=M.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        wp = _expr(mat, M.MaterialExpressionWorldPosition, -1200, y)
        xy = _expr(mat, M.MaterialExpressionComponentMask, -1050, y, r=True, g=True, b=False, a=False)
        _link(wp, "", xy, "")
        div = _expr(mat, M.MaterialExpressionDivide, -900, y, const_b=float(tile_cm))
        _link(xy, "", div, "A")
        smp = _expr(mat, M.MaterialExpressionTextureSample, -600, y,
                    texture=unreal.EditorAssetLibrary.load_asset(tex))
        _link(div, "", smp, "UVs")
        rgb = _expr(mat, M.MaterialExpressionComponentMask, -450, y, r=True, g=True, b=True, a=False)
        _link(smp, "", rgb, "")
        return rgb

    grass = colour((0.20, 0.33, 0.11), "tex_grass", ("grass",), -600, 350)
    dry = _expr(mat, M.MaterialExpressionConstant3Vector, -600, -450,
                constant=M.LinearColor(0.36, 0.36, 0.16, 1.0))
    worn = _expr(mat, M.MaterialExpressionConstant3Vector, -600, 450,
                 constant=M.LinearColor(0.30, 0.27, 0.15, 1.0))
    dirt = colour((0.30, 0.21, 0.12), "tex_dirt", ("dirt", "mud", "soil"), 600, 450)

    # macro: grass <-> dry patches (only part-way, keeps it a meadow)
    macro_a = _expr(mat, M.MaterialExpressionMultiply, -700, -250, const_b=0.55)
    _link(macro, "", macro_a, "A")
    field = _expr(mat, M.MaterialExpressionLinearInterpolate, -400, -400)
    _link(grass, "", field, "A")
    _link(dry, "", field, "B")
    _link(macro_a, "", field, "Alpha")
    # fine: brightness break-up 0.75..1.2
    fine_s = _expr(mat, M.MaterialExpressionLinearInterpolate, -600, 100, const_a=0.84, const_b=1.12)
    _link(fine, "", fine_s, "Alpha")
    field2 = _expr(mat, M.MaterialExpressionMultiply, -250, -300)
    _link(field, "", field2, "A")
    _link(fine_s, "", field2, "B")
    # trampled grass from vertex G
    tramp = _expr(mat, M.MaterialExpressionLinearInterpolate, -100, -200)
    _link(field2, "", tramp, "A")
    _link(worn, "", tramp, "B")
    tramp_m = _expr(mat, M.MaterialExpressionComponentMask, -700, 380, r=False, g=True, b=False, a=False)
    _link(vc, "", tramp_m, "")
    _link(tramp_m, "", tramp, "Alpha")  # trampled grass
    # dirt road from vertex R, edges eaten by the fine noise
    edge = _expr(mat, M.MaterialExpressionLinearInterpolate, -500, 250, const_a=0.6, const_b=1.5)
    _link(fine, "", edge, "Alpha")
    road_a = _expr(mat, M.MaterialExpressionMultiply, -300, 300)
    road_m = _expr(mat, M.MaterialExpressionComponentMask, -700, 330, r=True, g=False, b=False, a=False)
    _link(vc, "", road_m, "")
    _link(road_m, "", road_a, "A")  # dirt road
    _link(edge, "", road_a, "B")
    road_sat = _expr(mat, M.MaterialExpressionSaturate, -150, 300)
    _link(road_a, "", road_sat, "")
    dirt_s = _expr(mat, M.MaterialExpressionMultiply, -250, 500)
    _link(dirt, "", dirt_s, "A")
    _link(fine_s, "", dirt_s, "B")
    final = _expr(mat, M.MaterialExpressionLinearInterpolate, 100, 0)
    _link(tramp, "", final, "A")
    _link(dirt_s, "", final, "B")
    _link(road_sat, "", final, "Alpha")
    M.MaterialEditingLibrary.connect_material_property(final, "", M.MaterialProperty.MP_BASE_COLOR)
    grass_n = resolve_asset("tex_grass_n", (), cls="Texture2D")
    dirt_n = resolve_asset("tex_dirt_n", (), cls="Texture2D")
    if grass_n and dirt_n:
        n_lerp = _expr(mat, M.MaterialExpressionLinearInterpolate, 100, 600)
        _link(tex_xy(grass_n, 350.0, 700, normal=True), "", n_lerp, "A")
        _link(tex_xy(dirt_n, 450.0, 850, normal=True), "", n_lerp, "B")
        _link(road_sat, "", n_lerp, "Alpha")
        M.MaterialEditingLibrary.connect_material_property(n_lerp, "", M.MaterialProperty.MP_NORMAL)
    rough = _expr(mat, M.MaterialExpressionLinearInterpolate, 100, 250, const_a=0.95, const_b=0.85)
    _link(road_sat, "", rough, "Alpha")
    M.MaterialEditingLibrary.connect_material_property(rough, "", M.MaterialProperty.MP_ROUGHNESS)
    M.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


# ---------------------------------------------------------------- dressing

CUBE = "/Engine/BasicShapes/Cube"
SQRT2 = math.sqrt(2.0)


def _windows(label, cx, cy, base, w, d, h, trim, glass):
    rows = [0.32] if h < 550 else [0.28, 0.68]
    cols = max(2, int(w / 420))
    n = 0
    for r in rows:
        z = base + h * r
        for side in (-1, 1):
            for c in range(cols):
                x = cx - w * 0.5 + w * (c + 0.5) / cols
                if side > 0 and r == rows[0] and c == cols // 2:
                    continue  # door goes here
                y = cy + side * (d * 0.5 + 4)
                block("%s_WinF_%d" % (label, n), (x, y, z), (120, 10, 160), trim)
                block("%s_WinG_%d" % (label, n), (x, cy + side * (d * 0.5 + 8), z), (90, 8, 125), glass)
                n += 1
        for side in (-1, 1):
            x = cx + side * (w * 0.5 + 4)
            block("%s_WinF_%d" % (label, n), (x, cy, z), (10, 120, 160), trim)
            block("%s_WinG_%d" % (label, n), (cx + side * (w * 0.5 + 8), cy, z), (8, 90, 125), glass)
            n += 1


def building_shell(label, xy, size, body_mat, roof_mat, trim_mat, glass_mat, role, candidates,
                   keywords, chimneys=0):
    """Colonial building. ThirdParty mesh (own materials, fit to the footprint height) if
    present; else clapboard box + stone foundation + 45-degree gable roof with eaves and
    trimmed gable ends, sash windows, door and optional chimneys."""
    w, d, h = size
    cx, cy = xy
    base = ground_z(cx, cy)
    mesh = resolve_asset(role, candidates, keywords, min_extent=300.0, max_extent=4000.0)
    if mesh:
        return fit_native(label, mesh, (cx, cy, base), h + d * 0.5)
    block(label + "_Foundation", (cx, cy, base - 40), (w + 30, d + 30, 160), STONE_MAT)
    body = block(label, (cx, cy, base + h * 0.5), (w, d, h), body_mat)
    top = base + h
    eave = 55.0
    # Gable: square prism rolled 45 deg, diagonal = depth, centred on the wall top.
    block(label + "_Gable", (cx, cy, top), (w, d / SQRT2, d / SQRT2), body_mat, rot=(0, 0, 45))
    # Roof: larger prism, 20 cm proud of the gable faces, eaves overhang by `eave`.
    # 2 cm shorter than the body so the white gable triangle shows at each end with a
    # roof-coloured barge border.
    D = d + 2 * eave
    block(label + "_Roof", (cx, cy, top + 20 - eave), (w - 2, D / SQRT2, D / SQRT2), roof_mat, rot=(0, 0, 45))
    _windows(label, cx, cy, base, w, d, h, trim_mat, glass_mat)
    block(label + "_Door", (cx, cy + d * 0.5 + 6, base + 115), (130, 12, 230), WOOD_DARK_MAT)
    block(label + "_DoorTrim", (cx, cy + d * 0.5 + 3, base + 125), (170, 8, 260), trim_mat)
    ridge = top + d * 0.5 + 20
    for k in range(chimneys):
        s = -1 if k == 0 else 1
        block("%s_Chimney_%d" % (label, k), (cx + s * w * 0.32, cy, ridge - 40), (110, 110, 380), BRICK_MAT)
    return body


def belfry(label, xy, ridge_z, white, roof):
    cx, cy = xy
    block(label, (cx, cy, ridge_z + 150), (420, 420, 700), white)
    block(label + "_Trim", (cx, cy, ridge_z + 510), (480, 480, 30), white)
    block(label + "_Lantern", (cx, cy, ridge_z + 650), (330, 330, 260), white,
          mesh="/Game/LevelPrototyping/Meshes/SM_ChamferCube")
    for s in (-1, 1):
        block("%s_Louver_%d" % (label, s), (cx, cy + s * 168, ridge_z + 650), (180, 6, 170), GLASS_MAT)
    block(label + "_Spire", (cx, cy, ridge_z + 980), (300, 300, 420), roof, mesh="/Engine/BasicShapes/Cone")


_SCATTER = {}


def scatter(key, mesh_path, loc, rot, scale, collide=False, cull=(0.0, 0.0), shadows=True, gi=True):
    """Queue one instance of `mesh_path` into the hierarchical-instance group `key`.
    Groups become one actor each in flush_scatter(), so thousands of grass clumps or
    wall stones cost a handful of actors."""
    g = _SCATTER.setdefault(key, {"mesh": mesh_path, "xf": [], "collide": collide,
                                  "cull": cull, "shadows": shadows, "gi": gi})
    g["xf"].append(unreal.Transform(
        unreal.Vector(*loc), unreal.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2]),
        unreal.Vector(scale, scale, scale)))


def flush_scatter():
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for key in sorted(_SCATTER):
        g = _SCATTER[key]
        host = spawn(unreal.StaticMeshActor, (0, 0, 0), (0, 0, 0), "Scatter_" + key)
        host.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
        root = sds.k2_gather_subobject_data_for_instance(host)[0]
        params = unreal.AddNewSubobjectParams(
            parent_handle=root,
            new_class=unreal.HierarchicalInstancedStaticMeshComponent,
            blueprint_context=None,
        )
        handle, fail = sds.add_new_subobject(params)
        comp = lib.get_object(lib.get_data(handle))
        if comp is None:
            LOG("PA250: scatter %s failed (%s)" % (key, fail))
            continue
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(unreal.EditorAssetLibrary.load_asset(g["mesh"]))
        for i, mat in foliage_overrides(g["mesh"]):
            comp.set_material(i, mat)
        comp.set_collision_profile_name("BlockAll" if g["collide"] else "NoCollision")
        comp.set_cast_shadow(g["shadows"])
        if g["cull"][1] > 0:
            comp.set_cull_distances(int(g["cull"][0]), int(g["cull"][1]))
        if not g["gi"]:
            set_props(comp, (
                ("affect_dynamic_indirect_lighting", False),
                ("affect_distance_field_lighting", False),
            ), key)
        comp.add_instances(g["xf"], False, True)
        LOG("PA250: scatter %s %d x %s" % (key, len(g["xf"]), g["mesh"]))
    _SCATTER.clear()


def _mesh_extent(path):
    e = unreal.EditorAssetLibrary.load_asset(path).get_bounds().box_extent
    return max(e.x, e.y, e.z), e


def _seg_dist(x, y, p0, p1):
    vx, vy = p1[0] - p0[0], p1[1] - p0[1]
    t = ((x - p0[0]) * vx + (y - p0[1]) * vy) / max(vx * vx + vy * vy, 1e-6)
    t = min(1.0, max(0.0, t))
    return math.hypot(x - (p0[0] + vx * t), y - (p0[1] + vy * t))


def keep_clear(x, y, margin=0.0):
    """True if (x, y) is off the roads, building pads and capture posts."""
    for p0, p1, w in ROADS:
        if _seg_dist(x, y, p0, p1) < w * 0.6 + margin:
            return False
    for (bx, by), (w, d, _h) in (MEETINGHOUSE, TAVERN):
        if abs(x - bx) < w * 0.5 + 250 + margin and abs(y - by) < d * 0.5 + 250 + margin:
            return False
    for px, py in (GREEN_XY, (-2800, -4800), (-3600, 1400)):
        if math.hypot(x - px, y - py) < 450 + margin:
            return False
    return True


def place_tree(label, xy, trunk_mat, canopy_mat, rng):
    x, y = xy
    z = ground_z(x, y)
    mesh = resolve_asset(
        "tree",
        ("/Game/ThirdParty/Foliage/SM_Tree", "/Game/ThirdParty/Foliage/SM_DeciduousTree"),
        ("tree", "oak", "maple", "elm"), min_extent=200.0, max_extent=3000.0,
    )
    yaw = rng.uniform(0, 360)
    hgt = rng.uniform(850, 1300)
    if mesh and mesh.startswith(KITE):
        # Open World Demo oaks: alternate the two hill-tree variants, full-size.
        mesh = rng.choice(kite_all("tree"))
        return fit_native(label, mesh, (x, y, z - 20), rng.uniform(1100, 1600), (0, yaw, 0))
    if mesh:
        return fit_native(label, mesh, (x, y, z), hgt, (0, yaw, 0))
    trunk_h = hgt * 0.45
    block(label + "_Trunk", (x, y, z + trunk_h * 0.5), (55, 55, trunk_h), trunk_mat,
          mesh="/Engine/BasicShapes/Cylinder")
    # Three overlapping crowns read as a deciduous mass rather than a lollipop.
    crown = hgt - trunk_h
    for k in range(3):
        a = math.radians(yaw + k * 120)
        r = rng.uniform(80, 160)
        s = rng.uniform(0.7, 1.0) * crown
        block("%s_Canopy_%d" % (label, k),
              (x + math.cos(a) * r, y + math.sin(a) * r, z + trunk_h + s * 0.45 + k * 40),
              (s * 1.15, s * 1.05, s * 0.85), canopy_mat, mesh="/Engine/BasicShapes/Sphere")
    return None


def place_split_rail(label, p0, p1, wood_mat):
    """Zig-zag-free split-rail fence: posts every ~3.2 m, two rails, each on the ground."""
    mesh = resolve_asset(
        "fence",
        ("/Game/ThirdParty/Fences/SM_SplitRail", "/Game/ThirdParty/Fences/SM_WormFence"),
        ("fence", "rail"), min_extent=80.0, max_extent=800.0,
    )
    length = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    yaw = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
    if mesh:
        ext = unreal.EditorAssetLibrary.load_asset(mesh).get_bounds().box_extent
        piece = max(2.0 * max(ext.x, ext.y), 100.0)
        n = max(1, int(round(length / piece)))
        for i in range(n):
            t = (i + 0.5) / n
            x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
            fit_native("%s_%d" % (label, i), mesh, (x, y, ground_z(x, y)), 2.0 * ext.z,
                       (0, yaw + (90 if ext.y > ext.x else 0), 0))
        return None
    steps = max(2, int(length / 320))
    for i in range(steps + 1):
        t = i / float(steps)
        x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
        block("%s_Post_%d" % (label, i), (x, y, ground_z(x, y) + 60), (16, 16, 120), wood_mat)
        if i < steps:
            q = (p0[0] + (p1[0] - p0[0]) * (i + 1) / steps, p0[1] + (p1[1] - p0[1]) * (i + 1) / steps)
            for k, hz in enumerate((55, 95)):
                strip_on_ground("%s_Rail_%d_%d" % (label, i, k), (x, y), q, 9, 9, wood_mat, lift=hz)
    return None


def place_stone_wall(label, p0, p1, stone_mat, rng, group="wall_"):
    """Dry-stone wall along p0->p1: short irregular courses following the ground."""
    mesh = resolve_asset(
        "stone_wall",
        ("/Game/ThirdParty/Fences/SM_StoneWall", "/Game/ThirdParty/Fences/SM_DryStoneWall"),
        ("stonewall", "stone_wall", "drystone", "fieldstone", "boulder"),
        min_extent=50.0, max_extent=800.0,
    )
    length = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    yaw = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
    n = max(1, int(length / 260))
    rocks = [] if mesh else kite_all("wall_rock")
    if rocks:
        _CHOSEN["stone_wall"] = "KiteDemo rocks: " + ", ".join(r.rsplit("/", 1)[-1] for r in rocks)
    if rocks:
        # Two courses of fieldstone, randomly turned, plus capstones. Collision comes from
        # an invisible box of the old wall's size, so cover and pathing match the graybox.
        ux, uy = (p1[0] - p0[0]) / length, (p1[1] - p0[1]) / length
        # Every stone is seated on the ground or on the stone below it, using its real scaled
        # height (KiteDemo river rocks are flat, so a fixed lift left them floating), sunk a
        # little and only slightly tilted. Courses stack until the wall reaches the 1.1 m cover
        # height of its collision box; the cap course keeps a few gaps.
        steps = max(1, int(length / 48))
        for i in range(steps):
            t = (i + 0.5) / steps
            bx, by = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
            top = 0.0  # height of the stack above the ground at this step
            for course in range(5):
                if top >= 105.0:
                    break
                if top >= 75.0 and rng.random() < 0.2:
                    break  # gaps in the cap course
                r = rng.choice(rocks)
                big, e = _mesh_extent(r)
                size = rng.uniform(80, 105) if course == 0 else rng.uniform(60, 85)
                s = size / (2.0 * big)
                half_h = e.z * s
                off = rng.uniform(-8, 8)
                along = rng.uniform(-10, 10)
                x = bx - uy * off + ux * along
                y = by + ux * off + uy * along
                sink = 14.0 if course == 0 else 9.0
                # Seat on the downhill lip. A long Battle Road wall drops about 60 cm,
                # only a couple of centimetres across one stone; the sink covers that,
                # and using the lower sample keeps a flat rock from showing daylight.
                span = max(e.x, e.y) * s
                lip = min(
                    ground_z(x, y),
                    ground_z(x - ux * span, y - uy * span),
                    ground_z(x + ux * span, y + uy * span),
                )
                z = lip + top + half_h - sink
                scatter(group + r.rsplit("/", 1)[-1], r, (x, y, z),
                        (rng.uniform(-4, 4), rng.uniform(0, 360), rng.uniform(-4, 4)),
                        s, collide=False, cull=(0, 0), gi=False)
                top += 2.0 * half_h - sink
        for i in range(n):
            t = (i + 0.5) / n
            x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
            blk = block("%s_Block_%d" % (label, i), (x, y, ground_z(x, y) + 50),
                        (length / n + 25, 80, 120), stone_mat, rot_yaw=yaw)
            blk.set_actor_hidden_in_game(True)
            blk.static_mesh_component.set_visibility(False)
            tag(blk, "PA250Cover")  # bots use tagged walls as firing positions
        return
    for i in range(n):
        t = (i + 0.5) / n
        x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
        hgt = rng.uniform(95, 135)
        if mesh:
            a = fit_native("%s_%d" % (label, i), mesh, (x, y, ground_z(x, y) - 10), hgt,
                           (0, yaw + rng.uniform(-6, 6), 0))
        else:
            a = block("%s_%d" % (label, i), (x, y, ground_z(x, y) + hgt * 0.5 - 10),
                      (length / n + 25, rng.uniform(60, 80), hgt), stone_mat,
                      rot_yaw=yaw + rng.uniform(-3, 3))
        tag(a, "PA250Cover")


# Battle Road (GAME INVENTION layout, after the 19 April 1775 running fight): field walls flank the
# Boston road where the British column approaches, and militia companies muster in from the north,
# south and west. Walls are 1.1 m collision (chest covered, shoulder clear) tagged PA250Cover.
BATTLE_ROAD_WALLS = (
    ("BattleRoad_RoadN", (6500, -3250), (2700, -3600)),   # parallel to the road, ~17 m north of it
    ("BattleRoad_RoadS", (6400, 350), (2900, 700)),       # parallel to the road, ~17 m south
    ("BattleRoad_FieldN1", (5200, -6200), (5000, -3900)),  # cross walls facing the approach
    ("BattleRoad_FieldS1", (4700, 1100), (4900, 3500)),
    ("BattleRoad_FieldN2", (1800, -6400), (2000, -4000)),
    ("BattleRoad_FieldS2", (3100, 1900), (3300, 4400)),
)
# Patriot cover (GAME INVENTION): field walls on the Patriot side of the Green, 38-48 m from flag B
# and turned to face it, so militia can fire on regulars holding the Green from behind stone.
# Spots were chosen clear of roads, building pads, posts, fences, trees and the spawn cluster;
# the gap between W1 and W2 lets men move through.
PATRIOT_WALLS = (
    ("PatriotCover_W1", (-4596, 535), (-4874, -1041)),
    ("PatriotCover_W2", (-4814, -1719), (-4267, -3222)),
    ("PatriotCover_SW", (-1707, 2691), (-3093, 1891)),
)
# More trees on the Patriot side, 30-64 m from the Green, kept out of the walls' lines of fire.
PATRIOT_TREES = (
    (-6500, -3250), (-6000, -1250), (-5250, -5000), (-4500, 4000), (-3750, -3250),
    (-3500, -1250), (-3500, 2500), (-3000, 750), (-2250, 4250), (-1750, -5000),
)
MUSTER_POINTS = (  # label, xy; tagged "continental_muster"
    ("Muster_Continental_North", (300, -6800)),
    ("Muster_Continental_South", (1500, 5400)),
    ("Muster_Continental_West", (-6800, -2600)),
)
# Shared with the full Lexington build so the Patriot-cover check below stays honest
# if a fence or a meadow tree moves.
COMMON_FENCES = (
    ("Fence_NE", (2100, -2900), (2400, 500)),
    ("Fence_SE", (2000, 900), (-200, 3900)),
    ("Fence_SW", (-2400, 3300), (-3000, 2600)),
    ("Fence_NW", (-4300, -1500), (-3200, -3500)),
)
MEADOW_TREES = (
    (-5600, -3000), (-6900, 3300), (-1500, 4800), (3200, -3800), (3900, 1500),
    (-7400, -1200), (5500, -1800), (-1700, -6300), (1000, 4300), (-6200, 4200),
    (4600, -4600), (-800, -6100), (-8500, 1500), (6500, 2800), (-2600, 5600),
    (2300, 5600), (-9800, -3200), (7800, -4200), (-4600, 4900), (-12000, 500),
)


def audit_patriot_cover():
    """Re-check the hand-picked Patriot walls and trees against roads, pads, posts,
    fences and meadow trees. Raises if a later edit of those lists buries a wall."""
    problems = []

    def consider(name, x, y):
        if not keep_clear(x, y, 40):
            problems.append("%s sits on a road, pad or post (%.0f, %.0f)" % (name, x, y))
        for _label, a, b in COMMON_FENCES:
            if _seg_dist(x, y, a, b) < 150:
                problems.append("%s is within 1.5 m of %s" % (name, _label))
                break
        for tx, ty in MEADOW_TREES:
            if math.hypot(x - tx, y - ty) < 300:
                problems.append("%s is within 3 m of a meadow tree" % name)
                break

    for label, p0, p1 in PATRIOT_WALLS:
        for i in range(9):
            t = i / 8.0
            consider(label, p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t)
    for i, (x, y) in enumerate(PATRIOT_TREES):
        consider("PatriotCover_Tree_%d" % i, x, y)
        for label, p0, p1 in PATRIOT_WALLS:
            if _seg_dist(x, y, p0, p1) < 180:
                problems.append("PatriotCover_Tree_%d stands in %s" % (i, label))
    if problems:
        raise RuntimeError("Patriot cover is not clear:\n  " + "\n  ".join(problems))


def place_battle_road(stone, rng):
    for label, p0, p1 in BATTLE_ROAD_WALLS:
        place_stone_wall(label, p0, p1, stone, rng, group="brwall_")
    for label, (x, y) in MUSTER_POINTS:
        tag(spawn(unreal.PlayerStart, (x, y, ground_z(x, y) + 120), (0, 0, 0), label), "continental_muster")


def place_patriot_cover(stone, wood, canopy, rng):
    for label, p0, p1 in PATRIOT_WALLS:
        place_stone_wall(label, p0, p1, stone, rng, group="ptwall_")
    for i, xy in enumerate(PATRIOT_TREES):
        place_tree("PatriotCover_Tree_%d" % i, xy, wood, canopy, rng)


def build_lexington_battle_road():
    """Add the Battle Road walls, Patriot cover and muster points to the existing L_Lexington
    (idempotent; leaves everything else in the level untouched) and tag the existing stone walls
    as cover. Their stones live in their own scatter groups (brwall_ / ptwall_) so a rerun
    replaces exactly them."""
    import random

    audit_patriot_cover()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAPS + "/L_Lexington")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    removed = tagged = 0
    for a in actors:
        label = a.get_actor_label()
        if label.startswith(("BattleRoad_", "Muster_", "PatriotCover_", "Scatter_brwall_", "Scatter_ptwall_")):
            a.destroy_actor()
            removed += 1
        elif label.startswith("Wall_") and isinstance(a, unreal.StaticMeshActor):
            tag(a, "PA250Cover")
            tagged += 1
    stone = make_material("MI_PA250_Stone", (0.42, 0.41, 0.38))
    wood = make_material("MI_PA250_Wood", (0.36, 0.24, 0.14))
    canopy = make_material("MI_PA250_Canopy", (0.16, 0.26, 0.09))
    rng = random.Random(17750419)
    place_battle_road(stone, rng)
    place_patriot_cover(stone, wood, canopy, rng)
    flush_scatter()  # without this the walls were invisible collision boxes
    save(les)
    LOG("PA250: Battle Road on L_Lexington (removed %d old, tagged %d existing walls)" % (removed, tagged))


def dress_meadow(rng):
    """KiteDemo field boulders, grass clumps and flowers (no-op without the pack).
    Counts and cull distances are capped for the M3 frame budget."""
    boulders = kite_all("field_boulder")
    for i in range(18):
        if not boulders:
            break
        for _try in range(20):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(2500, 11000)
            x, y = GREEN_XY[0] + math.cos(a) * r, GREEN_XY[1] + math.sin(a) * r * 0.8
            if keep_clear(x, y, 300):
                break
        m = rng.choice(boulders)
        big, _e = _mesh_extent(m)
        size = rng.uniform(90, 220)
        # Boulders are low cover: they block like any wall.
        scatter("boulder_" + m.rsplit("/", 1)[-1], m, (x, y, ground_z(x, y) - size * 0.15),
                (rng.uniform(-8, 8), rng.uniform(0, 360), rng.uniform(-8, 8)),
                size / (2.0 * big), collide=True)
    stumps = kite_all("stump")
    for i in range(6):
        if not stumps:
            break
        x, y = rng.uniform(-9000, 8000), rng.uniform(-7000, 6000)
        if keep_clear(x, y, 200):
            big, _e = _mesh_extent(stumps[0])
            scatter("stump", stumps[0], (x, y, ground_z(x, y) - 5), (0, rng.uniform(0, 360), 0),
                    rng.uniform(70, 110) / (2.0 * big))
    grass = kite_all("grass_clump")
    flowers = kite_all("flowers")
    # Patchy meadow: clustered clumps read as a field without carpeting the frame.
    for patch in range(170 if grass else 0):
        cx = GREEN_XY[0] + rng.uniform(-9000, 9000)
        cy = GREEN_XY[1] + rng.uniform(-7500, 7500)
        n = rng.randint(8, 18)
        for _ in range(n):
            x, y = cx + rng.gauss(0, 260), cy + rng.gauss(0, 260)
            if not keep_clear(x, y, 60):
                continue
            big, _e = _mesh_extent(grass[0])
            scatter("grass", grass[0], (x, y, ground_z(x, y) - 3), (0, rng.uniform(0, 360), 0),
                    rng.uniform(70, 120) / (2.0 * big), cull=(2200, 3500), shadows=False, gi=False)
        if flowers and rng.random() < 0.45:
            f = rng.choice(flowers)
            for _ in range(rng.randint(3, 7)):
                x, y = cx + rng.gauss(0, 200), cy + rng.gauss(0, 200)
                if keep_clear(x, y, 60):
                    big, _e = _mesh_extent(f)
                    scatter("flowers_" + f.rsplit("/", 1)[-1], f, (x, y, ground_z(x, y) - 2),
                            (0, rng.uniform(0, 360), 0), rng.uniform(50, 80) / (2.0 * big),
                            cull=(1800, 2800), shadows=False, gi=False)


def shot_camera(tag_name, loc, rot, fov=60.0):
    cam = spawn(unreal.CameraActor, loc, rot, tag_name)
    tag(cam, tag_name)
    comp = cam.get_component_by_class(unreal.CameraComponent)
    if comp:
        comp.set_editor_property("field_of_view", fov)
    return cam


# ---------------------------------------------------------------- levels


def build_campaign():
    src = os.path.join(PROJECT, "Content/PA250/Source/campaign_map.png")
    import_texture(src, "/Game/PA250/Textures", "T_CampaignMap")
    les = new_level(MAPS + "/L_Campaign")
    set_game_mode("PA250CampaignGameMode")
    save(les)
    LOG("PA250: built L_Campaign")


def build_lexington():
    """Dawn Lexington Green, 19 April 1775.

    Units are cm; +X east (British road from Boston), -X west (Concord road).
    The sun rises in the east-north-east behind the British column.
    """
    import random

    global STONE_MAT, WOOD_DARK_MAT, BRICK_MAT, GLASS_MAT
    make_tuning()
    rng = random.Random(1775)  # deterministic dressing
    meadow = make_meadow_material()
    stone = STONE_MAT = make_material("MI_PA250_Stone", (0.42, 0.41, 0.38))
    wood = make_material("MI_PA250_Wood", (0.36, 0.24, 0.14))
    WOOD_DARK_MAT = make_material("MI_PA250_WoodDark", (0.16, 0.10, 0.06))
    white = make_material("MI_PA250_White", (0.80, 0.77, 0.68))
    ochre = make_material("MI_PA250_Ochre", (0.55, 0.42, 0.22))
    roof = make_material("MI_PA250_Roof", (0.20, 0.16, 0.13))
    canopy = make_material("MI_PA250_Canopy", (0.16, 0.26, 0.09))
    BRICK_MAT = make_material("MI_PA250_Brick", (0.40, 0.17, 0.11))
    GLASS_MAT = make_material("MI_PA250_Glass", (0.04, 0.045, 0.05))
    # Legacy MIs kept for any external refs.
    for name, rgb in (
        ("MI_PA250_Grass", (0.20, 0.36, 0.14)),
        ("MI_PA250_Ground", (0.28, 0.24, 0.15)),
        ("MI_PA250_Dirt", (0.38, 0.28, 0.17)),
        ("MI_PA250_DirtRoad", (0.32, 0.22, 0.12)),
    ):
        make_material(name, rgb)
    terrain = import_terrain_mesh()

    les = new_level(MAPS + "/L_Lexington")
    set_game_mode("PA250BattleGameMode")

    # Rolling ground; worn roads come from the meadow material masks.
    g = spawn(unreal.StaticMeshActor, (0, 0, 0), (0, 0, 0), "Ground")
    gc = g.static_mesh_component
    gc.set_mobility(unreal.ComponentMobility.STATIC)
    gc.set_static_mesh(terrain)
    gc.set_material(0, meadow)

    # Meetinghouse (white, with belfry on the ridge) and Buckman Tavern (ochre clapboard).
    (mx, my), msize = MEETINGHOUSE
    building_shell(
        "Meetinghouse", (mx, my), msize, white, roof, white, GLASS_MAT, "meetinghouse",
        ("/Game/ThirdParty/Buildings/SM_Meetinghouse", "/Game/ThirdParty/Buildings/SM_Church"),
        ("meetinghouse", "church", "chapel"),
    )
    if not _ASSET_CACHE.get("meetinghouse"):
        belfry("Belfry", (mx - msize[0] * 0.3, my), ground_z(mx, my) + msize[2] + msize[1] * 0.5, white, roof)
    (tx, ty), tsize = TAVERN
    building_shell(
        "BuckmanTavern", (tx, ty), tsize, ochre, roof, white, GLASS_MAT, "tavern",
        ("/Game/ThirdParty/Buildings/SM_Tavern", "/Game/ThirdParty/Buildings/SM_ColonialHouse"),
        ("tavern", "colonial", "house", "cottage", "farmhouse"), chimneys=2,
    )

    # Dry-stone walls flank the Concord road west of the Common.
    def concord_y(x):
        return -4000.0 + (x + 3000.0) * (1200.0 / 11000.0)

    for side, off in (("N", -520), ("S", 520)):
        p0 = (-4600, concord_y(-4600) + off)
        p1 = (-13500, concord_y(-13500) + off)
        place_stone_wall("Wall_" + side, p0, p1, stone, rng)
    # Field walls along the Boston road east of the Common.
    place_stone_wall("Wall_E", (3200, -2000), (9000, -1000), stone, rng)
    place_battle_road(stone, rng)
    place_patriot_cover(stone, wood, canopy, rng)

    # Split-rail fences around the Common, clear of the posts and buildings.
    audit_patriot_cover()
    for label, p0, p1 in COMMON_FENCES:
        place_split_rail(label, p0, p1, wood)
    for i, xy in enumerate(MEADOW_TREES):
        place_tree("Tree_%d" % i, xy, wood, canopy, rng)

    for i in range(14):
        x, y = -6200 + (i % 4) * 280, -200 + (i // 4) * 280
        tag(spawn(unreal.PlayerStart, (x, y, ground_z(x, y) + 120), (0, 20, 0),
                  "Start_Continental_%d" % i), "continental")
    for i in range(14):
        x, y = 9500 + (i // 4) * 350, -1500 + (i % 4) * 350
        tag(spawn(unreal.PlayerStart, (x, y, ground_z(x, y) + 120), (0, 180, 0),
                  "Start_British_%d" % i), "british")

    def post(label, post_id, xy, initial_owner=None):
        a = spawn(unreal.PA250ControlPoint, (xy[0], xy[1], ground_z(*xy) + 140), (0, 0, 0), label)
        a.set_editor_property("post_id", unreal.Name(post_id))
        if initial_owner:
            a.set_editor_property("initial_owner", unreal.Name(initial_owner))
        return a

    post("Objective_Green", "green", GREEN_XY)
    post("Objective_Meetinghouse", "meetinghouse", (-2800, -4800), "continental")
    post("Objective_Buckman", "buckman", (-3600, 1400), "continental")

    spawn(unreal.NavMeshBoundsVolume, (0, 0, 0), (0, 0, 0), "NavBounds", scale=(160, 130, 12))

    # --- Dawn light: low warm sun in the ENE (light travels WSW), ~15 deg elevation.
    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), (-15, 165, 0), "Sun")
    sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    set_props(sun_comp, (
        ("mobility", unreal.ComponentMobility.MOVABLE),
        ("intensity", DAWN_SUN_LUX),
        ("light_color", unreal.Color(255, 165, 95, 255)),
        ("atmosphere_sun_light", True),
        ("cast_shadows", True),
        ("volumetric_scattering_intensity", 1.5),
    ), "Sun")

    spawn(unreal.SkyAtmosphere, (0, 0, 0), (0, 0, 0), "SkyAtmosphere")
    spawn(unreal.VolumetricCloud, (0, 0, 0), (0, 0, 0), "VolumetricClouds")

    sky = spawn(unreal.SkyLight, (0, 0, 1500), (0, 0, 0), "SkyLight")
    set_props(sky.get_component_by_class(unreal.SkyLightComponent), (
        ("mobility", unreal.ComponentMobility.MOVABLE),
        ("real_time_capture", True),
        ("intensity", 1.0),
    ), "SkyLight")

    # Thin ground haze; volumetric so the low sun shafts through it like powder smoke.
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, 0), (0, 0, 0), "Fog")
    set_props(fog.get_component_by_class(unreal.ExponentialHeightFogComponent), (
        ("fog_density", 0.014),
        ("fog_height_falloff", 0.3),
        ("start_distance", 600.0),
        ("fog_max_opacity", 0.85),
        ("fog_inscattering_luminance", unreal.LinearColor(0.55, 0.45, 0.35, 1.0)),
        ("directional_inscattering_luminance", unreal.LinearColor(0.55, 0.36, 0.2, 1.0)),
        ("enable_volumetric_fog", True),
        ("volumetric_fog_scattering_distribution", 0.3),
        ("volumetric_fog_extinction_scale", 0.4),
        ("volumetric_fog_albedo", unreal.Color(230, 220, 200, 255)),
        ("volumetric_fog_distance", 8000.0),
    ), "Fog")

    pp = spawn(unreal.PostProcessVolume, (0, 0, 200), (0, 0, 0), "PP_Dawn")
    pp.set_editor_property("unbound", True)
    pp.set_editor_property("priority", 1.0)
    pp.set_editor_property("blend_weight", 1.0)
    st = pp.get_editor_property("settings")
    for prop, val in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("auto_exposure_apply_physical_camera_exposure", False),
        ("auto_exposure_bias", DAWN_EXPOSURE_EV),
        ("bloom_intensity", 0.4),
        ("vignette_intensity", 0.45),
        # Period-painting grade: warm, a little desaturated, soft contrast.
        ("white_temp", 7200.0),
        ("color_saturation", unreal.Vector4(0.78, 0.78, 0.72, 1.0)),
        ("color_contrast", unreal.Vector4(0.95, 0.95, 0.95, 1.0)),
        ("color_gain", unreal.Vector4(1.16, 1.0, 0.76, 1.0)),
        ("color_saturation_shadows", unreal.Vector4(0.8, 0.8, 0.85, 1.0)),
        ("film_toe", 0.6),
    ):
        try:
            st.set_editor_property("override_" + prop, True)
            st.set_editor_property(prop, val)
        except Exception as exc:  # noqa: BLE001
            LOG("PA250: PP_Dawn.%s skipped (%s)" % (prop, exc))
    pp.set_editor_property("settings", st)

    dress_meadow(rng)
    flush_scatter()

    # Fixed review cameras (also used by -PA250Preview=<label> captures).
    shot_camera("Shot_Spawn", (-6900, 900, ground_z(-6900, 900) + 260), (-8, -15, 0), 60.0)
    shot_camera("Shot_Green", (1600, 1400, ground_z(1600, 1400) + 220), (-6, -135, 0), 65.0)
    shot_camera("Shot_Wide", (3600, -7600, ground_z(3600, -7600) + 160), (-3, 135, 0), 75.0)

    write_asset_choices()
    save(les)
    LOG("PA250: built L_Lexington")


def make_portrait_assets():
    """Still portrait + MediaPlayer/MediaTexture, and unlit materials."""
    src = os.path.abspath(
        os.path.join(
            PROJECT,
            "../../services/negotiation/assets/avatars/"
            "british_general_frame.jpg",
        )
    )
    still = import_texture(src, "/Game/PA250/Textures", "T_EnvoyPortrait")
    d = "/Game/PA250/Media"
    if unreal.EditorAssetLibrary.does_asset_exist(d + "/MP_Envoy"):
        mp = unreal.EditorAssetLibrary.load_asset(d + "/MP_Envoy")
    else:
        mp = tools().create_asset(
            "MP_Envoy",
            d,
            unreal.MediaPlayer,
            unreal.MediaPlayerFactoryNew(),
        )
    if unreal.EditorAssetLibrary.does_asset_exist(d + "/MT_Envoy"):
        mt = unreal.EditorAssetLibrary.load_asset(d + "/MT_Envoy")
    else:
        mt = tools().create_asset(
            "MT_Envoy",
            d,
            unreal.MediaTexture,
            unreal.MediaTextureFactoryNew(),
        )
    mt.set_editor_property("media_player", mp)
    mt.set_editor_property("auto_clear", True)
    unreal.EditorAssetLibrary.save_loaded_asset(mp)
    unreal.EditorAssetLibrary.save_loaded_asset(mt)

    def painting_material(name, texture, atlas_grid=0):
        """Oil portrait over the mantel: lit like the room, cropped to 3:4, linen + varnish + darker edges.

        The still and the talking-portrait video (MediaTexture) share this graph, so the mouth moves
        inside the painting. A little emissive keeps the face readable in a candlelit room.
        """
        path = "/Game/PA250/Materials/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        mat = tools().create_asset(name, "/Game/PA250/Materials", unreal.Material, unreal.MaterialFactoryNew())
        mat.set_editor_property("two_sided", True)
        mel = unreal.MaterialEditingLibrary
        X = unreal

        def node(cls, x, y, **props):
            n = mel.create_material_expression(mat, cls, x, y)
            for k, v in props.items():
                n.set_editor_property(k, v)
            return n

        def link(a, a_out, b, b_in):
            mel.connect_material_expressions(a, a_out, b, b_in)

        uv = node(X.MaterialExpressionTextureCoordinate, -1600, 0)
        if atlas_grid:
            # Lip-sync flipbook: cell `Frame` of an atlas_grid x atlas_grid atlas (cells are already 3:4).
            frame = node(X.MaterialExpressionScalarParameter, -2000, -300, parameter_name="Frame", default_value=0.0)
            col = node(X.MaterialExpressionFmod, -1800, -300)
            grid = node(X.MaterialExpressionConstant, -2000, -150, r=float(atlas_grid))
            link(frame, "", col, "A"); link(grid, "", col, "B")
            rowf = node(X.MaterialExpressionDivide, -1800, -150)
            link(frame, "", rowf, "A"); link(grid, "", rowf, "B")
            row = node(X.MaterialExpressionFloor, -1650, -150)
            link(rowf, "", row, "")
            cell = node(X.MaterialExpressionAppendVector, -1500, -250)
            link(col, "", cell, "A"); link(row, "", cell, "B")
            in_cell = node(X.MaterialExpressionAdd, -1350, -100)
            link(cell, "", in_cell, "A"); link(uv, "", in_cell, "B")
            crop_uv = node(X.MaterialExpressionDivide, -1200, -100, const_b=float(atlas_grid))
            link(in_cell, "", crop_uv, "A")
        crop = node(X.MaterialExpressionScalarParameter, -1600, 150, parameter_name="CropWidth", default_value=PORTRAIT_CROP)
        one = node(X.MaterialExpressionConstant, -1600, 250, r=1.0)
        scale = node(X.MaterialExpressionAppendVector, -1400, 150)
        link(crop, "", scale, "A"); link(one, "", scale, "B")
        half_rest = node(X.MaterialExpressionOneMinus, -1400, 300)
        link(crop, "", half_rest, "")
        half = node(X.MaterialExpressionMultiply, -1250, 300, const_b=0.5)
        link(half_rest, "", half, "A")
        zero = node(X.MaterialExpressionConstant, -1250, 400, r=0.0)
        offset = node(X.MaterialExpressionAppendVector, -1100, 300)
        link(half, "", offset, "A"); link(zero, "", offset, "B")
        scaled = node(X.MaterialExpressionMultiply, -1100, 100)
        link(uv, "", scaled, "A"); link(scale, "", scaled, "B")
        if not atlas_grid:
            crop_uv = node(X.MaterialExpressionAdd, -950, 100)
            link(scaled, "", crop_uv, "A"); link(offset, "", crop_uv, "B")

        tex = node(X.MaterialExpressionTextureSampleParameter2D, -750, 0, parameter_name="Portrait", texture=texture)
        link(crop_uv, "", tex, "UVs")

        canvas_uv = node(X.MaterialExpressionMultiply, -950, 450, const_b=3.0)
        link(uv, "", canvas_uv, "A")
        canvas = node(X.MaterialExpressionTextureSampleParameter2D, -750, 400, parameter_name="Canvas",
                      texture=import_texture_mipped("canvas"))
        link(canvas_uv, "", canvas, "UVs")

        centre = node(X.MaterialExpressionConstant2Vector, -1100, 650, r=0.5, g=0.5)
        dist = node(X.MaterialExpressionDistance, -950, 650)
        link(uv, "", dist, "A"); link(centre, "", dist, "B")
        d0 = node(X.MaterialExpressionSubtract, -800, 650, const_b=0.3)
        link(dist, "", d0, "A")
        d1 = node(X.MaterialExpressionMultiply, -650, 650, const_b=1.6)
        link(d0, "", d1, "A")
        d2 = node(X.MaterialExpressionSaturate, -500, 650)
        link(d1, "", d2, "")
        d3 = node(X.MaterialExpressionMultiply, -350, 650, const_b=0.65)
        link(d2, "", d3, "A")
        vignette = node(X.MaterialExpressionOneMinus, -200, 650)
        link(d3, "", vignette, "")

        varnish = node(X.MaterialExpressionVectorParameter, -750, 250, parameter_name="Varnish",
                       default_value=X.LinearColor(1.0, 0.86, 0.66, 1.0))
        c1 = node(X.MaterialExpressionMultiply, -450, 0)
        link(tex, "RGB", c1, "A"); link(canvas, "RGB", c1, "B")
        c2 = node(X.MaterialExpressionMultiply, -300, 0)
        link(c1, "", c2, "A"); link(varnish, "", c2, "B")
        color = node(X.MaterialExpressionMultiply, -150, 0)
        link(c2, "", color, "A"); link(vignette, "", color, "B")
        mel.connect_material_property(color, "", X.MaterialProperty.MP_BASE_COLOR)

        glow = node(X.MaterialExpressionScalarParameter, -300, 250, parameter_name="Brightness", default_value=PORTRAIT_BRIGHTNESS)
        emissive = node(X.MaterialExpressionMultiply, -150, 250)
        link(color, "", emissive, "A"); link(glow, "", emissive, "B")
        mel.connect_material_property(emissive, "", X.MaterialProperty.MP_EMISSIVE_COLOR)
        rough = node(X.MaterialExpressionScalarParameter, -150, 400, parameter_name="Roughness", default_value=0.42)
        mel.connect_material_property(rough, "", X.MaterialProperty.MP_ROUGHNESS)
        mel.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        return mat

    atlas = import_lipsync_atlas()
    # Idle and live speech share one material: cell 0 at rest, cells 1..15 driven by the voice (PollVoice).
    still_mat = painting_material("M_PortraitStill", atlas, atlas_grid=4) if atlas else painting_material("M_PortraitStill", still)
    video_mat = painting_material("M_PortraitVideo", mt)
    return still_mat, video_mat


PARLOUR_DIR = "/Game/PA250/Materials/Parlour"
PARLOUR_TEX_SRC = os.path.abspath(os.path.join(PROJECT, "SourceArt", "parlour"))
PORTRAIT_BRIGHTNESS = 0.35  # emissive share of the lit painting (face stays readable by candlelight)
PORTRAIT_CROP = 0.42  # width share of the 1.77-1.8:1 still/video shown: a 3:4 oil portrait
# Manual exposure with physical-camera exposure off: the bias is a compensation (+1 = 2x brighter), not EV100.
PARLOUR_EXPOSURE_COMP = 0.0


def import_lipsync_atlas():
    """tools/art/build_envoy_lipsync_atlas.py output: 4x4 cells, 1448x1920 (not power of two: no mips, never stream)."""
    src = os.path.join(PARLOUR_TEX_SRC, "envoy_lipsync_atlas.png")
    if not os.path.exists(src):
        LOG("PA250: lip-sync atlas missing (run tools/art/build_envoy_lipsync_atlas.py); portrait stays still")
        return None
    task = unreal.AssetImportTask()
    for k, v in (("filename", src), ("destination_path", PARLOUR_DIR + "/Textures"), ("destination_name", "T_EnvoyLipSyncAtlas"),
                 ("replace_existing", True), ("automated", True), ("save", True)):
        task.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(PARLOUR_DIR + "/Textures/T_EnvoyLipSyncAtlas")
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    LOG("PA250: lip-sync atlas %dx%d" % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    return tex


def import_texture_mipped(name):
    """Tiling surface texture: sRGB, mips from the texture group (sources are power of two), trilinear."""
    src = os.path.join(PARLOUR_TEX_SRC, name + ".png")
    if not os.path.exists(src):
        LOG("PA250: parlour texture missing (run tools/art/gen_parlour_textures.py): " + src)
        return None
    task = unreal.AssetImportTask()
    for k, v in (("filename", src), ("destination_path", PARLOUR_DIR + "/Textures"), ("destination_name", "T_" + name),
                 ("replace_existing", True), ("automated", True), ("save", True)):
        task.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(PARLOUR_DIR + "/Textures/T_" + name)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_TRILINEAR)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    LOG("PA250: parlour texture T_%s %dx%d mips=%s" % (name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                                                      tex.get_editor_property("mip_gen_settings")))
    return tex


def _fresh_material(name):
    path = PARLOUR_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
        return mat
    return tools().create_asset(name, PARLOUR_DIR, unreal.Material, unreal.MaterialFactoryNew())


def _param(mat, cls, name, x, y, value=None):
    mel = unreal.MaterialEditingLibrary
    n = mel.create_material_expression(mat, cls, x, y)
    n.set_editor_property("parameter_name", name)
    if value is not None:
        n.set_editor_property("default_value", value)
    return n


def parlour_masters(default_tex):
    """M_Parlour_World (tri-planar, world cm), M_Parlour_UV (mesh UVs), M_Parlour_Glow (unlit emissive)."""
    mel = unreal.MaterialEditingLibrary
    P = unreal.MaterialProperty

    world = _fresh_material("M_Parlour_World")
    tex = _param(world, unreal.MaterialExpressionTextureObjectParameter, "Tex", -900, 0)
    tex.set_editor_property("texture", default_tex)
    size = _param(world, unreal.MaterialExpressionVectorParameter, "TileCm", -900, 200, unreal.LinearColor(100, 100, 100, 0))
    wat = mel.create_material_expression(world, unreal.MaterialExpressionMaterialFunctionCall, -600, 0)
    wat.set_editor_property("material_function", unreal.EditorAssetLibrary.load_asset(
        "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture"))
    mel.connect_material_expressions(tex, "", wat, "TextureObject")
    mel.connect_material_expressions(size, "", wat, "TextureSize")
    tint = _param(world, unreal.MaterialExpressionVectorParameter, "Tint", -600, 250, unreal.LinearColor(1, 1, 1, 1))
    mul = mel.create_material_expression(world, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(wat, "XYZ Texture", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")
    mel.connect_material_property(mul, "", P.MP_BASE_COLOR)
    mel.connect_material_property(_param(world, unreal.MaterialExpressionScalarParameter, "Roughness", -300, 300, 0.7), "", P.MP_ROUGHNESS)
    mel.connect_material_property(_param(world, unreal.MaterialExpressionScalarParameter, "Metallic", -300, 400, 0.0), "", P.MP_METALLIC)
    mel.recompile_material(world)
    unreal.EditorAssetLibrary.save_loaded_asset(world)

    uv = _fresh_material("M_Parlour_UV")
    t2 = _param(uv, unreal.MaterialExpressionTextureSampleParameter2D, "Tex", -600, 0)
    t2.set_editor_property("texture", default_tex)
    tint2 = _param(uv, unreal.MaterialExpressionVectorParameter, "Tint", -600, 250, unreal.LinearColor(1, 1, 1, 1))
    mul2 = mel.create_material_expression(uv, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(t2, "RGB", mul2, "A")
    mel.connect_material_expressions(tint2, "", mul2, "B")
    mel.connect_material_property(mul2, "", P.MP_BASE_COLOR)
    mel.connect_material_property(_param(uv, unreal.MaterialExpressionScalarParameter, "Roughness", -300, 300, 0.9), "", P.MP_ROUGHNESS)
    mel.recompile_material(uv)
    unreal.EditorAssetLibrary.save_loaded_asset(uv)

    glow = _fresh_material("M_Parlour_Glow")
    glow.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    col = _param(glow, unreal.MaterialExpressionVectorParameter, "Color", -600, 0, unreal.LinearColor(1, 0.5, 0.1, 1))
    k = _param(glow, unreal.MaterialExpressionScalarParameter, "Strength", -600, 200, 5.0)
    mul3 = mel.create_material_expression(glow, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(col, "", mul3, "A")
    mel.connect_material_expressions(k, "", mul3, "B")
    mel.connect_material_property(mul3, "", P.MP_EMISSIVE_COLOR)
    mel.recompile_material(glow)
    unreal.EditorAssetLibrary.save_loaded_asset(glow)
    return world, uv, glow


def parlour_mi(name, parent, tex=None, tile_cm=None, tint=(1, 1, 1), rough=None, metal=None, color=None, strength=None):
    path = PARLOUR_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.EditorAssetLibrary.load_asset(path)
    else:
        mi = tools().create_asset(name, PARLOUR_DIR, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    mel = unreal.MaterialEditingLibrary
    if tex:
        mel.set_material_instance_texture_parameter_value(mi, "Tex", tex)
    if tile_cm:
        mel.set_material_instance_vector_parameter_value(mi, "TileCm", unreal.LinearColor(tile_cm, tile_cm, tile_cm, 0))
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1))
    if rough is not None:
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
    if metal is not None:
        mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
    if color is not None:
        mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1))
    if strength is not None:
        mel.set_material_instance_scalar_parameter_value(mi, "Strength", strength)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def parlour_light(label, loc, intensity, color, radius, shadows=True, tag_name=None, source_radius=2.0):
    pl = spawn(unreal.PointLight, loc, (0, 0, 0), label)
    c = pl.get_component_by_class(unreal.PointLightComponent)
    c.set_mobility(unreal.ComponentMobility.MOVABLE)  # Lumen; fire and candles flicker at runtime
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", intensity)
    c.set_editor_property("light_color", unreal.Color(r=color[0], g=color[1], b=color[2], a=255))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("source_radius", source_radius)
    c.set_editor_property("cast_shadows", shadows)
    if tag_name:
        tag(pl, tag_name)
    return pl


def build_diplomacy():
    """Georgian parlour, 700 x 600 x 320 cm, lit by fire and candles (GAME INVENTION interior).

    North wall: chimney breast with a marble fireplace, the envoy's portrait over the mantel, and a
    curtained sash window either side. Panelled cream wainscot, green damask above, plank floor, a
    Turkey carpet under a mahogany table with candles, two chairs. Manual exposure (no auto exposure).
    The camera sits at the player's chair, so the portrait is the centre of the shot.
    """
    still_mat, _ = make_portrait_assets()
    T = {n: import_texture_mipped(n) for n in
         ("floor_planks", "wallpaper", "paint_cream", "mahogany", "marble", "firebrick", "rug", "curtain")}
    world, uvm, glowm = parlour_masters(T["paint_cream"])
    m_floor = parlour_mi("MI_Parlour_Floor", world, T["floor_planks"], 200, rough=0.55)
    m_paper = parlour_mi("MI_Parlour_Wallpaper", world, T["wallpaper"], 120, rough=0.85)
    m_paint = parlour_mi("MI_Parlour_Paint", world, T["paint_cream"], 100, rough=0.6)
    m_ceiling = parlour_mi("MI_Parlour_Ceiling", world, T["paint_cream"], 200, tint=(0.62, 0.58, 0.52), rough=0.95)
    m_wood = parlour_mi("MI_Parlour_Mahogany", world, T["mahogany"], 80, rough=0.35)
    m_marble = parlour_mi("MI_Parlour_Marble", world, T["marble"], 90, tint=(0.78, 0.76, 0.72), rough=0.3)
    m_brick = parlour_mi("MI_Parlour_Firebrick", world, T["firebrick"], 60, rough=0.95)
    m_gilt = parlour_mi("MI_Parlour_Gilt", world, T["marble"], 40, tint=(0.78, 0.56, 0.24), rough=0.45, metal=1.0)
    m_brass = parlour_mi("MI_Parlour_Brass", world, T["marble"], 30, tint=(0.95, 0.75, 0.4), rough=0.3, metal=1.0)
    m_iron = parlour_mi("MI_Parlour_Iron", world, T["firebrick"], 30, tint=(0.12, 0.12, 0.12), rough=0.6, metal=1.0)
    m_rug = parlour_mi("MI_Parlour_Rug", uvm, T["rug"], rough=0.95)
    m_curtain = parlour_mi("MI_Parlour_Curtain", uvm, T["curtain"], rough=0.9)
    m_velvet = parlour_mi("MI_Parlour_Velvet", world, T["curtain"], 40, rough=0.9)
    m_paper_sheet = parlour_mi("MI_Parlour_Paper", world, T["paint_cream"], 50, tint=(1.1, 1.08, 1.0), rough=0.9)
    m_log = parlour_mi("MI_Parlour_Log", world, T["mahogany"], 30, tint=(0.35, 0.25, 0.2), rough=0.9)
    m_wax = parlour_mi("MI_Parlour_Wax", world, T["paint_cream"], 20, tint=(0.9, 0.85, 0.75), rough=0.4)
    m_flame = parlour_mi("MI_Parlour_Flame", glowm, color=(1.0, 0.42, 0.08), strength=3.5)
    m_ember = parlour_mi("MI_Parlour_Embers", glowm, color=(1.0, 0.2, 0.03), strength=2.5)
    m_night = parlour_mi("MI_Parlour_NightGlass", glowm, color=(0.03, 0.05, 0.12), strength=0.35)

    les = new_level(MAPS + "/L_DiplomacyRoom")
    set_game_mode("PA250DiplomacyGameMode")

    W, D, H = 700.0, 600.0, 320.0
    N = -D / 2  # north wall plane (y)

    # ---- shell
    block("Floor", (0, 0, -5), (W, D, 10), m_floor)
    block("Ceiling", (0, 0, H + 5), (W, D, 10), m_ceiling)
    block("Wall_North", (0, N - 5, H / 2), (W, 10, H), m_paper)
    block("Wall_South", (0, -N + 5, H / 2), (W, 10, H), m_paper)
    block("Wall_East", (W / 2 + 5, 0, H / 2), (10, D, H), m_paper)
    block("Wall_West", (-W / 2 - 5, 0, H / 2), (10, D, H), m_paper)

    # ---- wainscot, chair rail, skirting, crown on all four walls
    walls = (  # (label, centre xy of the inner face, along-axis length, normal axis, inward sign)
        ("N", (0, N), W, "y", 1), ("S", (0, -N), W, "y", -1),
        ("E", (W / 2, 0), D, "x", -1), ("W", (-W / 2, 0), D, "x", 1),
    )
    for lab, (cx, cy), length, axis, sgn in walls:
        def slab(name, depth, z0, z1, mat, inset=0.0, along=None, offset=0.0):
            ln = along if along is not None else length
            if axis == "y":
                block("%s_%s" % (name, lab), (cx + offset, cy + sgn * (inset + depth / 2), (z0 + z1) / 2), (ln, depth, z1 - z0), mat)
            else:
                block("%s_%s" % (name, lab), (cx + sgn * (inset + depth / 2), cy + offset, (z0 + z1) / 2), (depth, ln, z1 - z0), mat)
        slab("Dado", 2, 0, 92, m_paint)
        slab("Skirting", 3, 0, 18, m_paint, inset=2)
        slab("ChairRail", 4, 88, 96, m_paint, inset=2)
        slab("Crown", 10, H - 14, H, m_paint)
        slab("CrownLip", 4, H - 20, H - 14, m_paint, inset=10)
        n_panels = int(length // 80)
        pw = length / n_panels
        for i in range(n_panels):
            off = -length / 2 + pw * (i + 0.5)
            slab("Panel%d" % i, 1.5, 28, 80, m_paint, inset=2, along=pw - 18, offset=off)

    # ---- chimney breast and fireplace (north wall centre)
    BW, BD = 230.0, 40.0
    face = N + BD  # front face of the breast
    OW, OH = 92.0, 82.0  # fire opening
    block("Breast_L", (-(BW / 2 + OW / 2) / 2, N + BD / 2, H / 2), ((BW - OW) / 2, BD, H), m_paper)
    block("Breast_R", ((BW / 2 + OW / 2) / 2, N + BD / 2, H / 2), ((BW - OW) / 2, BD, H), m_paper)
    block("Breast_Top", (0, N + BD / 2, (OH + H) / 2), (OW, BD, H - OH), m_paper)
    block("Firebox_Back", (0, N + 6, OH / 2), (OW, 4, OH), m_brick)
    block("Firebox_L", (-OW / 2 + 2, N + BD / 2, OH / 2), (4, BD, OH), m_brick, rot=(0, 8, 0))
    block("Firebox_R", (OW / 2 - 2, N + BD / 2, OH / 2), (4, BD, OH), m_brick, rot=(0, -8, 0))
    block("Firebox_Floor", (0, N + BD / 2, 1), (OW, BD, 2), m_brick)
    # marble surround, mantel, hearth
    block("Surround_L", (-OW / 2 - 14, face + 2, 52), (28, 4, 104), m_marble)
    block("Surround_R", (OW / 2 + 14, face + 2, 52), (28, 4, 104), m_marble)
    block("Surround_Lintel", (0, face + 2, OH + 11), (OW + 56, 4, 22), m_marble)
    block("Mantel_Frieze", (0, face + 5, 112), (OW + 80, 6, 16), m_wood)
    block("Mantel_Shelf", (0, face + 9, 123), (OW + 100, 18, 5), m_wood)
    block("Mantel_Pilaster_L", (-OW / 2 - 36, face + 5, 56), (10, 6, 112), m_wood)
    block("Mantel_Pilaster_R", (OW / 2 + 36, face + 5, 56), (10, 6, 112), m_wood)
    block("Hearth", (0, face + 25, 1.5), (OW + 110, 50, 3), m_marble)
    # fire: andirons, logs, flames, embers
    for sx in (-1, 1):
        block("Andiron_%d" % sx, (sx * 28, N + 22, 10), (4, 30, 20), m_iron)
    for i, (lx, lz, yaw) in enumerate(((0, 9, 0), (-6, 17, 12), (8, 16, -10))):
        block("Log_%d" % i, (lx, N + 22, lz), (70, 11, 11), m_log, mesh="/Engine/BasicShapes/Cylinder", rot=(90, 0, yaw))
    block("Embers", (0, N + 22, 3), (70, 26, 3), m_ember)
    for i, (fx, fh, fw) in enumerate(((0, 40, 22), (-20, 28, 16), (19, 32, 17), (-8, 22, 12), (10, 24, 12))):
        block("Flame_%d" % i, (fx, N + 22, 18 + fh / 2), (fw, fw * 0.6, fh), m_flame, mesh="/Engine/BasicShapes/Cone")
    parlour_light("FireLight", (0, N + 24, 30), 45.0, (255, 120, 45), 850, tag_name="FireLight", source_radius=20)
    parlour_light("FireGlowLow", (0, N + 80, 6), 8.0, (255, 100, 35), 350, shadows=False, tag_name="FireLight")

    # ---- the envoy's portrait over the mantel (media-fed plane in a gilt frame)
    # 3:4 canvas (the material crops the 16:9 still/video) in a deep, stepped gilt frame.
    PW, PH, PZ, F = 96.0, 128.0, 214.0, 16.0
    m_gilt_dark = parlour_mi("MI_Parlour_GiltDark", world, T["marble"], 30, tint=(0.42, 0.29, 0.12), rough=0.55, metal=1.0)

    def ring(name, w_out, h_out, w_in, h_in, depth, y, mat):
        bw, bh = (w_out - w_in) / 2.0, (h_out - h_in) / 2.0
        block(name + "_T", (0, y, PZ + (h_in + bh) / 2.0), (w_out, depth, bh), mat)
        block(name + "_B", (0, y, PZ - (h_in + bh) / 2.0), (w_out, depth, bh), mat)
        block(name + "_L", (-(w_in + bw) / 2.0, y, PZ), (bw, depth, h_in), mat)
        block(name + "_R", ((w_in + bw) / 2.0, y, PZ), (bw, depth, h_in), mat)

    block("PortraitBacking", (0, face + 1.5, PZ), (PW + 8, 3, PH + 8), m_wood)
    ring("PortraitFrame", PW + 2 * F, PH + 2 * F, PW + 8, PH + 8, 6, face + 3, m_gilt)           # outer moulding
    ring("PortraitFrameOgee", PW + 2 * F - 9, PH + 2 * F - 9, PW + 11, PH + 11, 5, face + 7.5, m_gilt)  # raised ogee
    ring("PortraitFrameSight", PW + 11, PH + 11, PW, PH, 4, face + 5, m_gilt_dark)               # dark sight edge
    for i, (cx, cz) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        block("PortraitFrameBoss_%d" % i, (cx * (PW / 2 + F - 4), face + 9, PZ + cz * (PH / 2 + F - 4)), (9, 5, 9), m_gilt,
              mesh="/Engine/BasicShapes/Sphere")
    block("PortraitFrameCrest", (0, face + 7, PZ + PH / 2 + F + 5), (34, 5, 10), m_gilt, mesh="/Engine/BasicShapes/Cylinder", rot=(90, 0, 0))
    block("PortraitFrameCrestTop", (0, face + 8, PZ + PH / 2 + F + 12), (10, 5, 10), m_gilt, mesh="/Engine/BasicShapes/Sphere")
    portrait = spawn(unreal.StaticMeshActor, (0, face + 3.5, PZ), (0, 0, 90), "EnvoyPortrait")
    portrait.set_actor_scale3d(unreal.Vector(PW / 100.0, PH / 100.0, 1.0))
    comp = portrait.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    comp.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane"))
    comp.set_material(0, still_mat)
    comp.set_editor_property("cast_shadow", False)
    tag(portrait, "Portrait")

    # ---- mantel garniture: candlesticks and a clock
    for sx in (-1, 1):
        x = sx * 78
        block("MantelStick_%d" % sx, (x, face + 9, 136), (6, 6, 22), m_brass, mesh="/Engine/BasicShapes/Cylinder")
        block("MantelCandle_%d" % sx, (x, face + 9, 155), (3, 3, 16), m_wax, mesh="/Engine/BasicShapes/Cylinder")
        block("MantelFlame_%d" % sx, (x, face + 9, 166), (2.2, 2.2, 5), m_flame, mesh="/Engine/BasicShapes/Sphere")
        parlour_light("MantelCandleLight_%d" % sx, (x, face + 16, 168), 3.0, (255, 175, 95), 380, shadows=False, tag_name="CandleLight")

    # ---- sash windows with curtains, either side of the breast
    for sx in (-1, 1):
        wx, ww, wz0, wz1 = sx * 232.0, 92.0, 70.0, 250.0
        block("Window_Glass_%d" % sx, (wx, N + 1.5, (wz0 + wz1) / 2), (ww, 1, wz1 - wz0), m_night)
        block("Window_Sill_%d" % sx, (wx, N + 8, wz0 - 2), (ww + 16, 16, 4), m_paint)
        for side in (-1, 1):
            block("Window_Jamb_%d_%d" % (sx, side), (wx + side * (ww / 2 + 4), N + 4, (wz0 + wz1) / 2), (8, 8, wz1 - wz0 + 8), m_paint)
        block("Window_Head_%d" % sx, (wx, N + 4, wz1 + 4), (ww + 16, 8, 8), m_paint)
        block("Window_MeetingRail_%d" % sx, (wx, N + 3, (wz0 + wz1) / 2), (ww, 4, 4), m_paint)
        for k in (1, 2):  # 6-over-6 glazing bars
            block("Window_BarV_%d_%d" % (sx, k), (wx - ww / 2 + k * ww / 3, N + 2.5, (wz0 + wz1) / 2), (2, 2, wz1 - wz0), m_paint)
        for k in (1, 2, 4, 5):
            block("Window_BarH_%d_%d" % (sx, k), (wx, N + 2.5, wz0 + k * (wz1 - wz0) / 6), (ww, 2, 2), m_paint)
        block("Pelmet_%d" % sx, (wx, N + 14, 272), (ww + 70, 12, 22), m_wood)
        for side in (-1, 1):
            block("Curtain_%d_%d" % (sx, side), (wx + side * (ww / 2 + 6), N + 10, 145), (44, 6, 250), m_curtain)
        block("Window_Moon_%d" % sx, (wx, N - 2, (wz0 + wz1) / 2), (ww - 10, 1, wz1 - wz0 - 10), m_night)

    # ---- Turkey carpet, table, chairs, candles and papers
    block("Rug", (0, 40, 0.6), (330, 230, 1.2), m_rug)
    TZ, TY = 76.0, 40.0
    block("Table_Top", (0, TY, TZ - 2), (190, 96, 4), m_wood)
    block("Table_Apron", (0, TY, TZ - 9), (176, 82, 10), m_wood)
    for i, (x, y) in enumerate(((-84, TY - 42), (84, TY - 42), (-84, TY + 42), (84, TY + 42))):
        block("Table_Leg_%d" % i, (x, y, (TZ - 14) / 2), (7, 7, TZ - 14), m_wood, mesh="/Engine/BasicShapes/Cylinder")
    for i, (x, y) in enumerate(((-72, TY - 18), (72, TY - 18))):
        block("TableStick_%d" % i, (x, y, TZ + 1.5), (12, 12, 3), m_brass, mesh="/Engine/BasicShapes/Cylinder")
        block("TableStickStem_%d" % i, (x, y, TZ + 10), (3.5, 3.5, 16), m_brass, mesh="/Engine/BasicShapes/Cylinder")
        block("TableCandle_%d" % i, (x, y, TZ + 26), (3, 3, 16), m_wax, mesh="/Engine/BasicShapes/Cylinder")
        block("TableFlame_%d" % i, (x, y, TZ + 37), (2.2, 2.2, 5), m_flame, mesh="/Engine/BasicShapes/Sphere")
        parlour_light("TableCandleLight_%d" % i, (x, y, TZ + 42), 4.0, (255, 170, 90), 450, tag_name="CandleLight")
    block("Papers_A", (-12, TY + 10, TZ + 0.3), (30, 22, 0.6), m_paper_sheet, rot=(0, 8, 0))
    block("Papers_B", (16, TY + 14, TZ + 0.6), (30, 22, 0.6), m_paper_sheet, rot=(0, -14, 0))
    block("Inkwell", (40, TY + 18, TZ + 3), (6, 6, 6), m_iron, mesh="/Engine/BasicShapes/Cylinder")
    for name, y, back in (("Chair_Player", TY + 102, 1),):  # the envoy is present as his portrait
        block(name + "_Seat", (0, y, 46), (56, 52, 5), m_wood)
        block(name + "_Cushion", (0, y, 50), (50, 46, 4), m_velvet)
        block(name + "_Back", (0, y + back * 24, 82), (54, 5, 64), m_wood)
        block(name + "_BackPad", (0, y + back * 21, 84), (40, 2, 46), m_velvet)
        for i, (dx, dy) in enumerate(((-24, -22), (24, -22), (-24, 22), (24, 22))):
            block(name + "_Leg%d" % i, (dx, y + dy, 22), (5, 5, 44), m_wood)

    # ---- low warm fill so the room reads (Lumen does the bounce)
    parlour_light("FillWarm", (0, 150, 140), 3.0, (255, 185, 125), 900, shadows=False, source_radius=60)

    # ---- manual exposure, warm grade
    pp = spawn(unreal.PostProcessVolume, (0, 0, 160), (0, 0, 0), "PP_Parlour")
    pp.set_editor_property("unbound", True)
    st = pp.get_editor_property("settings")
    for k, v in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                 ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False),
                 ("override_auto_exposure_bias", True), ("auto_exposure_bias", PARLOUR_EXPOSURE_COMP),
                 ("override_bloom_intensity", True), ("bloom_intensity", 0.25),
                 ("override_vignette_intensity", True), ("vignette_intensity", 0.5),
                 ("override_white_temp", True), ("white_temp", 5600.0),
                 ("override_film_grain_intensity", True), ("film_grain_intensity", 0.08)):
        st.set_editor_property(k, v)
    pp.set_editor_property("settings", st)

    # ---- camera at the player's chair, portrait centred above the dialogue panel
    cam = spawn(unreal.CameraActor, (0, 250, 148), (3.5, -90, 0), "RoomCamera")
    cc = cam.get_component_by_class(unreal.CameraComponent)
    cc.set_editor_property("field_of_view", 62.0)
    tag(cam, "RoomCamera")
    save(les)
    LOG("PA250: built L_DiplomacyRoom (parlour)")

def build_ftest_bot_spacing():
    """Flat Lexington-like layout for APA250BotSpacingTest (3 CPs, battle GM)."""
    make_tuning()
    les = new_level("/Game/Tests/FTEST_BotSpacing")
    set_game_mode("PA250BattleGameMode")
    # Flat ground pad.
    ground = spawn(unreal.StaticMeshActor, (0, 0, 0), (0, 0, 0), "Ground")
    ground.set_actor_scale3d(unreal.Vector(120, 120, 1))
    gcomp = ground.static_mesh_component
    gcomp.set_mobility(unreal.ComponentMobility.STATIC)
    gcomp.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube"))
    posts = (
        ("green", (0, 0, 100), ""),
        ("meetinghouse", (-4000, -2000, 100), "continental"),
        ("buckman", (-4000, 2000, 100), "continental"),
    )
    for pid, loc, owner in posts:
        a = spawn(unreal.PA250ControlPoint, loc, (0, 0, 0), "CP_%s" % pid)
        a.set_editor_property("post_id", pid)
        if owner:
            a.set_editor_property("initial_owner", owner)
        a.set_editor_property("radius", 1000.0)
    # Functional test actor.
    try:
        ft = spawn(unreal.PA250BotSpacingTest, (0, 0, 200), (0, 0, 0), "FTEST_BotSpacing")
        ft.set_editor_property("duration_seconds", 90.0)
    except Exception as e:
        LOG("PA250: APA250BotSpacingTest spawn skipped (%s)" % e)
    # Spawns
    for tag_name, xy in (("british", (5000, 0)), ("continental", (-6000, 0))):
        ps = spawn(unreal.PlayerStart, (xy[0], xy[1], 120), (0, 180 if tag_name == "british" else 0, 0), "Start_%s" % tag_name)
        tag(ps, tag_name)
    save(les)
    LOG("PA250: built /Game/Tests/FTEST_BotSpacing")


BUILDERS = {
    "campaign": build_campaign,
    "lexington": build_lexington,
    "diplomacy": build_diplomacy,
    "ftest_bot_spacing": build_ftest_bot_spacing,
    "lexington_battle_road": build_lexington_battle_road,
    "data": make_tuning,  # DataAssets only (creates missing DA_*; never overwrites existing ones)
}
try:
    from pa250_levels_extra import register  # optional per-milestone extensions

    register(BUILDERS, globals())
except ImportError:
    pass

wanted = [
    w.strip()
    for w in os.environ.get(
        "PA250_LEVELS", ",".join(BUILDERS.keys())
    ).split(",")
    if w.strip()
]
for w in wanted:
    if w in BUILDERS:
        BUILDERS[w]()
LOG("PA250: build_levels done: " + ",".join(wanted))
