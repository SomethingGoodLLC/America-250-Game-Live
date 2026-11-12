#!/usr/bin/env python3
"""Import campaign-map army illustrations and the sprite material.

Inputs (prepared by tools/art/prepare_army_sprites.py):
  Content/PA250/Source/ArmyArt/army_{provincial_regulars,british_marching,british_firing,militia,ground_oval}.png
  Content/PA250/Source/ArmyArt/town_wash_disc.png
Outputs:
  /Game/PA250/Textures/Army/T_Army_{ProvincialRegulars,BritishMarching,BritishFiring,Militia,GroundOval}
  /Game/PA250/Textures/Army/T_Map_TownWash
  /Game/PA250/Materials/M_MapSprite
    (unlit, translucent; params Sprite, UVRect, Tint, Opacity)
  /Game/PA250/Materials/M_MapText (unlit copy of the engine text material)

Run with the editor closed:
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript \
    -script=<abs>/Scripts/import_army_art.py -unattended -nullrhi

PA250_ARMY_ART_ONLY=T_Army_Militia (comma list) imports just those textures and leaves
every other asset, including the materials, untouched.
"""

from __future__ import annotations

import os

import unreal

PROJECT_CONTENT = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_content_dir()
)
SRC_DIR = os.path.join(PROJECT_CONTENT, "PA250", "Source", "ArmyArt")
TEX_DIR = "/Game/PA250/Textures/Army"
MAT_PATH = "/Game/PA250/Materials/M_MapSprite"

SPRITES = (
    ("army_provincial_regulars.png", "T_Army_ProvincialRegulars"),
    ("army_british_marching.png", "T_Army_BritishMarching"),
    ("army_british_firing.png", "T_Army_BritishFiring"),
    ("army_militia.png", "T_Army_Militia"),
    ("army_ground_oval.png", "T_Army_GroundOval"),
    ("town_wash_disc.png", "T_Map_TownWash"),
)


def _log(msg: str) -> None:
    unreal.log(f"ARMY_ART_IMPORT: {msg}")


def _set(obj: unreal.Object, prop: str, value: object) -> None:
    try:
        obj.set_editor_property(prop, value)
    except Exception as exc:  # noqa: BLE001
        _log(f"warn: {obj.get_name()}.{prop} not set ({exc})")


