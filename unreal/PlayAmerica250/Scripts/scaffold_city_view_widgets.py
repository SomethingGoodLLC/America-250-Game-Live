#!/usr/bin/env python3
"""Scaffold WBP_CityView, a Blueprint child of the C++ town panel (UPA250CityViewWidget).

Safe to re-run: skips an asset that already exists. The panel's layout is Slate (C++);
the Blueprint exists so the header painting and plot pictures (TownHeaderTexture,
BuildingIcons) can be swapped in its class defaults without a C++ rebuild.

  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript \\
    -script=<abs>/Scripts/scaffold_city_view_widgets.py -unattended -nullrhi
"""

from __future__ import annotations

import unreal

OUT_DIR = "/Game/PA250/UI/CityView"
CITY_BP = f"{OUT_DIR}/WBP_CityView"
CITY_CLASS = "/Script/PlayAmerica250.PA250CityViewWidget"


def _log(msg: str) -> None:
    unreal.log(f"CITY_VIEW_SCAFFOLD: {msg}")


def _create_widget_bp(path: str, parent_path: str) -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        _log(f"exists {path}")
        return
    parent = unreal.load_class(None, parent_path)
    if parent is None:
        raise RuntimeError(f"missing parent class {parent_path}")
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    asset_name = path.rsplit("/", 1)[-1]
    package_path = path.rsplit("/", 1)[0]
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, unreal.WidgetBlueprint, factory
    )
    if asset is None:
        raise RuntimeError(f"failed to create {path}")
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    _log(f"created {path}")


def main() -> None:
    unreal.EditorAssetLibrary.make_directory(OUT_DIR)
    _create_widget_bp(CITY_BP, CITY_CLASS)
    _log("done — set TownHeaderTexture / BuildingIcons in WBP_CityView class defaults to swap art")


if __name__ == "__main__":
    main()
