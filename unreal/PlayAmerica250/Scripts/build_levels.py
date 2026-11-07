"""Editor Python: builds PA250 levels and imports assets. Idempotent.

Run headless:

  UnrealEditor-Cmd PlayAmerica250.uproject \\
    -run=pythonscript -script=Scripts/build_levels.py \\
    -unattended -nullrhi

Selects which levels with env PA250_LEVELS (comma list of: campaign,
lexington, diplomacy; default all).
"""
import os

import unreal

LOG = unreal.log
MAPS = "/Game/PA250/Maps"
PROJECT = unreal.SystemLibrary.get_project_directory()


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
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    ok = les.new_level(path)
    if not ok:
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
    v.set_editor_property("reload_time", 20.0)  # historical ~20 s
    v.set_editor_property("projectile_speed", 3000.0)
    v.set_editor_property("damage", 40.0)
    asset.set_editor_property("values", v)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


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


def block(label, loc, size, mat, rot_yaw=0.0, mesh="/Engine/BasicShapes/Cube"):
    """Graybox block. `size` in cm (x, y, z); loc is the centre."""
    a = spawn(unreal.StaticMeshActor, loc, (0, rot_yaw, 0), label)
    comp = a.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    comp.set_static_mesh(unreal.EditorAssetLibrary.load_asset(mesh))
    a.set_actor_scale3d(
        unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0)
    )
    comp.set_material(0, mat)
    return a


def strip(label, p0, p1, width, height, mat):
    """A road/wall segment between two ground points."""
    import math

    cx, cy = (p0[0] + p1[0]) / 2.0, (p0[1] + p1[1]) / 2.0
    length = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    yaw = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
    return block(
        label,
        (cx, cy, height / 2.0),
        (length, width, height),
        mat,
        rot_yaw=yaw,
    )


def tag(actor, name):
    actor.set_editor_property("tags", [unreal.Name(name)])


# ---------------------------------------------------------------- levels


def build_campaign():
    src = os.path.join(PROJECT, "Content/PA250/Source/campaign_map.png")
    import_texture(src, "/Game/PA250/Textures", "T_CampaignMap")
    les = new_level(MAPS + "/L_Campaign")
    set_game_mode("PA250CampaignGameMode")
    save(les)
    LOG("PA250: built L_Campaign")


