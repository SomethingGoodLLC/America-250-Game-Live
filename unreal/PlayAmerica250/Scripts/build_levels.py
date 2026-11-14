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
DAWN_SUN_LUX = 15.0
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
        # 12 s is the shipped bleed (C++ default; PA250Battle checks the asset for it). This
        # line wrote the retired 5 s until 2026-11-12, so every level rebuild undid the fix.
        conquest.set_editor_property("bleed_interval_seconds", 12.0)
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
    """Add an actor tag (keeps the ones already there)."""
    tags = [t for t in actor.get_editor_property("tags") if str(t) != name]
    actor.set_editor_property("tags", tags + [unreal.Name(name)])


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

# Battlefield kit (tools/art/battlefield_kit.py -> Scripts/import_battlefield_kit.py): period
# buildings, fieldstone walls, rail fences and props with photoreal PBR surfaces
# (tools/art/build_battlefield_textures.py). Used when imported; the graybox stays the fallback.
KIT = "/Game/PA250/Battlefield"


def kit_mesh(name):
    path = "%s/Meshes/SM_%s" % (KIT, name)
    return path if unreal.EditorAssetLibrary.does_asset_exist(path) else None


def kit_tex(name, suffix="BaseColor"):
    path = "%s/Textures/T_%s_%s" % (KIT, name, suffix)
    return path if unreal.EditorAssetLibrary.does_asset_exist(path) else None


def make_foliage_material(name, base_tex, mask_tex, roughness, up_normal=False, leaf=False):
    """Masked, two-sided, default-lit material: base = texture RGB, mask = texture alpha
    (or a separate mask texture's R). mask_tex == "" means opaque (bark).
    up_normal: shade with a world-up normal (grass and flowers). Their card normals face
    sideways, so under a low sun half of every clump went black against the lit ground.
    leaf: two-sided foliage shading (crowns and the far trees' billboards), so a tree seen
    against the light glows green as real leaves do instead of being a black cut-out."""
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
            mask_asset = unreal.EditorAssetLibrary.load_asset(KITE + mask_tex)
            # The sampler has to match the texture (Yarrow's mask is sRGB, Buttercup's is not),
            # or the material fails to compile and the plant is drawn with the grey grid.
            mask = _expr(mat, M.MaterialExpressionTextureSample, -600, 250, texture=mask_asset,
                         sampler_type=(M.MaterialSamplerType.SAMPLERTYPE_COLOR if mask_asset.get_editor_property("srgb")
                                       else M.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR))
            M.MaterialEditingLibrary.connect_material_property(mask, "R", M.MaterialProperty.MP_OPACITY_MASK)
        else:
            M.MaterialEditingLibrary.connect_material_property(base, "A", M.MaterialProperty.MP_OPACITY_MASK)
    rough = _expr(mat, M.MaterialExpressionConstant, -300, 200, r=float(roughness))
    M.MaterialEditingLibrary.connect_material_property(rough, "", M.MaterialProperty.MP_ROUGHNESS)
    if up_normal:
        set_props(mat, (("tangent_space_normal", False),), name)
        up = _expr(mat, M.MaterialExpressionConstant3Vector, -300, 350, constant=M.LinearColor(0.0, 0.0, 1.0, 1.0))
        M.MaterialEditingLibrary.connect_material_property(up, "", M.MaterialProperty.MP_NORMAL)
    if leaf:
        set_props(mat, (("shading_model", M.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE),), name)
        through = _expr(mat, M.MaterialExpressionMultiply, -300, 480)
        _link(base, "RGB", through, "A")
        _link(_expr(mat, M.MaterialExpressionConstant3Vector, -500, 520,
                    constant=M.LinearColor(0.75, 0.85, 0.35, 1.0)), "", through, "B")
        M.MaterialEditingLibrary.connect_material_property(through, "", M.MaterialProperty.MP_SUBSURFACE_COLOR)
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
                up = any(k in mname for k in ("Grass", "Yarrow", "Buttercup"))
                out.append((i, make_foliage_material(mname, base, mask, rough, up_normal=up,
                                                     leaf="Leaves" in mname or "Billboard" in mname)))
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


# ---------------------------------------------------------------- battlefield layouts
#
# Two battle levels share every function below; a layout is the set of module globals one level
# is built from (use_layout). Units are cm; +X is east and +Y is south, so a compass bearing b
# is the Unreal yaw b - 90.


def _ray(origin, bearing_deg, dist):
    """Point `dist` cm from origin on a compass bearing (0 = north = -Y, 90 = east = +X)."""
    a = math.radians(bearing_deg)
    return (origin[0] + math.sin(a) * dist, origin[1] - math.cos(a) * dist)


