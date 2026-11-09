#!/usr/bin/env python3
"""Import M01-de-costa-boston-concord.jpg as /Game/PA250/Textures/T_DeCostaBostonConcord.

Run inside the Unreal editor (Python) or via:
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=Scripts/import_decosta.py
"""
from __future__ import annotations

import os
import sys

# Reuse the map-asset importer (adds mips + BC7 options).
REPO_TOOLS = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools", "ue")
)
if REPO_TOOLS not in sys.path:
    sys.path.insert(0, REPO_TOOLS)

import build_map_assets  # noqa: E402


def main() -> int:
    tex = build_map_assets.import_decosta(use_bc7=True)
    if tex is None:
        print("DECOSTA_IMPORT_FAIL")
        return 1
    print(
        "DECOSTA_IMPORT_OK",
        tex.get_name(),
        tex.blueprint_get_size_x(),
        tex.blueprint_get_size_y(),
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print("DECOSTA_IMPORT_FAIL", exc)
        raise
