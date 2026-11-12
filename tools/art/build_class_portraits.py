#!/usr/bin/env python3
"""Cut the class picker's card images from in-game stills.

Input: unreal/PlayAmerica250/Saved/PA250/portraits/<faction>_<class>.png (1280x720), written by a real
window run of the battle level with -PA250ClassPortraits -PA250Bots=0 (see Battle/PA250ClassPortraits.cpp).
Output: unreal/PlayAmerica250/Content/PA250/Source/ClassPortraits/<faction>_<class>.png (head to waist,
440x330), imported by unreal/PlayAmerica250/Scripts/import_class_portraits.py.

Run with /opt/homebrew/bin/python3 (needs Pillow).
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "unreal" / "PlayAmerica250" / "Saved" / "PA250" / "portraits"
DST = ROOT / "unreal" / "PlayAmerica250" / "Content" / "PA250" / "Source" / "ClassPortraits"

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
# The capture camera centres the man; this box holds his headgear (a bearskin reaches the top) to his waist.
BOX = (420, 0, 860, 330)


def main() -> int:
    DST.mkdir(parents=True, exist_ok=True)
    missing = [name for name in CLASSES if not (SRC / f"{name}.png").is_file()]
    if missing:
        print(f"missing stills in {SRC}: {', '.join(missing)}", file=sys.stderr)
        return 1
    for name in CLASSES:
        still = Image.open(SRC / f"{name}.png").convert("RGB")
        if still.size != (1280, 720):
            print(f"{name}: expected 1280x720, got {still.size}", file=sys.stderr)
            return 1
        still.crop(BOX).save(DST / f"{name}.png")
        print(f"wrote {DST / (name + '.png')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