def _off(p0, p1, t, lateral=0.0):
    """Point at fraction t of p0->p1, moved `lateral` cm to the RIGHT of the direction of travel
    (heading north, right is east)."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    ln = math.hypot(dx, dy)
    return (p0[0] + dx * t - dy / ln * lateral, p0[1] + dy * t + dx / ln * lateral)


def _yaw_of(p0, p1):
    return math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))


def _lexington_layout():
    """Lexington Common at sunrise, 19 April 1775, at its real size and compass orientation.

    Record (Lexington Historical Society / NPS / town markers): the Common is the triangle
    between the Concord road (Massachusetts Avenue), the Bedford road and the lane closing its
    north-west end (Harrington Road), about 2.5 acres, its point toward Boston. The 1714
    meetinghouse stood on the Common at that point "facing directly down the road leading to
    Boston", the Belfry (moved onto the Common in 1768) and a schoolhouse stood near it, and
    Buckman Tavern stood across the Bedford road on the meetinghouse's right as the regulars
    came up. Parker's company formed toward the far end, between the Bedford road and the
    Concord road. Jonathan Harrington's house faced the Common from its north-west side, the
    Munroe houses from across the Concord road; Belfry Hill rises south of the Concord road.
    Reading: side lengths (165 m, 150 m, about 120 m) and bearings (Concord road 300, Bedford
    road 350, Boston road from 120) are taken from the present street plan from memory, not
    surveyed; exact building footprints, the belfry's and schoolhouse's spots, yard fences,
    barns, fields and trees are plausible, not documented.
    GAME INVENTION: capture posts, spawn points, the Battle Road and Patriot cover walls and
    the militia muster points."""
    fork = (5100.0, 6700.0)
    west = _ray(fork, 300, 16500)
    north = _ray(fork, 350, 15000)
    boston1 = _ray(fork, 120, 11000)
    boston2 = _ray(boston1, 113, 20000)
    concord1 = _ray(west, 297, 11000)
    concord2 = _ray(concord1, 285, 11500)
    bedford1 = _ray(north, 352, 9000)
    bedford2 = _ray(bedford1, 8, 9000)
    meeting = _ray(fork, 325, 3600)
    tavern = _off(fork, north, 7000 / 15000.0, 2800)
    green = _ray(meeting, 323, 7000)  # middle of Parker's line

    def br(along, lateral):  # along the Boston road from the fork; lateral > 0 = its south-west side
        return _off(fork, boston1, along / 11000.0, lateral)

    harrington_yaw = _yaw_of(west, north) + 90.0  # fronts across Harrington Road face the Common
    munroe_yaw = _yaw_of(fork, west) + 90.0       # fronts across the Concord road face the Common
    return {
        "MAP_NAME": "L_Lexington",
        "TERRAIN_DEST": "/Game/PA250/Meshes", "TERRAIN_NAME": "SM_PA250_Terrain",
        "MEADOW_NAME": "M_PA250_Meadow",
        "GREEN_XY": green,
        "OPEN_GROUND": (fork, west, north),  # the Common itself carried no trees
        "ROADS": (
            (fork, west, 450), (fork, north, 450), (west, north, 400),
            (fork, boston1, 600), (boston1, boston2, 600),
            (west, concord1, 750), (concord1, concord2, 750),
            (north, bedford1, 520), (bedford1, bedford2, 520),
        ),
        # Building footprints (centre, size w,d,h) for the graybox fallback and clearances.
        "MEETINGHOUSE": (meeting, (2200, 1500, 900)), "MEETINGHOUSE_FACING": 45.0,
        "TAVERN": (tavern, (1900, 1100, 760)), "TAVERN_FACING": 170.0,
        # (label, post_id, xy, initial owner, z or None = on the ground)
        "POSTS": (
            ("Objective_Green", "green", green, None, None),
            ("Objective_Meetinghouse", "meetinghouse", _ray(meeting, 325, 2000), "continental", None),
            ("Objective_Buckman", "buckman", _ray(tavern, 260, 1500), "continental", None),
        ),
        # side: (centre of the front rank, yaw the men face, spacing). 14 starts, four abreast.
        "SPAWNS": {"continental": ((1500.0, -3000.0), 77.0, 280.0), "british": (br(6000, 0), 210.0, 350.0)},
        "BASE_PADS": ((green[0], green[1], 2300),),
        "TRAMPLED": ((green[0], green[1], 900, 2600, 0.55),),
        # (label, kit asset, (x, y), facing yaw, clear radius cm)
        "VILLAGE": (
            ("Belfry", "Belfry", _ray(meeting, 305, 2700), 45, 300),
            ("Schoolhouse", "Schoolhouse", _ray(meeting, 300, 5500), munroe_yaw + 180, 600),
            ("TavernShed", "WagonShed", _ray(tavern, 80, 1900), 170, 480),
            ("TavernWell", "Well", _ray(tavern, 20, 1500), 170, 300),
            ("TavernWoodpile", "Woodpile", _ray(tavern, 80, 900), 80, 150),
            ("TavernSign", "TavernSign", _ray(tavern, 238, 1900), 80, 60),
            ("TavernHitchingRail", "HitchingRail", _ray(tavern, 280, 1150), 170, 170),
            ("Barn_Tavern", "Barn", _ray(tavern, 70, 4200), 170, 800),
            ("House_JonathanHarrington", "HouseGable", _off(west, north, 0.78, -2500), harrington_yaw, 800),
            ("House_DanielHarrington", "HouseSaltbox", _off(west, north, 0.2, -2600), harrington_yaw, 800),
            ("Smithy_DanielHarrington", "WagonShed", _off(west, north, 0.34, -2300), harrington_yaw, 480),
            ("Barn_Harrington", "Barn", _off(west, north, 0.5, -5200), harrington_yaw, 800),
            ("House_MarrettMunroe", "HouseGable", _off(fork, west, 0.45, -2500), munroe_yaw, 800),
            ("House_NathanMunroe", "HouseCape", _off(fork, west, 0.9, -2500), munroe_yaw, 750),
            ("Barn_Munroe", "Barn", _off(fork, west, 0.47, -5600), munroe_yaw, 800),
            ("House_BedfordRoad", "HouseSaltbox", _off(fork, north, 0.85, 2600), 170, 800),
            ("House_BostonRoad", "HouseCape", br(2600, 3200), _yaw_of(fork, boston1) - 90.0, 750),
        ),
        "FIELDS": (
            (_off(west, north, 0.45, -8500), (2200, 1400), _yaw_of(west, north)),
            (_ray(tavern, 80, 7800), (1500, 1800), -10.0),
            (_off(fork, west, 0.8, -6500), (1800, 1200), _yaw_of(fork, west)),
            (br(7200, 5600), (2000, 1300), _yaw_of(fork, boston1)),
        ),
        "WOODLOTS": (
            ((-15500, -11000), 5200, 22), ((-4000, -17500), 4800, 20), ((9000, -15500), 5200, 22),
            ((17500, -6000), 4600, 16), ((20000, 4500), 5000, 18), ((8500, 18500), 5200, 22),
            ((-7500, 13500), 5200, 24), ((-18500, 7000), 5200, 20), ((-21500, -1500), 4200, 14),
            ((7000, -11500), 1800, 5), ((15000, 2000), 1700, 5), ((-13500, 4500), 1600, 5),
        ),
        "TREE_COUNT": 20, "TREE_RING": (6000.0, 15500.0),
        # Stone walls flanking the Concord road west of the Common and the Boston road east of it.
        "ROAD_WALLS": (
            ("Wall_N", _off(west, concord1, 0.14, 520), _off(west, concord1, 0.95, 520)),
            ("Wall_S", _off(west, concord1, 0.14, -520), _off(west, concord1, 0.95, -520)),
            ("Wall_E", br(7700, -700), br(11000, -700)),
        ),
        # Battle Road (GAME INVENTION, after the running fight of the afternoon): field walls flank
        # the Boston road where the column approaches. 1.1 m collision, tagged PA250Cover.
        "BATTLE_ROAD_WALLS": (
            ("BattleRoad_RoadN", br(5500, -1700), br(1500, -1700)),
            ("BattleRoad_RoadS", br(5300, 1700), br(1700, 1700)),
            ("BattleRoad_FieldN1", br(4200, -4500), br(4200, -2100)),
            ("BattleRoad_FieldS1", br(3800, 2100), br(3800, 4500)),
            ("BattleRoad_FieldN2", br(800, -4700), br(800, -2300)),
            ("BattleRoad_FieldS2", br(1400, 2300), br(1400, 4700)),
        ),
        # Patriot cover (GAME INVENTION): walls on the far side of Harrington Road and of the
        # Concord road, 44-50 m from the Green post and facing it; the gap lets men through.
        "PATRIOT_WALLS": (
            ("PatriotCover_W1", _off(west, north, 0.36, -800), _off(west, north, 0.50, -800)),
            ("PatriotCover_W2", _off(west, north, 0.56, -800), _off(west, north, 0.70, -800)),
            ("PatriotCover_SW", _off(fork, west, 0.545, -700), _off(fork, west, 0.642, -700)),
        ),
        "PATRIOT_TREES": (
            _off(west, north, 0.28, -1500), _off(west, north, 0.53, -1900), _off(west, north, 0.64, -2400),
            _off(west, north, 0.92, -1400), _off(west, north, 0.08, -1300), _off(fork, west, 0.60, -1900),
            _off(fork, west, 0.74, -1500), _off(fork, west, 0.30, -1500), _off(fork, north, 0.66, 1300),
            _off(fork, north, 0.97, 1500),
        ),
        "MUSTER_POINTS": (
            ("Muster_Continental_North", _off(north, bedford1, 0.45)),
            ("Muster_Continental_South", (1500.0, 12500.0)),
            ("Muster_Continental_West", _off(west, concord1, 0.5)),
        ),
        # Post-and-rail yard fences along the roads (the Common itself was open ground).
        "COMMON_FENCES": (
            ("Fence_NE", _off(fork, north, 0.54, 900), _off(fork, north, 0.70, 900)),
            ("Fence_SE", _off(fork, west, 0.28, -800), _off(fork, west, 0.39, -800)),
            ("Fence_SW", _off(fork, west, 0.80, -800), _off(fork, west, 0.87, -800)),
            ("Fence_NW", _off(west, north, 0.74, -900), _off(west, north, 0.86, -900)),
        ),
        "TERRAIN_X": (-30000.0, 32000.0), "TERRAIN_Y": (-25000.0, 26000.0), "TERRAIN_CELL": 250.0,
        "AMP": (45.0, 255.0, 700.0), "AMP_R": (8500.0, 17000.0, 28000.0),
        "HILLS": ((_off(fork, west, 0.5, -11000), 6500.0, 1300.0),),  # Belfry Hill
        "CAUSEWAYS": (), "RIVER_PTS": (), "RIVER_HALF": 0.0, "WATER_Z": 0.0, "RIVER_BED_Z": 0.0, "BRIDGE": None,
        "NAV": ((0.0, 500.0), (175, 150, 12)),
        # Sunrise (about 5 a.m.): low warm sun in the ENE (light travels WSW), 15 deg up.
        "LIGHT": {"sun_rot": (-15, 165, 0), "sun_lux": DAWN_SUN_LUX, "sun_color": (255, 165, 95), "exposure": DAWN_EXPOSURE_EV,
                  "fog_density": 0.014, "white_temp": 7300.0, "gain": (1.12, 1.0, 0.82), "pp": "PP_Dawn"},
        # tag: ((x, y), height above ground, (pitch, yaw), fov)
        "CAMERAS": (
            ("Shot_Spawn", (1300.0, -5300.0), 240, (-5, 78), 70.0),
            ("Shot_Green", br(1700, 150), 190, (-3, _yaw_of(boston1, fork) + 10), 65.0),
            ("Shot_Wide", (-6200.0, 7800.0), 650, (-9, -55), 75.0),
        ),
    }


def _concord_layout():
    """The North Bridge, Concord, about 9.30 a.m. on 19 April 1775; the bridge is at the origin.

    Record (NPS Minute Man NHP, "The Groton Road" and North Bridge pages): the bridge of 1760
    crossed the Concord River on the road from the village; from its west end a one-rod (16.5 ft)
    gravel causeway ran about 400 ft west through wet meadow, a low stone wall topped with a rail
    on its north edge, the river on its south side, then forked - the east branch north "up ye
    hill" past the Buttricks', the west branch along the river past David Brown's toward Barrett's
    farm. The militia formed in Brown's pasture on the high ground north-west (the Muster Field)
    and came down the east branch onto the causeway in column; the regulars had fallen back to
    the east end of the bridge. Firing opened at about 50 yards. East of the bridge the lane ran
    to the village road by the Old Manse (1770) and Elisha Jones's house; in 1775 that road did
    not continue north. Two of the British dead lie by a wall south-east of the bridge.
    Reading: river width (about 30 m) and course, bridge length (see the kit recipe), the hill's
    height, and every building's exact spot and form are approximations from the modern site.
    GAME INVENTION: post and spawn positions (the real muster field is 300 m off; the Muster
    ground post stands where the column came onto the causeway), muster points, tree positions."""
    cause_w = (-13900.0, 0.0)
    east_branch = (cause_w, (-15200.0, -4500.0), (-11500.0, -11500.0), (-8500.0, -17500.0))
    west_branch = (cause_w, (-20000.0, 1200.0), (-30000.0, 3500.0))
    lane = ((1700.0, 0.0), (9000.0, 300.0), (16500.0, 0.0))
    town_road = ((16500.0, 0.0), (15800.0, 12000.0), (14500.0, 22500.0))
    roads = [((-1700.0, 0.0), cause_w, 500)]
    for line, w in ((east_branch, 450), (west_branch, 450), (lane, 500), (town_road, 600)):
        roads += [(a, b, w) for a, b in zip(line, line[1:])]
    return {
        "MAP_NAME": "L_Concord",
        "TERRAIN_DEST": "/Game/PA250/Terrain/Concord", "TERRAIN_NAME": "SM_PA250_TerrainConcord",
        "MEADOW_NAME": "M_PA250_MeadowConcord",
        "GREEN_XY": (0.0, 0.0),
        "OPEN_GROUND": None,
        "ROADS": tuple(roads),
        "MEETINGHOUSE": None, "MEETINGHOUSE_FACING": 0.0, "TAVERN": None, "TAVERN_FACING": 0.0,
        # Post ids are the game's fixed three (APA250BattleGameMode::ConfigurePostForScenario names
        # them North Bridge, Muster ground and Far bridgehead for this fight).
        "POSTS": (
            ("Objective_NorthBridge", "green", (0.0, -150.0), None, 60.0 + 90.0 + 140.0),
            ("Objective_MusterGround", "meetinghouse", (-14300.0, -1500.0), "continental", None),
            ("Objective_FarBridgehead", "buckman", (5600.0, 1000.0), "british", None),
        ),
        "SPAWNS": {"continental": ((-15700.0, -3700.0), 14.0, 280.0), "british": ((11500.0, 150.0), 180.0, 350.0)},
        "BASE_PADS": (),
        "TRAMPLED": ((-2600.0, 0.0, 300, 1200, 0.5), (2600.0, 0.0, 300, 1200, 0.5)),
        "VILLAGE": (
            ("OldManse", "OldManse", (9600.0, 5600.0), 0, 850),
            ("Barn_Manse", "Barn", (11900.0, 7400.0), 0, 800),
            ("ManseWell", "Well", (8300.0, 6900.0), 0, 300),
            ("House_ElishaJones", "HouseSaltbox", (18900.0, -900.0), 180, 800),
            ("Shed_ElishaJones", "WagonShed", (19300.0, 1300.0), 180, 480),
            ("House_DavidBrown", "HouseCape", (-17600.0, -1500.0), 100, 750),
            ("Barn_DavidBrown", "Barn", (-20200.0, -2500.0), 100, 800),
            ("Woodpile_DavidBrown", "Woodpile", (-18600.0, -2300.0), 10, 150),
            ("House_Buttrick", "HouseGable", (-9300.0, -14200.0), 150, 800),
            ("Barn_Buttrick", "Barn", (-12300.0, -15600.0), 150, 800),
        ),
        "FIELDS": (
            ((6200.0, 6800.0), (1400, 1700), 8.0),
            ((-4500.0, -13800.0), (2200, 1300), 20.0),
            ((-22500.0, -6500.0), (2000, 1400), -12.0),
            ((21500.0, 6500.0), (1700, 2200), 4.0),
        ),
        "WOODLOTS": (
            ((-18000, -18000), 5200, 24), ((-2000, -20500), 4500, 18), ((11000, -17000), 4500, 16),
            ((23000, -9000), 4000, 14), ((23500, 13500), 4500, 16), ((6000, 17500), 4500, 18),
            ((-8000, 15500), 5000, 20), ((-21000, 15000), 4500, 16), ((-26000, -9000), 3500, 10),
            ((3500, -8500), 1500, 4), ((-10500, 6500), 1500, 4), ((-24500, 8500), 1800, 5),
        ),
        "TREE_COUNT": 16, "TREE_RING": (3500.0, 14500.0),
        # The causeway's north wall (record), lane and lot walls east of the bridge (reading),
        # and the walls of Brown's pasture on the hillside (reading).
        "ROAD_WALLS": (
            ("Wall_Causeway", (-13200.0, -340.0), (-2500.0, -340.0)),
            ("Wall_LaneN", (2700.0, -900.0), (10500.0, -650.0)),
            ("Wall_Graves", (2700.0, 1500.0), (3600.0, 4300.0)),
            ("Wall_ManseField", (7200.0, 1200.0), (15300.0, 1050.0)),
            ("Wall_TownRoad", (15750.0, 2200.0), (15250.0, 10500.0)),
            ("Wall_Pasture1", (-13300.0, -4600.0), (-8300.0, -6900.0)),
            ("Wall_Pasture2", (-6300.0, -7600.0), (-3600.0, -9800.0)),
        ),
        "BATTLE_ROAD_WALLS": (), "PATRIOT_WALLS": (), "PATRIOT_TREES": (),
        "MUSTER_POINTS": (
            ("Muster_Continental_North", (-11900.0, -10200.0)),
            ("Muster_Continental_South", (-6800.0, -10800.0)),
            ("Muster_Continental_West", (-21500.0, 1400.0)),
        ),
        "COMMON_FENCES": (
            ("Fence_Manse", (7600.0, 4300.0), (7500.0, 8200.0)),
            ("Fence_Brown", (-16300.0, -300.0), (-19700.0, 350.0)),
            ("Fence_Jones", (17200.0, -2600.0), (17300.0, 2600.0)),
        ),
        "TERRAIN_X": (-30000.0, 28000.0), "TERRAIN_Y": (-24000.0, 23000.0), "TERRAIN_CELL": 250.0,
        "AMP": (18.0, 150.0, 600.0), "AMP_R": (7000.0, 16000.0, 27000.0),
        # The ridge north-west of the bridge (Brown's pasture and the Buttrick farms) and the
        # slightly higher ground of the Manse and the village road.
        "HILLS": (((-13500.0, -13500.0), 9000.0, 1500.0), ((-3500.0, -16000.0), 7000.0, 1100.0),
                  ((17500.0, 3000.0), 9000.0, 450.0)),
        # Raised road beds (p0, p1, half width, top level): the causeway and the east approach.
        "CAUSEWAYS": (((-1700.0, 0.0), cause_w, 250.0, 60.0), ((1700.0, 0.0), (4600.0, 100.0), 250.0, 60.0)),
        "RIVER_PTS": ((-30500.0, 8500.0), (-20000.0, 6000.0), (-14000.0, 4200.0), (-8000.0, 2900.0), (-3200.0, 2300.0),
                      (-900.0, 1900.0), (-100.0, 700.0), (0.0, -400.0), (0.0, -2600.0), (1200.0, -7000.0),
                      (4500.0, -13000.0), (9500.0, -24500.0)),
        "RIVER_HALF": 1400.0, "WATER_Z": -70.0, "RIVER_BED_Z": -320.0,
        # kit asset, centre, yaw of the span, z of the deck ends, half length and half width, and the
        # rails' distance from the centreline (cm)
        "BRIDGE": {"asset": "NorthBridge1775", "xy": (0.0, 0.0), "yaw": 0.0, "z": 60.0, "half_len": 1910.0, "half_wid": 300.0, "rail_y": 248.0},
        "NAV": ((-3500.0, -2000.0), (195, 125, 12)),
        # Mid-morning: the sun in the ESE (bearing about 115), 40 deg up.
        "LIGHT": {"sun_rot": (-40, 205, 0), "sun_lux": 11.0, "sun_color": (255, 238, 214), "exposure": 0.35,
                  "fog_density": 0.005, "white_temp": 6700.0, "gain": (1.04, 1.0, 0.93), "pp": "PP_Morning"},
        "CAMERAS": (
            ("Shot_Spawn", (-15200.0, -900.0), 260, (-4, 3), 60.0),
            ("Shot_Green", (-4200.0, -1900.0), 210, (-3, 22), 60.0),
            ("Shot_Wide", (-9500.0, -7800.0), 350, (-8, 39), 70.0),
        ),
    }


LAYOUTS = {"lexington": _lexington_layout, "concord": _concord_layout}
LAYOUT = None


def spawn_points(side):
    """The 14 PlayerStarts of one side: four abreast facing `yaw`, ranks closing up behind."""
    (cx, cy), yaw, sp = SPAWNS[side]
    fx, fy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return [(cx - fy * ((i % 4) - 1.5) * sp - fx * (i // 4) * sp, cy + fx * ((i % 4) - 1.5) * sp - fy * (i // 4) * sp)
            for i in range(14)]


def _catmull(pts, step=500.0):
    out = []
    for i in range(len(pts) - 1):
        p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, len(pts) - 1)]
        n = max(2, int(math.hypot(p2[0] - p1[0], p2[1] - p1[1]) / step))
        for k in range(n):
            t = k / float(n)
            out.append(tuple(
                0.5 * (2 * p1[a] + (p2[a] - p0[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t * t
                       + (3 * p1[a] - p0[a] - 3 * p2[a] + p3[a]) * t * t * t) for a in (0, 1)))
    out.append(tuple(pts[-1]))
    return out


def _in_poly(x, y, poly):
    inside = False
    for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            inside = not inside
    return inside


def all_wall_lines():
    return list(BATTLE_ROAD_WALLS) + list(PATRIOT_WALLS) + list(COMMON_FENCES) + list(ROAD_WALLS)


def _pick_trees():
    """Field and roadside trees on a ring round the fight, clear of everything placed by hand
    (fixed seed; replaces a hand-kept list, which the audits then had to police)."""
    import random

    rng = random.Random(41775)
    spots = [xy for _l, xy in MUSTER_POINTS] + [p[2] for p in POSTS] + spawn_points("continental") + spawn_points("british")
    lines = all_wall_lines()
    out = []
    for _ in range(TREE_COUNT * 80):
        if len(out) >= TREE_COUNT:
            break
        a, r = rng.uniform(0, 2 * math.pi), rng.uniform(*TREE_RING)
        x, y = GREEN_XY[0] + math.cos(a) * r, GREEN_XY[1] + math.sin(a) * r * 0.85
        if not keep_clear(x, y, 250) or in_field(x, y, 300) or (OPEN_GROUND and _in_poly(x, y, list(OPEN_GROUND))):
            continue
        if any(_seg_dist(x, y, p0, p1) < 450 for _n, p0, p1 in lines):
            continue
        if any(math.hypot(x - sx, y - sy) < 800 for sx, sy in spots):
            continue
        if any(math.hypot(x - tx, y - ty) < 900 for tx, ty in list(PATRIOT_TREES) + out):
            continue
        out.append((float(round(x)), float(round(y))))
    return tuple(out)


def use_layout(name):
    """Bind one level's layout to the module globals every function here reads."""
    global LAYOUT, PADS, RIVER, MEADOW_TREES
    g = globals()
    g.update(LAYOUTS[name]())
    LAYOUT = name
    RIVER = _catmull(RIVER_PTS) if RIVER_PTS else []
    pads = list(BASE_PADS)
    for (bx, by), _size in (b for b in (MEETINGHOUSE, TAVERN) if b):
        pads.append((bx, by, 1500))
    pads += [(p[2][0], p[2][1], 900) for p in POSTS if p[4] is None]
    pads += [(SPAWNS[s][0][0] - math.cos(math.radians(SPAWNS[s][1])) * 1.5 * SPAWNS[s][2],
              SPAWNS[s][0][1] - math.sin(math.radians(SPAWNS[s][1])) * 1.5 * SPAWNS[s][2], 1200) for s in sorted(SPAWNS)]
    pads += [(x, y, max(r, 500) + 150) for _l, _a, (x, y), _f, r in VILLAGE if r >= 400]
    PADS = tuple(pads)
    MEADOW_TREES = _pick_trees()


