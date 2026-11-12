#!/usr/bin/env python3
"""Import the soldier class card images from Content/PA250/Source/ClassPortraits.

Outputs /Game/PA250/UI/Classes/T_Class_<faction>_<class> (UI textures, no mips).
The source PNGs are cut by tools/art/build_class_portraits.py from in-game stills.

Run with the editor closed:
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript \\
    -script=<abs>/Scripts/import_class_portraits.py -unattended -nullrhi
"""

from __future__ import annotations

import os

import unreal

PROJECT_CONTENT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
SRC_DIR = os.path.join(PROJECT_CONTENT, "PA250", "Source", "ClassPortraits")
TEX_DIR = "/Game/PA250/UI/Classes"

CLASSES = (
    "british_line_infantry",
    "british_grenadier",
    "british_light_infantry",
    "british_officer",
    "continental_line_infantry",
    "continental_militia",
    "continental_rifleman",
    "continental_officer",
)


def _log(msg: str) -> None:
    unreal.log(f"CLASS_PORTRAITS: {msg}")


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
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    tex.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def main() -> None:
    for name in CLASSES:
        tex = import_texture(os.path.join(SRC_DIR, f"{name}.png"), f"T_Class_{name}")
        _log(f"{tex.get_path_name()} {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()}")
    _log(f"done: {len(CLASSES)} textures")


main()