def import_texture(src: str, name: str) -> unreal.Texture2D:
    if not os.path.isfile(src):
        raise RuntimeError(f"missing {src}")
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = TEX_DIR
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(f"{TEX_DIR}/{name}")
    if tex is None:
        raise RuntimeError(f"import failed for {src}")
    # Colour art with straight alpha: sRGB, BC7 (keeps alpha quality), full mip chain
    # (sources are power of two), trilinear via the World group, and resident, so the
    # first frames never show blurry low mips.
    _set(tex, "srgb", True)
    _set(tex, "compression_settings", unreal.TextureCompressionSettings.TC_BC7)
    _set(tex, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    _set(tex, "lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    _set(tex, "filter", unreal.TextureFilter.TF_DEFAULT)
    _set(tex, "address_x", unreal.TextureAddress.TA_CLAMP)
    _set(tex, "address_y", unreal.TextureAddress.TA_CLAMP)
    _set(tex, "never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    prop = tex.get_editor_property
    _log(
        f"texture {name} {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()} "
        f"srgb={prop('srgb')} comp={prop('compression_settings')} "
        f"mips={prop('mip_gen_settings')} group={prop('lod_group')} "
        f"never_stream={prop('never_stream')}"
    )
    return tex


def build_material(default_tex: unreal.Texture2D) -> unreal.Material:
    mel = unreal.MaterialEditingLibrary
    mat = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            os.path.basename(MAT_PATH),
            os.path.dirname(MAT_PATH),
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    _set(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    _set(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    _set(mat, "two_sided", False)
    _set(mat, "enable_responsive_aa", True)
    mel.delete_all_material_expressions(mat)

    uv = mel.create_material_expression(
        mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0
    )
    rect = mel.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -1100, 200
    )
    rect.set_editor_property(
        "parameter_name", "UVRect"
    )  # (offset u, offset v, scale u, scale v)
    rect.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    off = mel.create_material_expression(
        mat, unreal.MaterialExpressionAppendVector, -850, 160
    )
    scl = mel.create_material_expression(
        mat, unreal.MaterialExpressionAppendVector, -850, 260
    )
    mel.connect_material_expressions(rect, "R", off, "A")
    mel.connect_material_expressions(rect, "G", off, "B")
    mel.connect_material_expressions(rect, "B", scl, "A")
    mel.connect_material_expressions(rect, "A", scl, "B")
    mul = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -650, 60
    )
    mel.connect_material_expressions(uv, "", mul, "A")
    mel.connect_material_expressions(scl, "", mul, "B")
    add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -500, 60)
    mel.connect_material_expressions(mul, "", add, "A")
    mel.connect_material_expressions(off, "", add, "B")

    tex = mel.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -320, 0
    )
    tex.set_editor_property("parameter_name", "Sprite")
    tex.set_editor_property("texture", default_tex)
    tex.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    )
    mel.connect_material_expressions(add, "", tex, "UVs")

    tint = mel.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -320, 260
    )
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    col = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -80, 0)
    mel.connect_material_expressions(tex, "RGB", col, "A")
    mel.connect_material_expressions(tint, "", col, "B")
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opa = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -320, 400
    )
    opa.set_editor_property("parameter_name", "Opacity")
    opa.set_editor_property("default_value", 1.0)
    amul = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -80, 200
    )
    mel.connect_material_expressions(tex, "A", amul, "A")
    mel.connect_material_expressions(opa, "", amul, "B")
    mel.connect_material_property(amul, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    _log(
        f"material {mat.get_path_name()} blend={mat.get_editor_property('blend_mode')} "
        f"shading={mat.get_editor_property('shading_model')}"
    )
    return mat


TEXT_MAT_PATH = "/Game/PA250/Materials/M_MapText"
ENGINE_TEXT_MAT = "/Engine/EngineMaterials/DefaultTextMaterialTranslucent"


def build_text_material() -> unreal.Material:
    """Unlit copy of the engine text material: same font sample and opacity.

    The engine material is default-lit (base colour = vertex colour), so the campaign
    key light turned dark map captions pastel. Unlit, with emissive = vertex colour,
    shows the TextRender colour exactly.
    """
    mel = unreal.MaterialEditingLibrary
    src = unreal.load_asset(ENGINE_TEXT_MAT)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.duplicate_asset(
        os.path.basename(TEXT_MAT_PATH), os.path.dirname(TEXT_MAT_PATH), src
    )
    if mat is None:
        raise RuntimeError(f"could not duplicate {ENGINE_TEXT_MAT}")
    base = unreal.MaterialProperty.MP_BASE_COLOR
    vc = mel.get_material_property_input_node(mat, base)
    if vc is None:
        raise RuntimeError("engine text material has no base colour input")
    _set(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    shading = mat.get_editor_property("shading_model")
    _log(f"material {mat.get_path_name()} shading={shading}")
    return mat


def main() -> int:
    only = {n for n in os.environ.get("PA250_ARMY_ART_ONLY", "").split(",") if n}
    if only:
        unknown = only - {name for _, name in SPRITES}
        if unknown:
            raise RuntimeError(f"unknown textures {sorted(unknown)}")
        for src, name in SPRITES:
            if name in only:
                import_texture(os.path.join(SRC_DIR, src), name)
        _log(f"OK (only {sorted(only)})")
        return 0
    texs = [import_texture(os.path.join(SRC_DIR, src), name) for src, name in SPRITES]
    # Rebuilding an existing M_MapSprite while the army token CDO references it asserts
    # (!IsRooted) in UE 5.8 and the material is never saved. Build it only when missing;
    # to change the graph, delete the .uasset with the editor closed and rerun this.
    if unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
        _log(f"material {MAT_PATH} exists; kept (delete it to rebuild)")
    else:
        build_material(texs[0])
    if unreal.EditorAssetLibrary.does_asset_exist(TEXT_MAT_PATH):
        _log(f"material {TEXT_MAT_PATH} exists; kept (delete it to rebuild)")
    else:
        build_text_material()
    _log("OK")
    return 0


try:
    main()
except Exception as exc:  # noqa: BLE001
    unreal.log_error(f"ARMY_ART_IMPORT: FAIL {exc}")
    raise