def in_field(x, y, margin=0.0):
    for (cx, cy), (hx, hy), yaw in FIELDS:
        c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        u = (x - cx) * c + (y - cy) * s_
        v = -(x - cx) * s_ + (y - cy) * c
        if abs(u) < hx + margin and abs(v) < hy + margin:
            return True
    return False


# ---------------------------------------------------------------- terrain


def _smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def _raw_height(x, y):
    d = math.hypot(x - GREEN_XY[0], y - GREEN_XY[1])
    amp = AMP[0] + AMP[1] * _smoothstep(AMP_R[0], AMP_R[1], d) + AMP[2] * _smoothstep(AMP_R[1], AMP_R[2], d)
    n = (
        0.55 * math.sin(x / 2900.0 + 0.7) * math.cos(y / 3400.0 - 0.4)
        + 0.30 * math.sin((0.6 * x + y) / 2100.0 + 1.3)
        + 0.15 * math.cos((x - 0.8 * y) / 1300.0)
    )
    h = amp * n
    for (hx, hy), r, top in HILLS:  # a rounded hill that is exactly zero beyond 1.7 r
        k = 1.0 - ((x - hx) ** 2 + (y - hy) ** 2) / (2.89 * r * r)
        if k > 0.0:
            h += top * k * k
    return h


def river_dist(x, y):
    """Distance (cm) to the river's centreline; huge when the layout has no river or it is far."""
    if not RIVER:
        return 1e9
    reach = RIVER_HALF + 7000.0
    if min(_seg_dist(x, y, a, b) for a, b in zip(RIVER_PTS, RIVER_PTS[1:])) > reach + 1500.0:
        return 1e9
    return min(_seg_dist(x, y, a, b) for a, b in zip(RIVER, RIVER[1:]))


def ground_z(x, y):
    """Terrain height (cm) used for the mesh and for every placed actor."""
    h = _raw_height(x, y)
    for px, py, r in PADS:
        w = 1.0 - _smoothstep(r, r * 1.9, math.hypot(x - px, y - py))
        if w > 0.0:
            h = h * (1.0 - w) + _raw_height(px, py) * w
    for p0, p1, hw, level in CAUSEWAYS:  # a raised bed where the ground is lower, never a cut
        w = 1.0 - _smoothstep(hw, hw + 350.0, _seg_dist(x, y, p0, p1))
        if w > 0.0:
            h += (max(h, level) - h) * w
    if RIVER:
        d = river_dist(x, y)
        if d < RIVER_HALF + 6000.0:
            # No hollow beside the river lies lower than its meadow, then the channel is cut.
            h += (max(h, 0.0) - h) * (1.0 - _smoothstep(RIVER_HALF + 450.0, RIVER_HALF + 6000.0, d))
            w = 1.0 - _smoothstep(RIVER_HALF - 350.0, RIVER_HALF + 450.0, d)
            h = h * (1.0 - w) + RIVER_BED_Z * w
    return h