def build_lexington():
    """Graybox Lexington Green, 19 April 1775.

    Units are cm; +X east (British road), -X west (Concord road).
    """
    make_tuning()
    grass = make_material("MI_PA250_Grass", (0.20, 0.36, 0.14))
    ground = make_material("MI_PA250_Ground", (0.28, 0.24, 0.15))
    dirt = make_material("MI_PA250_Dirt", (0.38, 0.28, 0.17))
    stone = make_material("MI_PA250_Stone", (0.45, 0.45, 0.43))
    wood = make_material("MI_PA250_Wood", (0.36, 0.22, 0.12))
    white = make_material("MI_PA250_White", (0.82, 0.80, 0.72))
    roof = make_material("MI_PA250_Roof", (0.22, 0.12, 0.09))

    les = new_level(MAPS + "/L_Lexington")
    set_game_mode("PA250BattleGameMode")

    # Terrain: big slab + lighter grass triangle (the Common).
    block("Ground", (0, 0, -50), (30000, 24000, 100), ground)
    # NW tip, NE corner, S corner of the triangular common
    A, B, C = (-3000, -4000), (2500, -2500), (-1000, 3500)
    common_center = (
        (A[0] + B[0] + C[0]) / 3.0,
        (A[1] + B[1] + C[1]) / 3.0,
        2,
    )
    block("CommonGrass", common_center, (4200, 3600, 4), grass, rot_yaw=-8)
    strip("Road_N", A, B, 450, 6, dirt)
    strip("Road_E", B, C, 450, 6, dirt)
    strip("Road_W", C, A, 450, 6, dirt)
    # British arrive from the east
    strip("BostonRoad", (2500, -2500), (13500, -700), 600, 6, dirt)
    # leads west toward Concord
    strip("ConcordRoad", (-3000, -4000), (-14000, -5200), 600, 6, dirt)
    strip("ConcordRoad_South", (-1000, 3500), (-14000, 2600), 500, 6, dirt)

    # Meetinghouse with belfry at the northwest tip of the Common.
    block("Meetinghouse", (-3200, -5200, 450), (2200, 1500, 900), white)
    block("Meetinghouse_Roof", (-3200, -5200, 1000), (2300, 1600, 200), roof)
    block("Belfry", (-3200, -5200, 1500), (420, 420, 1200), white)
    block("Belfry_Cap", (-3200, -5200, 2200), (520, 520, 220), roof)
    # Buckman Tavern on the southwest corner.
    block("BuckmanTavern", (-4300, 1800, 380), (1900, 1100, 760), wood)
    block("BuckmanTavern_Roof", (-4300, 1800, 840), (2000, 1200, 160), roof)

    # Stone walls along the Concord road, both sides.
    for i in range(10):
        x0 = -4000 - i * 1000
        block(
            f"Wall_N_{i}",
            (x0, -4600 - i * 110, 60),
            (900, 70, 120),
            stone,
        )
        block(
            f"Wall_S_{i}",
            (x0, -3600 - i * 110, 60),
            (900, 70, 120),
            stone,
        )

    # Spawns. Continentals south of Buckman Tavern (y 1250..2350).
    for i in range(14):
        loc = (-6200 + (i % 4) * 280, -200 + (i // 4) * 280, 200)
        tag(
            spawn(
                unreal.PlayerStart,
                loc,
                (0, 20, 0),
                f"Start_Continental_{i}",
            ),
            "continental",
        )
    for i in range(14):
        loc = (9500 + (i // 4) * 350, -1500 + (i % 4) * 350, 100)
        tag(
            spawn(
                unreal.PlayerStart,
                loc,
                (0, 180, 0),
                f"Start_British_{i}",
            ),
            "british",
        )

    # Objective: control point on the middle of the Green.
    obj_loc = (
        (A[0] + B[0] + C[0]) / 3.0,
        (A[1] + B[1] + C[1]) / 3.0,
        130,
    )
    spawn(
        unreal.PA250ControlPoint,
        obj_loc,
        (0, 0, 0),
        "Objective_Green",
    )

    # Navigation, light and sky.
    spawn(
        unreal.NavMeshBoundsVolume,
        (0, 0, 0),
        (0, 0, 0),
        "NavBounds",
        scale=(160, 130, 12),
    )
    spawn(unreal.DirectionalLight, (0, 0, 2000), (-42, 35, 0), "Sun")
    spawn(unreal.SkyAtmosphere, (0, 0, 0), (0, 0, 0), "SkyAtmosphere")
    spawn(unreal.SkyLight, (0, 0, 1500), (0, 0, 0), "SkyLight")
    spawn(unreal.ExponentialHeightFog, (0, 0, 0), (0, 0, 0), "Fog")
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

    def unlit_material(name, texture):
        path = "/Game/PA250/Materials/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        mat = tools().create_asset(
            name,
            "/Game/PA250/Materials",
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
        mat.set_editor_property(
            "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
        )
        mat.set_editor_property("two_sided", True)
        node = unreal.MaterialEditingLibrary.create_material_expression(
            mat,
            unreal.MaterialExpressionTextureSampleParameter2D,
            -400,
            0,
        )
        node.set_editor_property("parameter_name", "Portrait")
        node.set_editor_property("texture", texture)
        unreal.MaterialEditingLibrary.connect_material_property(
            node, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        )
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        return mat

    still_mat = unlit_material("M_PortraitStill", still)
    video_mat = unlit_material("M_PortraitVideo", mt)
    return still_mat, video_mat


def build_diplomacy():
    """Graybox candlelit parlor, 700 x 600 x 320 cm.

    The envoy's portrait hangs on the north wall.
    """
    still_mat, _ = make_portrait_assets()
    floor = make_material("MI_PA250_Floor", (0.22, 0.13, 0.07))
    wall = make_material("MI_PA250_Wall", (0.40, 0.34, 0.26))
    wood = make_material("MI_PA250_Wood", (0.36, 0.22, 0.12))
    cloth = make_material("MI_PA250_Cloth", (0.32, 0.05, 0.05))
    gold = make_material("MI_PA250_Gold", (0.70, 0.55, 0.15))
    candle = make_material("MI_PA250_Candle", (0.95, 0.9, 0.7))

    les = new_level(MAPS + "/L_DiplomacyRoom")
    set_game_mode("PA250DiplomacyGameMode")

    block("Floor", (0, 0, -5), (700, 600, 10), floor)
    block("Ceiling", (0, 0, 325), (700, 600, 10), wall)
    block("Wall_North", (0, -305, 160), (700, 10, 320), wall)
    block("Wall_South", (0, 305, 160), (700, 10, 320), wall)
    block("Wall_East", (355, 0, 160), (10, 600, 320), wall)
    block("Wall_West", (-355, 0, 160), (10, 600, 320), wall)
    block("Wainscot_North", (0, -297, 50), (700, 6, 100), wood)

    # Table, two chairs.
    block("Table", (0, -10, 38), (240, 100, 8), wood)
    for i, (x, y) in enumerate(
        [(-105, -45), (105, -45), (-105, 25), (105, 25)]
    ):
        block(f"TableLeg_{i}", (x, y, 17), (10, 10, 34), wood)
    block("TableCloth", (0, -10, 43), (200, 70, 2), cloth)
    for name, y, _yaw in (
        ("Chair_Envoy", -150, 0),
        ("Chair_Player", 130, 180),
    ):
        block(name + "_Seat", (0, y, 25), (55, 55, 8), wood)
        back_y = y + (-27 if y < 0 else 27)
        block(name + "_Back", (0, back_y, 62), (55, 6, 70), wood)
        for i, (dx, dy) in enumerate(
            [(-22, -22), (22, -22), (-22, 22), (22, 22)]
        ):
            block(
                name + f"_Leg{i}",
                (dx, y + dy, 11),
                (6, 6, 22),
                wood,
            )

    # Candles with warm point lights.
    for i, x in enumerate((-70, 0, 70)):
        block(
            f"Candle_{i}",
            (x, -10, 56),
            (4, 4, 24),
            candle,
            mesh="/Engine/BasicShapes/Cylinder",
        )
        pl = spawn(
            unreal.PointLight,
            (x, -10, 82),
            (0, 0, 0),
            f"CandleLight_{i}",
        )
        comp = pl.get_component_by_class(unreal.PointLightComponent)
        comp.set_editor_property("intensity", 60.0)
        comp.set_editor_property("light_color", unreal.Color(255, 170, 90, 255))
        comp.set_editor_property("attenuation_radius", 520.0)
        comp.set_editor_property("source_radius", 3.0)
    sky = spawn(unreal.SkyLight, (0, 0, 250), (0, 0, 0), "DimAmbient")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property(
        "intensity", 0.15
    )

    # Framed portrait: frame slab + media-fed plane in front.
    block("PortraitFrame", (0, -297, 195), (150, 6, 190), gold)
    portrait = spawn(
        unreal.StaticMeshActor,
        (0, -293, 195),
        (0, 0, 90),
        "EnvoyPortrait",
    )
    portrait.set_actor_scale3d(unreal.Vector(1.3, 1.7, 1.0))  # 130 x 170 cm
    comp = portrait.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    comp.set_static_mesh(
        unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    )
    comp.set_material(0, still_mat)
    tag(portrait, "Portrait")

    # Fixed camera looking at the envoy and portrait.
    cam = spawn(unreal.CameraActor, (0, 270, 165), (-4, -90, 0), "RoomCamera")
    tag(cam, "RoomCamera")
    save(les)
    LOG("PA250: built L_DiplomacyRoom")


BUILDERS = {
    "campaign": build_campaign,
    "lexington": build_lexington,
    "diplomacy": build_diplomacy,
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
