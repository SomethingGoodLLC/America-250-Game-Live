#!/usr/bin/env python3
"""Import City View header/icons from Content/PA250/Source/CityViewArt.

Outputs under /Game/PA250/UI/CityView/:
  T_TownHeader, T_Icon_Farm, T_Icon_Workshop, T_Icon_Depot, T_Icon_MilitiaMuster

Run with the editor closed:
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript \\
    -script=<abs>/Scripts/import_city_view_art.py -unattended -nullrhi
"""

from __future__ import annotations

import os

import unreal

PROJECT_CONTENT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
SRC_DIR = os.path.join(PROJECT_CONTENT, "PA250", "Source", "CityViewArt")
TEX_DIR = "/Game/PA250/UI/CityView"

ASSETS = (
    ("town_header.jpg", "T_TownHeader"),
    ("farm.jpg", "T_Icon_Farm"),
    ("workshop.png", "T_Icon_Workshop"),
    ("depot.png", "T_Icon_Depot"),
    ("militia_muster.png", "T_Icon_MilitiaMuster"),
)


def _log(msg: str) -> None:
    unreal.log(f"CITY_VIEW_ART: {msg}")


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
    _set(tex, "srgb", True)
    _set(tex, "compression_settings", unreal.TextureCompressionSettings.TC_BC7)
    _set(tex, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    _set(tex, "lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    _set(tex, "filter", unreal.TextureFilter.TF_BILINEAR)
    _set(tex, "address_x", unreal.TextureAddress.TA_CLAMP)
    _set(tex, "address_y", unreal.TextureAddress.TA_CLAMP)
    _set(tex, "never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    _log(f"texture {name} ok")
    return tex


def main() -> None:
    unreal.EditorAssetLibrary.make_directory(TEX_DIR)
    for filename, name in ASSETS:
        import_texture(os.path.join(SRC_DIR, filename), name)
    _log("done")


if __name__ == "__main__":
    main()