def _mask_hlsl():
    """HLSL for the Custom node: float4(road, trampled, ploughed, furrow) from absolute world
    position (cm). Generated from the layout so level and material agree."""
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
    lines.append("float tr = 0;")
    for tx, ty, r0, r1, k in TRAMPLED:
        lines.append("tr = max(tr, %.2f * (1 - smoothstep(%.1f, %.1f, length(p - float2(%.1f, %.1f)))));" % (k, r0, r1, tx, ty))
    for post in POSTS:
        if post[4] is None:
            lines.append("tr = max(tr, 0.45 * (1 - smoothstep(300, 1100, length(p - float2(%.1f, %.1f)))));" % post[2])
    # Trodden yards in front of the village doors.
    for _label, asset, (vx, vy), _facing, r in VILLAGE:
        if r >= 400 and asset != "Belfry":
            lines.append("tr = max(tr, 0.5 * (1 - smoothstep(%.1f, %.1f, length(p - float2(%.1f, %.1f)))));"
                         % (r * 0.7, r * 1.7, vx, vy))
    if RIVER_PTS:
        # Wet mud on the banks and a bare bed, by height, only near the river's course.
        lines.append("float rd = 1e9;")
        for p0, p1 in zip(RIVER_PTS, RIVER_PTS[1:]):
            lines += [
                "a = float2(%.1f, %.1f); b = float2(%.1f, %.1f);" % (p0[0], p0[1], p1[0], p1[1]),
                "ab = b - a; t = saturate(dot(p - a, ab) / dot(ab, ab)); rd = min(rd, length(p - (a + ab * t)));",
            ]
        lines += [
            "float near = 1 - smoothstep(%.1f, %.1f, rd);" % (RIVER_HALF + 900.0, RIVER_HALF + 2600.0),
            "tr = max(tr, near * (1 - smoothstep(%.1f, %.1f, WP.z)));" % (WATER_Z + 25.0, WATER_Z + 95.0),
            "road = max(road, near * (1 - smoothstep(%.1f, %.1f, WP.z)));" % (WATER_Z - 45.0, WATER_Z + 5.0),
        ]
    lines += ["float plough = 0; float furrow = 0; float2 q; float e;"]
    for (cx, cy), (hx, hy), yaw in FIELDS:
        c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        lines += [
            "q = float2(dot(p - float2(%.1f, %.1f), float2(%.5f, %.5f)), dot(p - float2(%.1f, %.1f), float2(%.5f, %.5f)));"
            % (cx, cy, c, s_, cx, cy, -s_, c),
            "e = 1 - smoothstep(-120, 120, max(abs(q.x) - %.1f, abs(q.y) - %.1f));" % (hx, hy),
            "if (e > plough) { plough = e; furrow = sin(q.y * 0.0698); }",  # ridges every 90 cm
        ]
    lines.append("return float4(road, tr, plough, furrow);")
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
        "nodes": [{"mesh": 0, "name": TERRAIN_NAME}],
        "meshes": [
            {
                "name": TERRAIN_NAME,
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


def _write_gltf_mesh(path, name, pos, nrm, uv, idx):
    """A single-mesh glTF from flat float lists (glTF space: Y up, metres)."""
    import base64
    import json
    import struct

    blobs = [struct.pack("<%df" % len(pos), *pos), struct.pack("<%df" % len(nrm), *nrm),
             struct.pack("<%df" % len(uv), *uv), struct.pack("<%dI" % len(idx), *idx)]
    offsets, total = [], 0
    for blob in blobs:
        offsets.append(total)
        total += len(blob)
    nv = len(pos) // 3
    views = [{"buffer": 0, "byteOffset": offsets[k], "byteLength": len(blobs[k]), "target": 34963 if k == 3 else 34962}
             for k in range(4)]
    doc = {
        "asset": {"version": "2.0", "generator": "PA250 build_levels.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
        "buffers": [{"byteLength": total, "uri": "data:application/octet-stream;base64," + base64.b64encode(b"".join(blobs)).decode()}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": nv, "type": "VEC3",
             "min": [min(pos[k::3]) for k in range(3)], "max": [max(pos[k::3]) for k in range(3)]},
            {"bufferView": 1, "componentType": 5126, "count": nv, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": nv, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(idx), "type": "SCALAR"},
        ],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(doc, f)


def write_river_gltf(path, name):
    """The river's surface: a flat ribbon along the smoothed centreline at WATER_Z, wide enough
    to run under both banks. UV0 = world XY / 10 m (M_Water ripples on world position anyway)."""
    pos, nrm, uv, idx = [], [], [], []
    half = RIVER_HALF + 550.0
    for i, (x, y) in enumerate(RIVER):
        ax, ay = RIVER[max(i - 1, 0)]
        bx, by = RIVER[min(i + 1, len(RIVER) - 1)]
        ln = math.hypot(bx - ax, by - ay)
        nx, ny = -(by - ay) / ln, (bx - ax) / ln
        for side in (-1.0, 1.0):
            px, py = x + nx * half * side, y + ny * half * side
            pos += [px / 100.0, WATER_Z / 100.0, py / 100.0]  # UE (x, y, z) cm -> glTF (x, z, y) m
            nrm += [0.0, 1.0, 0.0]
            uv += [px / 1000.0, py / 1000.0]
    for i in range(len(RIVER) - 1):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        for tri in ((a, c, b), (b, c, d)):
            (x0, _y0, z0), (x1, _y1, z1), (x2, _y2, z2) = (pos[3 * k:3 * k + 3] for k in tri)
            up = (z1 - z0) * (x2 - x0) - (x1 - x0) * (z2 - z0)  # glTF +Y component of the face normal
            idx += list(tri) if up > 0 else [tri[0], tri[2], tri[1]]
    _write_gltf_mesh(path, name, pos, nrm, uv, idx)
    LOG("PA250: river ribbon %d verts -> %s" % (len(pos) // 3, path))


def _import_gltf_mesh(src, dest, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = None
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(dest, recursive=True):
        if str(data.asset_class_path.asset_name) == "StaticMesh" and str(data.asset_name) == name:
            mesh = unreal.EditorAssetLibrary.load_asset(str(data.package_name))
            break
    if not mesh:
        raise RuntimeError("import of %s produced no StaticMesh" % name)
    # Interchange enables Nanite on import. The meadow material has no Nanite usage, so the
    # game drew - and walked on - the Nanite FALLBACK mesh: 498 triangles for the whole
    # field, up to 30 cm off ground_z(), which every wall, building and post is placed with.
    # Plain static mesh: LOD0 is the full mesh.
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", False)
    mesh.set_editor_property("nanite_settings", nanite)
    return mesh


def import_terrain_mesh():
    """Generate + import the rolling ground; complex collision so nav and pawns follow it."""
    dest = TERRAIN_DEST
    src = os.path.join(PROJECT, "Saved", "PA250", "gen", TERRAIN_NAME + ".gltf")
    write_terrain_gltf(src)
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(dest, recursive=True):
        unreal.EditorAssetLibrary.delete_asset(str(data.package_name))
    mesh = _import_gltf_mesh(src, dest, TERRAIN_NAME)
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    mesh.set_editor_property("body_setup", body)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    want = 2 * (int((TERRAIN_X[1] - TERRAIN_X[0]) / TERRAIN_CELL)) * (int((TERRAIN_Y[1] - TERRAIN_Y[0]) / TERRAIN_CELL))
    LOG("PA250: terrain LOD0 triangles %d (heightfield %d)" % (mesh.get_num_triangles(0), want))
    if mesh.get_num_triangles(0) < want * 0.95:
        raise RuntimeError("terrain is simplified: %d of %d triangles" % (mesh.get_num_triangles(0), want))
    b = mesh.get_bounds()
    LOG(
        "PA250: terrain bounds origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"
        % (b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z)
    )
    return mesh


def import_river_mesh():
    name = TERRAIN_NAME.replace("Terrain", "River")
    src = os.path.join(PROJECT, "Saved", "PA250", "gen", name + ".gltf")
    write_river_gltf(src, name)
    mesh = _import_gltf_mesh(src, TERRAIN_DEST, name)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return mesh


# ---------------------------------------------------------------- horizon
# A wood edge in photographs (tools/art/build_battlefield_backdrop.py -> T_HorizonTreeline)
# on two rings round the field, so the ground ends in trees as it did in 1775 Middlesex and
# not in a bare skyline. GAME INVENTION as to which wood stood where; the woodlot trees and
# the fog stand in front of it.
HORIZON_INSET = 1800.0  # the near ring stands this far inside the terrain edge
HORIZON_RINGS = (
    # (offset outward from the near ring, height, U offset, texture repeats round the field)
    (0.0, 2900.0, 0.0, 4),
    (7500.0, 4300.0, 0.37, 3),
)


def _horizon_loop(grow, corner=9000.0, step=1200.0):
    """A closed rounded rectangle `grow` cm outside the near ring: [(x, y)]."""
    x0, x1 = TERRAIN_X[0] + HORIZON_INSET - grow, TERRAIN_X[1] - HORIZON_INSET + grow
    y0, y1 = TERRAIN_Y[0] + HORIZON_INSET - grow, TERRAIN_Y[1] - HORIZON_INSET + grow
    r = corner + grow
    pts = []

    def line(ax, ay, bx, by):
        n = max(1, int(math.hypot(bx - ax, by - ay) / step))
        pts.extend((ax + (bx - ax) * i / n, ay + (by - ay) * i / n) for i in range(n))

    def arc(cx, cy, a0):
        n = max(2, int(r * math.pi / 2 / step))
        pts.extend((cx + r * math.cos(math.radians(a0 + 90.0 * i / n)),
                    cy + r * math.sin(math.radians(a0 + 90.0 * i / n))) for i in range(n))

    line(x0 + r, y0, x1 - r, y0)
    arc(x1 - r, y0 + r, -90)
    line(x1, y0 + r, x1, y1 - r)
    arc(x1 - r, y1 - r, 0)
    line(x1 - r, y1, x0 + r, y1)
    arc(x0 + r, y1 - r, 90)
    line(x0, y1 - r, x0, y0 + r)
    arc(x0 + r, y0 + r, 180)
    return pts


def write_horizon_gltf(path, name):
    """Both rings as one mesh. Each follows the ground under it; U runs round the field
    (whole repeats, so the strip closes), V = 0 at the crowns and 1 at the foot."""
    pos, nrm, uv, idx = [], [], [], []
    for grow, height, u_off, repeats in HORIZON_RINGS:
        loop = _horizon_loop(grow)
        loop.append(loop[0])
        acc = [0.0]
        for (ax, ay), (bx, by) in zip(loop, loop[1:]):
            acc.append(acc[-1] + math.hypot(bx - ax, by - ay))
        base = len(pos) // 3
        for (x, y), d in zip(loop, acc):
            # The foot sits on the ground of the near ring even for the far one: beyond the
            # terrain mesh there is no ground to follow.
            cx = min(max(x, TERRAIN_X[0] + HORIZON_INSET), TERRAIN_X[1] - HORIZON_INSET)
            cy = min(max(y, TERRAIN_Y[0] + HORIZON_INSET), TERRAIN_Y[1] - HORIZON_INSET)
            z = ground_z(cx, cy) - 120.0
            u = u_off + repeats * d / acc[-1]
            for zz, v in ((z, 1.0), (z + height, 0.0)):
                pos += [x / 100.0, zz / 100.0, y / 100.0]  # UE (x, y, z) cm -> glTF (x, z, y) m
                nrm += [0.0, 1.0, 0.0]
                uv += [u, v]
        for i in range(len(loop) - 1):
            a = base + 2 * i
            idx += [a, a + 2, a + 1, a + 1, a + 2, a + 3]
    _write_gltf_mesh(path, name, pos, nrm, uv, idx)
    LOG("PA250: horizon rings %d verts -> %s" % (len(pos) // 3, path))


def make_horizon_material():
    """Masked, two-sided, lit as level ground (world-up normal): the photographed wood takes
    the level's own sun and sky, so one strip serves dawn at Lexington and mid-morning at
    Concord. None when the strip has not been imported."""
    tex_path = KIT + "/Textures/T_HorizonTreeline"
    if not unreal.EditorAssetLibrary.does_asset_exist(tex_path):
        return None
    name = "M_PA250_Horizon"
    path = "/Game/PA250/Materials/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = tools().create_asset(name, "/Game/PA250/Materials", unreal.Material, unreal.MaterialFactoryNew())
    M = unreal
    set_props(mat, (
        ("blend_mode", M.BlendMode.BLEND_MASKED),
        ("two_sided", True),
        ("opacity_mask_clip_value", 0.35),
        ("dither_opacity_mask", True),
        ("tangent_space_normal", False),
    ), name)
    tex = _expr(mat, M.MaterialExpressionTextureSample, -700, 0, texture=unreal.EditorAssetLibrary.load_asset(tex_path))
    # A wood is darker than the pasture before it; the photographs were taken in flat light.
    tint = _expr(mat, M.MaterialExpressionVectorParameter, -700, 260, parameter_name="Tint",
                 default_value=M.LinearColor(0.8, 0.8, 0.8, 1.0))
    mul = _expr(mat, M.MaterialExpressionMultiply, -400, 0)
    _link(tex, "RGB", mul, "A")
    _link(tint, "", mul, "B")
    M.MaterialEditingLibrary.connect_material_property(mul, "", M.MaterialProperty.MP_BASE_COLOR)
    M.MaterialEditingLibrary.connect_material_property(tex, "A", M.MaterialProperty.MP_OPACITY_MASK)
    up = _expr(mat, M.MaterialExpressionConstant3Vector, -400, 300, constant=M.LinearColor(0.0, 0.0, 1.0, 1.0))
    M.MaterialEditingLibrary.connect_material_property(up, "", M.MaterialProperty.MP_NORMAL)
    M.MaterialEditingLibrary.connect_material_property(
        _expr(mat, M.MaterialExpressionConstant, -400, 450, r=1.0), "", M.MaterialProperty.MP_ROUGHNESS)
    M.MaterialEditingLibrary.connect_material_property(
        _expr(mat, M.MaterialExpressionConstant, -400, 550, r=0.0), "", M.MaterialProperty.MP_SPECULAR)
    M.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def place_horizon():
    """The photographed treeline round the field: no collision, no shadow, not in Lumen."""
    mat = make_horizon_material()
    if not mat:
        LOG("PA250: horizon skipped (T_HorizonTreeline not imported)")
        _CHOSEN["horizon"] = "none"
        return
    name = TERRAIN_NAME.replace("Terrain", "Horizon")
    src = os.path.join(PROJECT, "Saved", "PA250", "gen", name + ".gltf")
    write_horizon_gltf(src, name)
    mesh = _import_gltf_mesh(src, TERRAIN_DEST, name)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    a = spawn(unreal.StaticMeshActor, (0, 0, 0), (0, 0, 0), "Horizon")
    c = a.static_mesh_component
    c.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_static_mesh(mesh)
    c.set_material(0, mat)
    c.set_collision_profile_name("NoCollision")
    c.set_cast_shadow(False)
    set_props(c, (
        ("affect_dynamic_indirect_lighting", False),
        ("affect_distance_field_lighting", False),
        ("can_ever_affect_navigation", False),
    ), "Horizon")
    tag(a, "PA250Horizon")
    _CHOSEN["horizon"] = "T_HorizonTreeline on %d rings" % len(HORIZON_RINGS)
    LOG("PA250: horizon placed (%d rings)" % len(HORIZON_RINGS))


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
    """Ground for the terrain mesh.

    With the battlefield textures imported (KIT): April pasture (two scales of the same
    photo texture so the 3 m tile never reads), trampled turf round the Common, posts and
    doorways, ploughed fields with furrow ridges, and packed-earth roads, each with its
    normal and roughness. Masks come from world position (_mask_hlsl), generated from the
    same ROADS / FIELDS / VILLAGE lists as the level. Without the kit it falls back to the
    KiteDemo grass and dirt, then to flat colours.
    """
    path = "/Game/PA250/Materials/" + MEADOW_NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = tools().create_asset(
        MEADOW_NAME, "/Game/PA250/Materials", unreal.Material, unreal.MaterialFactoryNew()
    )
    M = unreal
    lerp_cls = M.MaterialExpressionLinearInterpolate

    def noise(scale, levels, y):
        # Texture-based gradient noise: a few ALU + one fetch per level. The default
        # computational Simplex cost ~13 fps on the full-screen ground on this Mac.
        return _expr(
            mat, M.MaterialExpressionNoise, -900, y,
            scale=scale, levels=levels, output_min=0.0, output_max=1.0, quality=1,
            noise_function=M.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX,
        )

    def tex_xy(tex_path, tile_cm, y, kind="color"):
        """World-aligned (XY / tile) sample of a texture asset."""
        wp = _expr(mat, M.MaterialExpressionWorldPosition, -1400, y)
        xy = _expr(mat, M.MaterialExpressionComponentMask, -1250, y, r=True, g=True, b=False, a=False)
        _link(wp, "", xy, "")
        div = _expr(mat, M.MaterialExpressionDivide, -1100, y, const_b=float(tile_cm))
        _link(xy, "", div, "A")
        props = {"texture": unreal.EditorAssetLibrary.load_asset(tex_path)}
        if kind == "normal":
            props["sampler_type"] = M.MaterialSamplerType.SAMPLERTYPE_NORMAL
        elif kind == "mask":
            props["sampler_type"] = M.MaterialSamplerType.SAMPLERTYPE_MASKS
        elif kind == "linear":
            props["sampler_type"] = M.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        smp = _expr(mat, M.MaterialExpressionTextureSample, -900, y, **props)
        _link(div, "", smp, "UVs")
        return smp

    def rgb_of(node, y):
        out = _expr(mat, M.MaterialExpressionComponentMask, -450, y, r=True, g=True, b=True, a=False)
        _link(node, "", out, "")
        return out

    def chan(node, y, **which):
        flags = {"r": False, "g": False, "b": False, "a": False}
        flags.update(which)
        out = _expr(mat, M.MaterialExpressionComponentMask, -700, y, **flags)
        _link(node, "", out, "")
        return out

    def lerp(a, b, alpha, y, x=-100):
        out = _expr(mat, lerp_cls, x, y)
        _link(a, "", out, "A")
        _link(b, "", out, "B")
        _link(alpha, "", out, "Alpha")
        return out

    def const3(rgb, y):
        return _expr(mat, M.MaterialExpressionConstant3Vector, -600, y,
                     constant=M.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    macro_tex = resolve_asset("tex_macro", (), cls="Texture2D")
    if macro_tex:
        # Two scales of the pack's macro-variation map: cheaper than procedural noise
        # and breaks the grass tile completely.
        macro = chan(tex_xy(macro_tex, 9000.0, -300), -300, r=True)
        fine = chan(tex_xy(macro_tex, 700.0, 0), 0, g=True)
    else:
        macro = noise(1.0 / 6000.0, 2, -300)
        fine = noise(1.0 / 450.0, 2, 0)
    wp_abs = _expr(mat, M.MaterialExpressionWorldPosition, -1100, 300)
    wp_in = M.CustomInput()
    wp_in.set_editor_property("input_name", "WP")
    vc = _expr(
        mat, M.MaterialExpressionCustom, -900, 300,
        code=_mask_hlsl(), output_type=M.CustomMaterialOutputType.CMOT_FLOAT4,
        description="PA250 road, trampled, ploughed, furrow masks",
        inputs=[wp_in],
    )
    _link(wp_abs, "", vc, "WP")
    road_m = chan(vc, 330, r=True)
    tramp_m = chan(vc, 380, g=True)
    plough_m = chan(vc, 430, b=True)
    furrow = chan(vc, 480, a=True)
    fine_s = _expr(mat, lerp_cls, -600, 100, const_a=0.84, const_b=1.12)
    _link(fine, "", fine_s, "Alpha")
    # Mask edges eaten by the fine noise, so no road or field has a ruled edge.
    edge = _expr(mat, lerp_cls, -500, 250, const_a=0.6, const_b=1.5)
    _link(fine, "", edge, "Alpha")

    def eaten(mask, y):
        mul = _expr(mat, M.MaterialExpressionMultiply, -300, y)
        _link(mask, "", mul, "A")
        _link(edge, "", mul, "B")
        sat = _expr(mat, M.MaterialExpressionSaturate, -150, y)
        _link(mul, "", sat, "")
        return sat

    road_a = eaten(road_m, 300)
    plough_a = eaten(plough_m, 420)

    kit = all(kit_tex(n, sfx) for n in ("PastureSpring", "RoadDirt", "TrampledTurf", "FieldPloughed")
              for sfx in ("BaseColor", "Normal", "Roughness"))
    _CHOSEN["ground"] = "battlefield kit textures" if kit else "KiteDemo / flat colours"
    if kit:
        def layer(name, tile, y):
            return (rgb_of(tex_xy(kit_tex(name), tile, y), y),
                    tex_xy(kit_tex(name, "Normal"), tile, y + 60, "normal"),
                    chan(tex_xy(kit_tex(name, "Roughness"), tile, y + 120, "mask"), y + 120, r=True))

        # One 3.1 m tile; the macro and fine maps below vary it, so the repeat does not read.
        # (A second, 17 m sample of the same photo turned every straw into a metre-long stroke.)
        grass, g_n, g_r = layer("PastureSpring", 310.0, -1200)
        worn, w_n, w_r = layer("TrampledTurf", 340.0, -800)
        soil, s_n, s_r = layer("FieldPloughed", 290.0, -500)
        dirt, d_n, d_r = layer("RoadDirt", 330.0, 700)
        # macro: a third of the way toward last year's bleached thatch, in 60 m drifts
        dry = _expr(mat, M.MaterialExpressionMultiply, -500, -1300)
        _link(grass, "", dry, "A")
        _link(const3((1.1, 1.02, 0.76), -1350), "", dry, "B")
        macro_a = _expr(mat, M.MaterialExpressionMultiply, -700, -250, const_b=0.45)
        _link(macro, "", macro_a, "A")
        field = lerp(grass, dry, macro_a, -400, -400)
        field2 = _expr(mat, M.MaterialExpressionMultiply, -250, -300)
        _link(field, "", field2, "A")
        _link(fine_s, "", field2, "B")
        macro_photo = KIT + "/Textures/T_PastureMacro"
        if unreal.EditorAssetLibrary.does_asset_exist(macro_photo):
            # A pasture photographed from the air (55 m across, stored as a ratio to its own
            # mean, 0.5 = no change): the greener and drier drifts of real ground, which the
            # 3 m tile and the noise cannot draw.
            drift = rgb_of(tex_xy(macro_photo, 5500.0, -1500, "linear"), -1500)
            # 0.5 -> x0.86 (grass is darker than this tile was toned), and 60 % of the swing
            drift2 = _expr(mat, lerp_cls, -300, -1500, const_a=0.34, const_b=1.38)
            _link(drift, "", drift2, "Alpha")
            field3 = _expr(mat, M.MaterialExpressionMultiply, -150, -1400)
            _link(field2, "", field3, "A")
            _link(drift2, "", field3, "B")
            field2 = field3
            _CHOSEN["ground_drifts"] = "T_PastureMacro"
        col = lerp(field2, worn, tramp_m, -200)
        # furrows: ridges catch the light, hollows stay dark
        fur = _expr(mat, lerp_cls, -500, -520, const_a=0.8, const_b=1.12)
        fur_01 = _expr(mat, M.MaterialExpressionConstantBiasScale, -650, -520, bias=1.0, scale=0.5)
        _link(furrow, "", fur_01, "")
        _link(fur_01, "", fur, "Alpha")
        soil_f = _expr(mat, M.MaterialExpressionMultiply, -350, -500)
        _link(soil, "", soil_f, "A")
        _link(fur, "", soil_f, "B")
        col = lerp(col, soil_f, plough_a, -100, 0)
        dirt_s = _expr(mat, M.MaterialExpressionMultiply, -250, 700)
        _link(dirt, "", dirt_s, "A")
        _link(fine_s, "", dirt_s, "B")
        col = lerp(col, dirt_s, road_a, 0, 100)
        M.MaterialEditingLibrary.connect_material_property(col, "", M.MaterialProperty.MP_BASE_COLOR)
        nrm = lerp(lerp(lerp(g_n, w_n, tramp_m, 900), s_n, plough_a, 960, 0), d_n, road_a, 1020, 100)
        M.MaterialEditingLibrary.connect_material_property(nrm, "", M.MaterialProperty.MP_NORMAL)
        rgh = lerp(lerp(lerp(g_r, w_r, tramp_m, 1100), s_r, plough_a, 1160, 0), d_r, road_a, 1220, 100)
        M.MaterialEditingLibrary.connect_material_property(rgh, "", M.MaterialProperty.MP_ROUGHNESS)
        spec = _expr(mat, M.MaterialExpressionConstant, 100, 1350, r=0.25)  # matt ground, no sheen at dawn
        M.MaterialEditingLibrary.connect_material_property(spec, "", M.MaterialProperty.MP_SPECULAR)
        M.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        return mat

    def colour(rgb, role, keywords, y, tile_cm):
        tex = resolve_asset(role, (), keywords, cls="Texture2D")
        if not tex:
            return const3(rgb, y)
        return rgb_of(tex_xy(tex, tile_cm, y), y)

    grass = colour((0.20, 0.33, 0.11), "tex_grass", ("grass",), -600, 350)
    dry = const3((0.36, 0.36, 0.16), -450)
    worn = const3((0.30, 0.27, 0.15), 450)
    dirt = colour((0.30, 0.21, 0.12), "tex_dirt", ("dirt", "mud", "soil"), 600, 450)
    macro_a = _expr(mat, M.MaterialExpressionMultiply, -700, -250, const_b=0.55)
    _link(macro, "", macro_a, "A")
    field = lerp(grass, dry, macro_a, -400, -400)
    field2 = _expr(mat, M.MaterialExpressionMultiply, -250, -300)
    _link(field, "", field2, "A")
    _link(fine_s, "", field2, "B")
    tramp = lerp(field2, worn, tramp_m, -200)
    tramp = lerp(tramp, const3((0.17, 0.12, 0.08), 520), plough_a, -100, 0)
    dirt_s = _expr(mat, M.MaterialExpressionMultiply, -250, 500)
    _link(dirt, "", dirt_s, "A")
    _link(fine_s, "", dirt_s, "B")
    final = lerp(tramp, dirt_s, road_a, 0, 100)
    M.MaterialEditingLibrary.connect_material_property(final, "", M.MaterialProperty.MP_BASE_COLOR)
    grass_n = resolve_asset("tex_grass_n", (), cls="Texture2D")
    dirt_n = resolve_asset("tex_dirt_n", (), cls="Texture2D")
    if grass_n and dirt_n:
        n_lerp = lerp(tex_xy(grass_n, 350.0, 700, "normal"), tex_xy(dirt_n, 450.0, 850, "normal"), road_a, 600, 100)
        M.MaterialEditingLibrary.connect_material_property(n_lerp, "", M.MaterialProperty.MP_NORMAL)
    rough = _expr(mat, lerp_cls, 100, 250, const_a=0.95, const_b=0.85)
    _link(road_a, "", rough, "Alpha")
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
    """True if (x, y) is off the roads, building pads, capture posts, the river and the bridge."""
    for p0, p1, w in ROADS:
        if _seg_dist(x, y, p0, p1) < w * 0.6 + margin:
            return False
    for (bx, by), (w, d, _h) in (b for b in (MEETINGHOUSE, TAVERN) if b):
        if math.hypot(x - bx, y - by) < 0.5 * math.hypot(w, d) + 150 + margin:
            return False
    for post in POSTS:
        if math.hypot(x - post[2][0], y - post[2][1]) < 450 + margin:
            return False
    for _label, _asset, (vx, vy), _facing, r in VILLAGE:
        if math.hypot(x - vx, y - vy) < r + 120 + margin:
            return False
    if RIVER and river_dist(x, y) < RIVER_HALF + 700 + margin:
        return False
    return True


def place_kit(label, asset, xy, facing, sink=0.0, collide=True, z=None):
    """One kit mesh at native size, base on the ground, front toward `facing` (Unreal yaw of
    the front's direction; kit fronts are +Y after import). Collision = the mesh's UCX hulls."""
    path = kit_mesh(asset)
    if not path:
        LOG("PA250: kit asset %s missing, %s not placed" % (asset, label))
        return None
    x, y = xy
    a = spawn(unreal.StaticMeshActor, (x, y, (ground_z(x, y) if z is None else z) - sink), (0, facing - 90.0, 0), label)
    comp = a.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    comp.set_static_mesh(unreal.EditorAssetLibrary.load_asset(path))
    comp.set_collision_profile_name("BlockAll" if collide else "NoCollision")
    _CHOSEN[label] = path
    return a


def audit_village():
    """Every village site must be clear of roads, walls, fences, trees, posts, spawns and
    muster points, or bots and cover audits change. Raises with the list of conflicts."""
    problems = []
    lines = all_wall_lines()
    spots = [xy for _l, xy in MUSTER_POINTS] + [p[2] for p in POSTS]
    spots += spawn_points("continental") + spawn_points("british")
    for sx, sy in spots:
        if RIVER and river_dist(sx, sy) < RIVER_HALF + 600 and not BRIDGE:
            problems.append("a post or spawn at (%d, %d) is in the river" % (sx, sy))
        for name, a, b in lines:
            if _seg_dist(sx, sy, a, b) < 160:
                problems.append("the post/spawn at (%d, %d) is on %s" % (sx, sy, name))
    for name, a, b in lines:
        for k in range(21):
            wx, wy = a[0] + (b[0] - a[0]) * k / 20.0, a[1] + (b[1] - a[1]) * k / 20.0
            if RIVER and river_dist(wx, wy) < RIVER_HALF + 500:
                problems.append("%s runs into the river" % name)
                break
            if any(_seg_dist(wx, wy, p0, p1) < w * 0.5 + 60 for p0, p1, w in ROADS):
                problems.append("%s crosses a road at (%d, %d)" % (name, wx, wy))
                break
    for label, _asset, (x, y), _facing, r in VILLAGE:
        for p0, p1, w in ROADS:
            if _seg_dist(x, y, p0, p1) < w * 0.6 + r * 0.6:
                problems.append("%s is on a road" % label)
                break
        for name, a, b in lines:
            if _seg_dist(x, y, a, b) < r + 100:
                problems.append("%s is within %d cm of %s" % (label, r + 100, name))
        for tx, ty in MEADOW_TREES + PATRIOT_TREES:
            if math.hypot(x - tx, y - ty) < r + 200:
                problems.append("%s is under the tree at (%d, %d)" % (label, tx, ty))
        for sx, sy in spots:
            if math.hypot(x - sx, y - sy) < r + 250:
                problems.append("%s crowds the post/spawn at (%d, %d)" % (label, sx, sy))
        if in_field(x, y, r):
            problems.append("%s stands in a ploughed field" % label)
        if RIVER and river_dist(x, y) < RIVER_HALF + 700 + r:
            problems.append("%s stands in the river" % label)
        for other, _a2, (ox, oy), _f2, r2 in VILLAGE:
            if other < label and math.hypot(x - ox, y - oy) < (r + r2) * 0.8:
                problems.append("%s crowds %s" % (label, other))
        for (bx, by), _size in (b for b in (MEETINGHOUSE, TAVERN) if b):
            if math.hypot(x - bx, y - by) < r + 750:
                problems.append("%s crowds the meetinghouse or tavern" % label)
    if problems:
        raise RuntimeError("Village sites are not clear:\n  " + "\n  ".join(problems))


def place_village():
    n = 0
    for label, asset, xy, facing, r in VILLAGE:
        # Buildings sit 6 cm into the ground so the underpinning meets uneven turf.
        if place_kit(label, asset, xy, facing, sink=6.0 if r >= 400 else 2.0):
            n += 1
    LOG("PA250: village %d of %d kit assets placed" % (n, len(VILLAGE)))


def place_woodlots(rng):
    """Distant woodlots and a few hedgerow trees (instanced, no collision): beyond every
    field wall of 1775 Middlesex there was a woodlot on the skyline."""
    trees = kite_all("tree")
    if not trees:
        return
    n = 0
    walls = all_wall_lines()
    for (cx, cy), radius, count in WOODLOTS:
        for _ in range(count):
            for _try in range(12):
                a, r = rng.uniform(0, 2 * math.pi), radius * math.sqrt(rng.random())
                x, y = cx + math.cos(a) * r, cy + math.sin(a) * r
                if (TERRAIN_X[0] + 500 < x < TERRAIN_X[1] - 500 and TERRAIN_Y[0] + 500 < y < TERRAIN_Y[1] - 500
                        and keep_clear(x, y, 400) and not in_field(x, y, 200)
                        and not any(_seg_dist(x, y, p0, p1) < 400 for _n, p0, p1 in walls)):
                    break
            else:
                continue
            m = rng.choice(trees)
            _big, e = _mesh_extent(m)
            s = rng.uniform(1300, 2100) / (2.0 * e.z)
            # No shadows or Lumen cards: they stand 100 m and more from the fight, and 189
            # shadow-casting trees cost about 3 ms of GPU on the M3.
            scatter("woodlot_" + m.rsplit("/", 1)[-1], m, (x, y, ground_z(x, y) - 25),
                    (0, rng.uniform(0, 360), 0), s, collide=False, shadows=False, gi=False)
            n += 1
    LOG("PA250: woodlots %d trees" % n)


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
    panel, end = kit_mesh("PostRailFence"), kit_mesh("PostRailFenceEnd")
    if not mesh and panel and end:
        # Kit post-and-rail panels (3 m, post at the start; the last carries the closing post),
        # scaled a few percent to fill the run exactly and pitched to follow the ground.
        _CHOSEN["fence"] = panel
        n = max(1, int(round(length / 300.0)))
        seg = length / n
        for i in range(n):
            a = (p0[0] + (p1[0] - p0[0]) * i / n, p0[1] + (p1[1] - p0[1]) * i / n)
            b = (p0[0] + (p1[0] - p0[0]) * (i + 1) / n, p0[1] + (p1[1] - p0[1]) * (i + 1) / n)
            za, zb = ground_z(*a), ground_z(*b)
            pitch = math.degrees(math.atan2(zb - za, seg))
            scatter("fence_end" if i == n - 1 else "fence_panel", end if i == n - 1 else panel,
                    ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (za + zb) / 2), (pitch, yaw, 0), seg / 300.0,
                    collide=True, gi=False)
        return None
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


def place_stone_wall(label, p0, p1, stone_mat, rng, group="wall_", extra_tag=None):
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
    kit_walls = [] if mesh else [m for m in (kit_mesh("StoneWall_" + v) for v in "ABC") if m]
    if kit_walls:
        # Kit fieldstone wall: 3 m pieces laid every 2.7 m so their ragged ends interlock,
        # turned end for end at random and pitched to the ground. Collision is the same
        # invisible 1.1 m box run as before, so cover and pathing are unchanged.
        _CHOSEN["stone_wall"] = "battlefield kit: " + ", ".join(m.rsplit("/", 1)[-1] for m in kit_walls)
        pieces = max(1, int(round(length / 270.0)))
        step = length / pieces
        for i in range(pieces):
            t0, t1 = i / float(pieces), (i + 1) / float(pieces)
            a = (p0[0] + (p1[0] - p0[0]) * t0, p0[1] + (p1[1] - p0[1]) * t0)
            b = (p0[0] + (p1[0] - p0[0]) * t1, p0[1] + (p1[1] - p0[1]) * t1)
            za, zb = ground_z(*a), ground_z(*b)
            flip = rng.random() < 0.5
            pitch = math.degrees(math.atan2(zb - za, step)) * (-1 if flip else 1)
            m = rng.choice(kit_walls)
            scatter(group + m.rsplit("/", 1)[-1], m, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (za + zb) / 2 - 6),
                    (pitch, yaw + (180 if flip else 0) + rng.uniform(-3, 3), 0), rng.uniform(0.96, 1.06),
                    collide=False, gi=False)
        for i in range(n):
            t = (i + 0.5) / n
            x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
            blk = block("%s_Block_%d" % (label, i), (x, y, ground_z(x, y) + 50),
                        (length / n + 25, 80, 120), stone_mat, rot_yaw=yaw)
            blk.set_actor_hidden_in_game(True)
            blk.static_mesh_component.set_visibility(False)
            tag(blk, "PA250Cover")  # bots use tagged walls as firing positions
            if extra_tag:
                tag(blk, extra_tag)
        return
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
            if extra_tag:
                tag(blk, extra_tag)
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
        if extra_tag:
            tag(a, extra_tag)


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
        # PA250BattleRoad marks the walls flanking the column's approach (PA250BattleCases).
        place_stone_wall(label, p0, p1, stone, rng, group="brwall_", extra_tag="PA250BattleRoad")
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

    use_layout("lexington")
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
            if not keep_clear(x, y, 60) or in_field(x, y, 80):
                continue
            big, _e = _mesh_extent(grass[0])
            scatter("grass", grass[0], (x, y, ground_z(x, y) - 3), (0, rng.uniform(0, 360), 0),
                    rng.uniform(70, 120) / (2.0 * big), cull=(2200, 3500), shadows=False, gi=False)
        if flowers and rng.random() < 0.45:
            f = rng.choice(flowers)
            for _ in range(rng.randint(3, 7)):
                x, y = cx + rng.gauss(0, 200), cy + rng.gauss(0, 200)
                if keep_clear(x, y, 60) and not in_field(x, y, 80):
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


def men_only(blk):
    """An unseen wall tagged PA250Creek. Saved blocking everything; the game narrows every actor
    with this tag to men only when the level starts (APA250BattleGameMode::SealLevelRiver)."""
    blk.set_actor_hidden_in_game(True)
    comp = blk.static_mesh_component
    comp.set_visibility(False)
    comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    tag(blk, "PA250Creek")
    return blk


def place_river(stone):
    """The river of a layout that has one: its surface (one ribbon mesh, kit water material), the
    bridge with its own collision, and an unseen wall down the channel that stops men - and only
    men: shot, sight lines and cameras pass - so the bridge is the one crossing."""
    water = spawn(unreal.StaticMeshActor, (0, 0, 0), (0, 0, 0), "River")
    wc = water.static_mesh_component
    wc.set_mobility(unreal.ComponentMobility.STATIC)
    wc.set_static_mesh(import_river_mesh())
    mat = unreal.EditorAssetLibrary.load_asset(KIT + "/Materials/M_Water") if unreal.EditorAssetLibrary.does_asset_exist(KIT + "/Materials/M_Water") else None
    if mat:
        wc.set_material(0, mat)
    wc.set_collision_profile_name("NoCollision")
    wc.set_cast_shadow(False)
    tag(water, "PA250CreekVisual")
    bridge = BRIDGE
    if bridge:
        a = place_kit("NorthBridge", bridge["asset"], bridge["xy"], bridge["yaw"] + 90.0, z=bridge["z"])
        if a is None:
            raise RuntimeError("the level needs kit asset %s (run Scripts/import_battlefield_kit.py)" % bridge["asset"])
        # The game mode lays its own plank crossing only where a level has none.
        tag(a, "PA250LevelCrossing")
    n = 0
    for i, ((ax, ay), (bx, by)) in enumerate(zip(RIVER, RIVER[1:])):
        mx, my = (ax + bx) / 2, (ay + by) / 2
        if bridge:
            c, s_ = math.cos(math.radians(bridge["yaw"])), math.sin(math.radians(bridge["yaw"]))
            u = (mx - bridge["xy"][0]) * c + (my - bridge["xy"][1]) * s_
            v = -(mx - bridge["xy"][0]) * s_ + (my - bridge["xy"][1]) * c
            ln = math.hypot(bx - ax, by - ay)
            if abs(u) < bridge["half_len"] and abs(v) < bridge["half_wid"] + ln * 0.5 + 40:
                continue  # under the span: the deck and its rails are the barrier here
        # 24 m tall: the navmesh that forms on a block's top is then far above any point the game
        # projects onto the navmesh (at 3 m, post slots and spawns landed men on top, over the water).
        men_only(block("River_Block_%d" % i, (mx, my, RIVER_BED_Z + 1200), (math.hypot(bx - ax, by - ay) + 60, 2 * RIVER_HALF - 500, 2400),
                       stone, rot_yaw=math.degrees(math.atan2(by - ay, bx - ax))))
        n += 1
    if bridge:
        # The bridge's railings: unseen walls along both sides of the deck, for men only, so a
        # ball fired over or between the rails flies on (a solid rail hull stopped every shot).
        c, s_ = math.cos(math.radians(bridge["yaw"])), math.sin(math.radians(bridge["yaw"]))
        for k, side in enumerate((-1.0, 1.0)):
            off = side * (bridge["rail_y"] + 12.0)
            men_only(block("Bridge_Rail_%d" % k, (bridge["xy"][0] - s_ * off, bridge["xy"][1] + c * off, bridge["z"] + 160),
                           (2 * bridge["half_len"], 24, 420), stone, rot_yaw=bridge["yaw"]))
    LOG("PA250: river %d centreline points, %d channel blocks, bridge %s" % (len(RIVER), n, bridge["asset"] if bridge else "none"))


def build_battlefield(layout):
    """One battle level from its layout (see _lexington_layout / _concord_layout).
    Units are cm; +X east, +Y south."""
    import random

    global STONE_MAT, WOOD_DARK_MAT, BRICK_MAT, GLASS_MAT
    use_layout(layout)
    audit_village()
    audit_patriot_cover()
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

    les = new_level(MAPS + "/" + MAP_NAME)
    set_game_mode("PA250BattleGameMode")

    # Rolling ground; worn roads come from the meadow material masks.
    g = spawn(unreal.StaticMeshActor, (0, 0, 0), (0, 0, 0), "Ground")
    gc = g.static_mesh_component
    gc.set_mobility(unreal.ComponentMobility.STATIC)
    gc.set_static_mesh(terrain)
    gc.set_material(0, meadow)

    if MEETINGHOUSE and TAVERN:
        (mx, my), msize = MEETINGHOUSE
        (tx, ty), tsize = TAVERN
        if kit_mesh("Meetinghouse1714") and kit_mesh("BuckmanTavern"):
            # The 1714 meetinghouse had no steeple; its bell hung in the Belfry on the Common
            # (placed with the village).
            place_kit("Meetinghouse", "Meetinghouse1714", (mx, my), MEETINGHOUSE_FACING, sink=6.0)
            place_kit("BuckmanTavern", "BuckmanTavern", (tx, ty), TAVERN_FACING, sink=6.0)
        else:
            building_shell(
                "Meetinghouse", (mx, my), msize, white, roof, white, GLASS_MAT, "meetinghouse",
                ("/Game/ThirdParty/Buildings/SM_Meetinghouse", "/Game/ThirdParty/Buildings/SM_Church"),
                ("meetinghouse", "church", "chapel"),
            )
            building_shell(
                "BuckmanTavern", (tx, ty), tsize, ochre, roof, white, GLASS_MAT, "tavern",
                ("/Game/ThirdParty/Buildings/SM_Tavern", "/Game/ThirdParty/Buildings/SM_ColonialHouse"),
                ("tavern", "colonial", "house", "cottage", "farmhouse"), chimneys=2,
            )
    place_village()
    if RIVER:
        place_river(stone)

    for label, p0, p1 in ROAD_WALLS:
        place_stone_wall(label, p0, p1, stone, rng)
    place_battle_road(stone, rng)
    place_patriot_cover(stone, wood, canopy, rng)

    for label, p0, p1 in COMMON_FENCES:
        place_split_rail(label, p0, p1, wood)
    for i, xy in enumerate(MEADOW_TREES):
        place_tree("Tree_%d" % i, xy, wood, canopy, rng)

    for side in ("continental", "british"):
        yaw = SPAWNS[side][1]
        for i, (x, y) in enumerate(spawn_points(side)):
            tag(spawn(unreal.PlayerStart, (x, y, ground_z(x, y) + 120), (0, yaw, 0),
                      "Start_%s_%d" % (side.capitalize(), i)), side)

    for label, post_id, xy, initial_owner, z in POSTS:
        a = spawn(unreal.PA250ControlPoint, (xy[0], xy[1], ground_z(*xy) + 140 if z is None else z), (0, 0, 0), label)
        a.set_editor_property("post_id", unreal.Name(post_id))
        if initial_owner:
            a.set_editor_property("initial_owner", unreal.Name(initial_owner))

    (nx, ny), nscale = NAV
    spawn(unreal.NavMeshBoundsVolume, (nx, ny, 0), (0, 0, 0), "NavBounds", scale=nscale)

    light = LIGHT
    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), light["sun_rot"], "Sun")
    sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    set_props(sun_comp, (
        ("mobility", unreal.ComponentMobility.MOVABLE),
        ("intensity", light["sun_lux"]),
        ("light_color", unreal.Color(light["sun_color"][0], light["sun_color"][1], light["sun_color"][2], 255)),
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

    # Thin ground haze; volumetric so the sun shafts through it like powder smoke.
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, 0), (0, 0, 0), "Fog")
    set_props(fog.get_component_by_class(unreal.ExponentialHeightFogComponent), (
        ("fog_density", light["fog_density"]),
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

    pp = spawn(unreal.PostProcessVolume, (0, 0, 200), (0, 0, 0), light["pp"])
    pp.set_editor_property("unbound", True)
    pp.set_editor_property("priority", 1.0)
    pp.set_editor_property("blend_weight", 1.0)
    st = pp.get_editor_property("settings")
    for prop, val in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("auto_exposure_apply_physical_camera_exposure", False),
        ("auto_exposure_bias", light["exposure"]),
        ("bloom_intensity", 0.4),
        ("vignette_intensity", 0.3),
        # Photographic grade: a little warmth, near-full saturation. (The earlier
        # period-painting grade - desaturated, gain 1.16/1.0/0.76 - turned the sky mauve and
        # the photo-textured buildings to mud.) At sunrise the sky lights far more of the
        # scene than the sun, so an ungraded frame is blue; the gain is the camera's white
        # balance for that light.
        ("white_temp", light["white_temp"]),  # above 6500 = warmer: balances for the blue sky light
        ("color_saturation", unreal.Vector4(0.95, 0.95, 0.93, 1.0)),
        ("color_contrast", unreal.Vector4(1.0, 1.0, 1.0, 1.0)),
        ("color_gain", unreal.Vector4(light["gain"][0], light["gain"][1], light["gain"][2], 1.0)),
    ):
        try:
            st.set_editor_property("override_" + prop, True)
            st.set_editor_property(prop, val)
        except Exception as exc:  # noqa: BLE001
            LOG("PA250: %s.%s skipped (%s)" % (light["pp"], prop, exc))
    pp.set_editor_property("settings", st)

    dress_meadow(rng)
    place_woodlots(rng)
    flush_scatter()
    place_horizon()

    # Fixed review cameras (also used by -PA250Preview=<label> captures).
    for tag_name, (cx, cy), up, (pitch, yaw), fov in CAMERAS:
        shot_camera(tag_name, (cx, cy, ground_z(cx, cy) + up), (pitch, yaw, 0), fov)

    write_asset_choices()
    save(les)
    LOG("PA250: built " + MAP_NAME)


def build_lexington():
    """Lexington Common at sunrise, 19 April 1775 (lexington_common and battle_road fights)."""
    build_battlefield("lexington")


def build_concord():
    """The North Bridge, Concord, mid-morning 19 April 1775 (the concord_bridge fight)."""
    build_battlefield("concord")


def make_portrait_assets():
    """The envoy's figure texture + MediaPlayer/MediaTexture, and his materials.

    The envoy is a living man seated across the table, not a picture: SourceArt/envoy/gage_live.png is the
    Copley portrait of Gage made photoreal with an alpha matte (tools/art/build_envoy_live.py).
    """
    still = import_envoy_texture("gage_live.png", "T_EnvoyLive")
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

    # Live speech deforms the figure's own pixels (M_PortraitWarp, masked by his alpha). M_PortraitStill is
    # the same material at rest (no "Frame" parameter: the old painted flipbook atlas is retired, so
    # pa250.LipSync 0 shows him still). M_PortraitVideo keeps the MediaTexture graph for rendered MP4s.
    video_mat = painting_material("M_PortraitVideo", mt)
    warp_mat = make_envoy_face(still)
    still_path = "/Game/PA250/Materials/M_PortraitStill"
    if unreal.EditorAssetLibrary.does_asset_exist(still_path):
        unreal.EditorAssetLibrary.delete_asset(still_path)
    if unreal.EditorAssetLibrary.duplicate_asset("/Game/PA250/Materials/M_PortraitWarp", still_path):
        unreal.EditorAssetLibrary.save_asset(still_path)
    else:
        LOG("PA250: could not duplicate M_PortraitWarp to M_PortraitStill")
    return warp_mat, video_mat


PARLOUR_DIR = "/Game/PA250/Materials/Parlour"
PARLOUR_TEX_SRC = os.path.abspath(os.path.join(PROJECT, "SourceArt", "parlour"))
ENVOY_TEX_SRC = os.path.abspath(os.path.join(PROJECT, "SourceArt", "envoy"))


def import_envoy_texture(filename, name):
    """RGBA figure texture (1024x1536, not power of two: no mips, never stream), clamped so warps never wrap."""
    tex = import_texture(os.path.join(ENVOY_TEX_SRC, filename), "/Game/PA250/Textures", name)
    if tex:
        for k, v in (("address_x", unreal.TextureAddress.TA_CLAMP), ("address_y", unreal.TextureAddress.TA_CLAMP),
                     ("never_stream", True), ("srgb", True)):
            tex.set_editor_property(k, v)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex
PORTRAIT_BRIGHTNESS = 0.35  # emissive share of the lit painting (face stays readable by candlelight)
PORTRAIT_CROP = 0.42  # width share of the 1.77-1.8:1 still/video shown: a 3:4 oil portrait
# Manual exposure with physical-camera exposure off: the bias is a compensation (+1 = 2x brighter), not EV100.
PARLOUR_EXPOSURE_COMP = 0.0


PORTRAIT_WARP_HLSL = r"""
// Painting UV -> pixels of the still (3:4 crop), then life and speech warps (backward mapping).
float W = ImageSize.x, H = ImageSize.y;
float2 P = float2((0.5 + (UV.x - 0.5) * ImageSize.z) * W, UV.y * H);
// breathing: a slow scale about the bottom centre
float bs = 1.0 + 0.004 * Breath;
P = float2(W * 0.5 + (P.x - W * 0.5) / bs, H + (P.y - H) / bs);
// head drift and nod: the head region moves as one, fading out at its edge
float2 hd = (P - Head.xy) / Head.zw;
float hw = saturate(1.0 - dot(hd, hd)); hw = hw * hw * (3.0 - 2.0 * hw);
P -= hw * float2(HeadX, HeadY + Nod * 7.0);
// eyes: a small look shift inside each eye opening
float2 el = (P - EyeL.xy) / EyeL.zw;
float2 er = (P - EyeR.xy) / EyeR.zw;
float ew = saturate(1.6 * (saturate(1.0 - dot(el, el)) + saturate(1.0 - dot(er, er))));
P -= ew * float2(EyeX * EyeL.z * 0.16, EyeY * EyeL.w * 0.22);
// mouth: spread (ee) / purse (oo) horizontally around the mouth centre
float hm = Mouth.z;
float2 d = P - Mouth.xy;
float mw = exp(-((d.x * d.x) / (hm * hm * 2.25) + (d.y * d.y) / (hm * hm * 0.8)));
float sx = 1.0 + 0.22 * LipsWide - 0.30 * LipsRound;
P.x = Mouth.x + d.x / lerp(1.0, sx, mw);
d = P - Mouth.xy;
// pressed (m, b, p, f, v): lips thinner around the seam
float pw = exp(-((d.x * d.x) / (hm * hm) + (d.y * d.y) / (Lips.y * Lips.y * 4.0)));
P.y = Mouth.y + d.y * (1.0 + 0.35 * LipsPressed * pw);
d = P - Mouth.xy;
// jaw: the lower lip and chin drop; where they leave a gap, the mouth is open
float open = saturate(JawOpen + 0.25 * LipsRound) * (1.0 - 0.9 * LipsPressed);
float drop = open * Mouth.w;
float ho = hm * 0.88;                                         // the opening stops short of the corners
float fx = sqrt(saturate(1.0 - (d.x * d.x) / (ho * ho)));     // oval opening
float jx = saturate(1.0 - (d.x * d.x) / (Jaw.z * Jaw.z));
float chinD = max(Jaw.y - Mouth.y, 1.0);
float jy = saturate(1.0 - saturate((d.y - chinD) / (chinD * 0.6)));
float cav = 0.0;
float3 cavCol = float3(0.0, 0.0, 0.0);
if (d.y > 0.0)
{
    float nearSeam = saturate(d.y / (Lips.y * 2.0));
    float s = drop * jx * jy * lerp(fx, 1.0, nearSeam);
    cav = saturate((s - d.y) / 1.5) * step(abs(d.x), ho);
    float t = d.y / max(s, 1e-3);
    // upper teeth under the upper lip, then the mouth's dark interior (red at the edges, near black inside)
    float edge = saturate(abs(d.x) / ho);
    float teeth = saturate((JawOpen - 0.3) * 3.3) * (1.0 - smoothstep(0.10, 0.20, t)) * saturate(1.0 - edge * edge * 1.4);
    float3 inner = lerp(float3(0.03, 0.008, 0.006), float3(0.22, 0.06, 0.05), edge * edge);
    cavCol = lerp(inner, float3(0.50, 0.45, 0.39) * (0.8 + 0.2 * (1.0 - t * 5.0)), teeth);
    P.y -= s;
}
else
{
    P.y += 0.12 * drop * fx * saturate(1.0 + d.y / (Lips.x * 2.0));  // upper lip lifts a little
}
float2 Q = P / float2(W, H);
float4 st = Texture2DSample(Still, StillSampler, Q);
float3 c = lerp(st.rgb, Texture2DSample(BlinkTex, BlinkTexSampler, Q).rgb, saturate(BlinkAmt));
float inside = step(0.0, Q.x) * step(Q.x, 1.0) * step(0.0, Q.y) * step(Q.y, 1.0);
return float4(lerp(c, cavCol, cav), st.a * inside);   // alpha = the figure's matte at the warped position
"""

WARP_SCALARS = ("JawOpen", "LipsWide", "LipsRound", "LipsPressed", "BlinkAmt", "Breath", "HeadX", "HeadY", "Nod", "EyeX", "EyeY")
WARP_VECTORS = ("ImageSize", "Mouth", "Lips", "Jaw", "EyeL", "EyeR", "Head")


def warp_painting_material(still, blink, face):
    """M_PortraitWarp: the figure's own pixels deformed for speech, breathing, drift, eyes and blinks.

    The game drives the scalar parameters; the vector parameters come from the envoy's face data asset
    (defaults here = DA_EnvoyFace_british). Masked by the figure's alpha (dithered edge), lit by the room
    with a small emissive share. No canvas, varnish or vignette: he is a man in the room, not a painting.
    """
    name = "M_PortraitWarp"
    path = "/Game/PA250/Materials/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = tools().create_asset(name, "/Game/PA250/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("dither_opacity_mask", True)
    mel = unreal.MaterialEditingLibrary
    X = unreal

    def node(cls, x, y, **props):
        n = mel.create_material_expression(mat, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    custom = node(X.MaterialExpressionCustom, -700, 0, code=PORTRAIT_WARP_HLSL,
                  output_type=X.CustomMaterialOutputType.CMOT_FLOAT4, description="PortraitWarp")
    names = ["UV", "Still", "BlinkTex", *WARP_VECTORS, *WARP_SCALARS]
    inputs = []
    for n in names:
        ci = X.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    uv = node(X.MaterialExpressionTextureCoordinate, -1500, -400)
    mel.connect_material_expressions(uv, "", custom, "UV")
    t_still = node(X.MaterialExpressionTextureObjectParameter, -1500, -300, parameter_name="Still", texture=still)
    t_blink = node(X.MaterialExpressionTextureObjectParameter, -1500, -200, parameter_name="BlinkTex", texture=blink)
    mel.connect_material_expressions(t_still, "", custom, "Still")
    mel.connect_material_expressions(t_blink, "", custom, "BlinkTex")
    y = -100
    for vname in WARP_VECTORS:
        v = face[vname]
        p = node(X.MaterialExpressionVectorParameter, -1700, y, parameter_name=vname,
                 default_value=X.LinearColor(v[0], v[1], v[2], v[3]))
        rgba = node(X.MaterialExpressionAppendVector, -1500, y)  # the default pin is RGB only: add A -> float4
        mel.connect_material_expressions(p, "", rgba, "A")
        mel.connect_material_expressions(p, "A", rgba, "B")
        mel.connect_material_expressions(rgba, "", custom, vname)
        y += 90
    for sname in WARP_SCALARS:
        p = node(X.MaterialExpressionScalarParameter, -1300, y, parameter_name=sname, default_value=0.0)
        mel.connect_material_expressions(p, "", custom, sname)
        y += 70

    color = node(X.MaterialExpressionComponentMask, -450, 0, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(custom, "", color, "")
    alpha = node(X.MaterialExpressionComponentMask, -450, 120, r=False, g=False, b=False, a=True)
    mel.connect_material_expressions(custom, "", alpha, "")
    mel.connect_material_property(color, "", X.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(alpha, "", X.MaterialProperty.MP_OPACITY_MASK)
    glow = node(X.MaterialExpressionScalarParameter, -300, 250, parameter_name="Brightness", default_value=PORTRAIT_BRIGHTNESS)
    emissive = node(X.MaterialExpressionMultiply, -150, 250)
    mel.connect_material_expressions(color, "", emissive, "A"); mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", X.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = node(X.MaterialExpressionScalarParameter, -150, 400, parameter_name="Roughness", default_value=0.8)
    mel.connect_material_property(rough, "", X.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


# Measured on SourceArt/envoy/gage_live.png (1024x1536, whole image shown: crop share 1).
FACE_BRITISH = {
    "ImageSize": (1024.0, 1536.0, 1.0, 0.0),
    "Mouth": (587.0, 453.0, 42.0, 19.0),   # x, y, half width, max jaw drop (px)
    "Lips": (7.0, 13.0, 0.0, 0.0),         # upper, lower lip thickness
    "Jaw": (585.0, 535.0, 70.0, 0.0),      # chin x, y, jaw half width
    "EyeL": (509.0, 333.0, 28.0, 14.0),
    "EyeR": (611.0, 326.0, 28.0, 14.0),
    "Head": (565.0, 340.0, 200.0, 300.0),
}
ENVOY_CM_PER_PX = 0.058  # his head (hair to chin, 450 px) is about 26 cm


def make_envoy_face(still, faction="british", face=FACE_BRITISH):
    """Closed-eyes texture + DA_EnvoyFace_<faction> (UPA250EnvoyFaceData) + M_PortraitWarp."""
    blink = import_envoy_texture("gage_blink.png", "T_EnvoyLiveBlink") or still
    path = "/Game/PA250/Data/DA_EnvoyFace_" + faction
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        da = unreal.EditorAssetLibrary.load_asset(path)
    else:
        da = tools().create_asset("DA_EnvoyFace_" + faction, "/Game/PA250/Data", unreal.PA250EnvoyFaceData, unreal.DataAssetFactory())
    V2 = lambda x, y: unreal.Vector2D(x, y)  # noqa: E731
    for k, v in (("still", still), ("blink", blink), ("image_size", V2(face["ImageSize"][0], face["ImageSize"][1])),
                 ("crop_width", face["ImageSize"][2]), ("mouth_center", V2(face["Mouth"][0], face["Mouth"][1])),
                 ("mouth_half_width", face["Mouth"][2]), ("max_jaw_drop", face["Mouth"][3]),
                 ("upper_lip", face["Lips"][0]), ("lower_lip", face["Lips"][1]),
                 ("chin", V2(face["Jaw"][0], face["Jaw"][1])), ("jaw_half_width", face["Jaw"][2]),
                 ("eye_left", V2(face["EyeL"][0], face["EyeL"][1])), ("eye_right", V2(face["EyeR"][0], face["EyeR"][1])),
                 ("eye_radius", V2(face["EyeL"][2], face["EyeL"][3])),
                 ("head_center", V2(face["Head"][0], face["Head"][1])), ("head_radius", V2(face["Head"][2], face["Head"][3]))):
        da.set_editor_property(k, v)
    unreal.EditorAssetLibrary.save_loaded_asset(da)
    LOG("PA250: face data %s (blink %s)" % (path, "yes" if blink is not still else "MISSING: eyes will not close"))
    return warp_painting_material(still, blink, face)


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


ROOM_PLATE_SRC = os.path.join(PARLOUR_TEX_SRC, "room_plate.png")
ROOM_PLATE_GAIN = 1.0  # emissive scale of the photograph under the room's manual exposure
ROOM_PLATE_TABLE_V = 0.779  # the table's far edge in the photograph (share of its height from the top)


def make_room_plate():
    """The room as a photograph (tools/art/build_parlour_plate.py): T_RoomPlate and the unlit
    M_Parlour_Plate / M_Parlour_PlateTable (the same picture from the table's far edge down).
    (None, None) when the photograph has not been made; the box room is then shown."""
    if not os.path.exists(ROOM_PLATE_SRC):
        LOG("PA250: room plate missing (run tools/art/build_parlour_plate.py); the box room is shown")
        return None, None
    tex = import_texture(ROOM_PLATE_SRC, PARLOUR_DIR + "/Textures", "T_RoomPlate")
    set_props(tex, (
        ("srgb", True),
        ("compression_settings", unreal.TextureCompressionSettings.TC_BC7),
        ("never_stream", True),
        ("address_x", unreal.TextureAddress.TA_CLAMP),
        ("address_y", unreal.TextureAddress.TA_CLAMP),
    ), "T_RoomPlate")
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    mel = unreal.MaterialEditingLibrary
    out = []
    for name, v0 in (("M_Parlour_Plate", 0.0), ("M_Parlour_PlateTable", ROOM_PLATE_TABLE_V)):
        mat = _fresh_material(name)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
        # plane V 0..1 -> picture V v0..1
        scale = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, 0)
        k = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -1100, 150)
        k.set_editor_property("r", 1.0)
        k.set_editor_property("g", 1.0 - v0)
        mel.connect_material_expressions(uv, "", scale, "A")
        mel.connect_material_expressions(k, "", scale, "B")
        shift = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -750, 0)
        o = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -900, 200)
        o.set_editor_property("r", 0.0)
        o.set_editor_property("g", v0)
        mel.connect_material_expressions(scale, "", shift, "A")
        mel.connect_material_expressions(o, "", shift, "B")
        smp = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -550, 0)
        smp.set_editor_property("texture", tex)
        mel.connect_material_expressions(shift, "", smp, "UVs")
        gain = _param(mat, unreal.MaterialExpressionScalarParameter, "Gain", -550, 300, ROOM_PLATE_GAIN)
        # fire and candle light is never still: a few per cent of slow, uneven breathing
        t = mel.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, 500)
        flick = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -850, 500)
        ti = unreal.CustomInput()
        ti.set_editor_property("input_name", "T")
        flick.set_editor_property("inputs", [ti])
        flick.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        flick.set_editor_property("code", "return 1.0 + 0.022 * sin(T * 7.3) + 0.014 * sin(T * 12.9 + 1.7) + 0.01 * sin(T * 23.1 + 0.4);")
        mel.connect_material_expressions(t, "", flick, "T")
        g2 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 300)
        mel.connect_material_expressions(gain, "", g2, "A")
        mel.connect_material_expressions(flick, "", g2, "B")
        mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 0)
        mel.connect_material_expressions(smp, "RGB", mul, "A")
        mel.connect_material_expressions(g2, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        out.append(mat)
    LOG("PA250: room plate T_RoomPlate %dx%d" % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    return out[0], out[1]


def place_room_plate(back_mat, table_mat, cam, pitch, fov, aspect_px):
    """Two planes square to the room camera, each sized to fill the 16:9 view with the
    picture's centre: the whole photograph behind the envoy, and its table again in front of
    him (so the table edge hides his lap as the box table did). The picture is wider than
    16:9, so a wider window sees more room, not an edge."""
    tan_v = math.tan(math.radians(fov / 2.0)) * 9.0 / 16.0  # the engine keeps the vertical FOV
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    fwd = (0.0, -cp, sp)  # camera yaw -90: looking down -Y, pitched by `pitch` (negative = down)
    down = (0.0, -sp, -cp)  # down the screen: square to fwd
    roll = 90.0 + pitch  # plane normal back along the view axis, its +V down the screen
    for label, mat, dist, v0 in (("RoomPlate", back_mat, 260.0, 0.0), ("RoomPlateTable", table_mat, 140.0, ROOM_PLATE_TABLE_V)):
        full_h = 2.0 * dist * tan_v
        w = full_h * aspect_px[0] / aspect_px[1]
        h = full_h * (1.0 - v0)
        off = (v0 + 1.0) / 2.0 - 0.5  # the strip's centre below the view centre, in picture heights
        loc = tuple(cam[i] + fwd[i] * dist + down[i] * off * full_h for i in range(3))
        a = spawn(unreal.StaticMeshActor, loc, (0, 0, roll), label)
        a.set_actor_scale3d(unreal.Vector(w / 100.0, h / 100.0, 1.0))
        c = a.static_mesh_component
        c.set_mobility(unreal.ComponentMobility.STATIC)
        c.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane"))
        c.set_material(0, mat)
        c.set_collision_profile_name("NoCollision")
        c.set_cast_shadow(False)
        set_props(c, (("affect_dynamic_indirect_lighting", False), ("affect_distance_field_lighting", False)), label)
        tag(a, "PA250RoomPlate")
        LOG("PA250: %s at (%.1f, %.1f, %.1f) %.1f x %.1f cm" % (label, loc[0], loc[1], loc[2], w, h))


def build_diplomacy():
    """Georgian parlour, 700 x 600 x 320 cm, lit by fire and candles (GAME INVENTION interior).

    North wall: chimney breast with a marble fireplace and a curtained sash window either side. Panelled
    cream wainscot, green damask above, plank floor, a Turkey carpet under a mahogany table with candles,
    two chairs. The envoy sits in the far chair, facing the player across the table (a masked, lip-synced
    figure plane tagged Portrait). Manual exposure. The camera is the player's eyes across the table.
    """
    still_mat, _ = make_portrait_assets()
    try:
        plate_back, plate_table = make_room_plate()
    except Exception as exc:  # noqa: BLE001 - a bad plate must not cost the room: fall back to the boxes
        LOG("PA250: room plate failed (%s); the box room is shown" % exc)
        plate_back = plate_table = None
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

    # ---- over the mantel: a plain gilt looking-glass (the envoy himself sits at the table, below)
    m_gilt_dark = parlour_mi("MI_Parlour_GiltDark", world, T["marble"], 30, tint=(0.42, 0.29, 0.12), rough=0.55, metal=1.0)
    block("MantelGlass_Frame", (0, face + 2, 205), (88, 4, 64), m_gilt)
    block("MantelGlass_Sight", (0, face + 4.2, 205), (78, 1, 54), m_gilt_dark)
    block("MantelGlass_Plate", (0, face + 4.8, 205), (72, 0.6, 48), m_night)

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
    # With the photograph of the room (make_room_plate) the table, its candles and papers and
    # the chairs are in the picture; only their lights are built, which still light the envoy.
    boxes = plate_back is None
    if boxes:
        block("Table_Top", (0, TY, TZ - 2), (190, 96, 4), m_wood)
        block("Table_Apron", (0, TY, TZ - 9), (176, 82, 10), m_wood)
        for i, (x, y) in enumerate(((-84, TY - 42), (84, TY - 42), (-84, TY + 42), (84, TY + 42))):
            block("Table_Leg_%d" % i, (x, y, (TZ - 14) / 2), (7, 7, TZ - 14), m_wood, mesh="/Engine/BasicShapes/Cylinder")
    for i, (x, y) in enumerate(((-72, TY - 18), (72, TY - 18))):
        if boxes:
            block("TableStick_%d" % i, (x, y, TZ + 1.5), (12, 12, 3), m_brass, mesh="/Engine/BasicShapes/Cylinder")
            block("TableStickStem_%d" % i, (x, y, TZ + 10), (3.5, 3.5, 16), m_brass, mesh="/Engine/BasicShapes/Cylinder")
            block("TableCandle_%d" % i, (x, y, TZ + 26), (3, 3, 16), m_wax, mesh="/Engine/BasicShapes/Cylinder")
            block("TableFlame_%d" % i, (x, y, TZ + 37), (2.2, 2.2, 5), m_flame, mesh="/Engine/BasicShapes/Sphere")
        parlour_light("TableCandleLight_%d" % i, (x, y, TZ + 42), 4.0, (255, 170, 90), 450, tag_name="CandleLight")
    if boxes:
        block("Papers_A", (-12, TY + 10, TZ + 0.3), (30, 22, 0.6), m_paper_sheet, rot=(0, 8, 0))
        block("Papers_B", (16, TY + 14, TZ + 0.6), (30, 22, 0.6), m_paper_sheet, rot=(0, -14, 0))
        block("Inkwell", (40, TY + 18, TZ + 3), (6, 6, 6), m_iron, mesh="/Engine/BasicShapes/Cylinder")
    for name, y, back in ((("Chair_Player", TY + 102, 1), ("Chair_Envoy", TY - 102, -1)) if boxes else ()):
        block(name + "_Seat", (0, y, 46), (56, 52, 5), m_wood)
        block(name + "_Cushion", (0, y, 50), (50, 46, 4), m_velvet)
        block(name + "_Back", (0, y + back * 24, 82), (54, 5, 64), m_wood)
        block(name + "_BackPad", (0, y + back * 21, 84), (40, 2, 46), m_velvet)
        for i, (dx, dy) in enumerate(((-24, -22), (24, -22), (-24, 22), (24, 22))):
            block(name + "_Leg%d" % i, (dx, y + dy, 22), (5, 5, 44), m_wood)

    # ---- the envoy, seated in the far chair: a masked figure plane (waist up; the table hides its lower edge)
    EW, EH = 1024 * ENVOY_CM_PER_PX, 1536 * ENVOY_CM_PER_PX
    ENVOY_TOP = 139.0  # seat 46 + seated height 88 to the crown, plus the image's margin above his hair
    envoy = spawn(unreal.StaticMeshActor, (0, TY - 92, ENVOY_TOP - EH / 2), (0, 0, 90), "EnvoyFigure")
    envoy.set_actor_scale3d(unreal.Vector(EW / 100.0, EH / 100.0, 1.0))
    comp = envoy.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    comp.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane"))
    comp.set_material(0, still_mat)
    comp.set_editor_property("cast_shadow", False)
    tag(envoy, "Portrait")  # the game mode finds his mesh by this tag
    parlour_light("EnvoyKey", (-40, TY + 30, 135), 2.5, (255, 190, 130), 320, shadows=False, source_radius=25)

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

    # ---- camera: the player's eyes across the table; the envoy's face sits above the dialogue panel
    cam = spawn(unreal.CameraActor, (0, 175, 128), (-9.5, -90, 0), "RoomCamera")
    cc = cam.get_component_by_class(unreal.CameraComponent)
    cc.set_editor_property("field_of_view", 42.0)
    tag(cam, "RoomCamera")
    if plate_back:
        place_room_plate(plate_back, plate_table, (0.0, 175.0, 128.0), -9.5, 42.0, (2560, 1088))
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


use_layout("lexington")  # module default, so the audits and helpers work on import

BUILDERS = {
    "campaign": build_campaign,
    "lexington": build_lexington,
    "concord": build_concord,
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
