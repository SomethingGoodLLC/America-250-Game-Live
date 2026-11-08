#!/usr/bin/env python3
"""Import Boston1775 textures and build bulletproof Unlit display materials."""
from __future__ import annotations

import os
import sys
import unreal

CONTENT = "/Game/Map/Boston1775"
SRC = unreal.Paths.project_content_dir() + "Map/Boston1775/Source/"
PREVIEW = unreal.Paths.project_dir() + "../tools/mapgen/out/preview_display.png"
# project_dir is unreal/PlayAmerica250/; mapgen lives at repo tools/
PREVIEW_ABS = os.path.normpath(
    os.path.join(unreal.Paths.project_dir(), "..", "..", "tools", "mapgen", "out", "preview_display.png")
)
# From Content: ../../tools/...  ProjectDir = .../PlayAmerica250/
# PlayAmerica250/../ = unreal/, unreal/../ = repo root
PREVIEW_CANDIDATES = [
    os.path.normpath(os.path.join(unreal.Paths.project_dir(), "..", "..", "tools", "mapgen", "out", "preview_display.png")),
    os.path.normpath(os.path.join(unreal.Paths.project_content_dir(), "..", "..", "..", "tools", "mapgen", "out", "preview_display.png")),
    os.path.normpath(os.path.join(unreal.Paths.project_dir(), "..", "..", "tools", "mapgen", "out", "preview.png")),
]


def _import_texture(src_path: str, dest_path: str, *, nearest: bool, srgb: bool, compression: str) -> unreal.Texture2D | None:
    if not os.path.isfile(src_path):
        unreal.log_warning(f"skip missing {src_path}")
        return None
    task = unreal.AssetImportTask()
    task.filename = src_path
    task.destination_path = os.path.dirname(dest_path)
    task.destination_name = os.path.basename(dest_path)
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(dest_path)
    if tex is None:
        raise RuntimeError(f"failed to import {src_path}")
    if nearest:
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    if compression == "vector":
        try:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        except Exception:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif compression == "gray":
        try:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_asset(dest_path)
    return tex


def _make_unlit_texture_mat(mat_path: str, param_name: str, default_tex) -> unreal.Material:
    factory = unreal.MaterialFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    else:
        mat = asset_tools.create_asset(os.path.basename(mat_path), os.path.dirname(mat_path), unreal.Material, factory)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    tex = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -300, 0
    )
    tex.set_editor_property("parameter_name", param_name)
    if default_tex is not None:
        tex.texture = default_tex
    # Constant fallback so an unbound param is parchment, never black.
    fallback = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -300, 200
    )
    fallback.set_editor_property("constant", unreal.LinearColor(0.82, 0.74, 0.55, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(mat_path)
    return mat


def main() -> int:
    os.makedirs(unreal.Paths.project_content_dir() + "Map/Boston1775", exist_ok=True)
    pid = _import_texture(
        SRC + "ProvinceID.png",
        CONTENT + "/T_ProvinceID",
        nearest=True,
        srgb=False,
        compression="vector",
    )
    _import_texture(
        SRC + "BorderDist.png",
        CONTENT + "/T_BorderDist",
        nearest=True,
        srgb=False,
        compression="gray",
    )
    _import_texture(
        SRC + "CoastDist.png",
        CONTENT + "/T_CoastDist",
        nearest=True,
        srgb=False,
        compression="gray",
    )

    preview = None
    for cand in PREVIEW_CANDIDATES:
        unreal.log(f"preview candidate: {cand} exists={os.path.isfile(cand)}")
        if os.path.isfile(cand):
            preview = _import_texture(
                cand,
                CONTENT + "/T_MapPreview",
                nearest=False,
                srgb=True,
                compression="default",
            )
            break

    # Bulletproof display material: one texture → emissive (never black if texture bound).
    _make_unlit_texture_mat(CONTENT + "/M_MapDisplay", "Display", preview or pid)

    # Keep M_ProvinceMap as Unlit land/water for later wash polish.
    mat_path = CONTENT + "/M_ProvinceMap"
    factory = unreal.MaterialFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    else:
        mat = asset_tools.create_asset("M_ProvinceMap", CONTENT, unreal.Material, factory)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    parchment = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -200, 0
    )
    parchment.set_editor_property("constant", unreal.LinearColor(0.82, 0.74, 0.55, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(parchment, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(parchment, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Keep params alive for runtime MID.
    for name, y in (("ProvinceID", -200), ("Paper", 0), ("ProvinceState", 200), ("BorderDist", 400), ("CoastDist", 600)):
        e = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, y
        )
        e.set_editor_property("parameter_name", name)
        if name == "ProvinceID" and pid is not None:
            e.texture = pid
    band = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -500, 800
    )
    band.set_editor_property("parameter_name", "BandPx")
    band.set_editor_property("default_value", 18.0)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(mat_path)

    unreal.log("build_map_assets: M_MapDisplay + Unlit M_ProvinceMap ready")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        unreal.log_error(f"build_map_assets failed: {exc}")
        sys.exit(1)
