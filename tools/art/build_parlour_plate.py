"""The parley room as a photograph (the room camera never moves).

The envoy is a photoreal man; the room built from boxes round him was not. This turns a
still of that box room into a photograph of the same room from the same camera with the
OpenAI image-edit endpoint (PA250_OPENAI_IMAGE_MODEL, default gpt-image-2.6-sunburst), so
the table edge, the chair and the fireplace stay where the game expects them:

  SourceArt/parlour/plate_layout.png   the box-room still, padded to 2.35:1 (the model's guide)
  SourceArt/parlour/plate_raw.png      the model's photograph, kept as delivered
  SourceArt/parlour/room_plate.png     2560x1088 sRGB, shown by build_levels.py build_diplomacy
                                       on two planes square to the camera: the whole picture
                                       behind the envoy, and its table again in front of him

  /opt/homebrew/bin/python3 tools/art/build_parlour_plate.py --layout <16:9 still of the box room> [--force]
  /opt/homebrew/bin/python3 tools/art/build_parlour_plate.py --process-only

The room is a GAME INVENTION interior (a Boston parlour of the 1770s), as before.
OPENAI_API_KEY comes from services/negotiation/.env and is never printed.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import urllib.error
import urllib.request
import uuid
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_battlefield_textures as bt  # noqa: E402

DIR = bt.ROOT / "unreal/PlayAmerica250/SourceArt/parlour"
SIZE = (2560, 1088)

PROMPT = (
    "This picture is a rough 3D blocking of a scene, padded with plain grey bars at the left and "
    "right. Re-create it as a real photograph with exactly the same camera position, framing and "
    "layout: a parlour in a prosperous Boston house in 1775, at night, lit only by the wood fire "
    "and by candles. Keep everything where it is in the blocking: the polished mahogany table "
    "across the foreground with its far edge as a straight level line at the same height in the "
    "frame; one empty mahogany side chair with a red upholstered back standing behind the table "
    "exactly in the centre, facing the camera; behind it the chimney breast with a marble "
    "fireplace and a burning log fire, a mahogany mantel shelf with two brass candlesticks and "
    "lit candles; cream painted panelled wainscot, dark green silk damask wall hangings above; "
    "tall sash windows with heavy crimson curtains at the far left and far right. On the table: "
    "two brass candlesticks with lit beeswax candles, one near each side, a few sheets of laid "
    "paper, a quill and a pewter inkwell. Remove the man completely: nobody is in the room and "
    "the chair is empty. Replace the grey bars by continuing the same room naturally to both "
    "sides. Real photograph on a full-frame camera, 35 mm lens, shallow candle-lit exposure, "
    "warm firelight, deep natural shadows, fine wood grain and fabric texture, slight depth of "
    "field softness in the far wall. No people, no text, no modern objects, no electric light."
)


def make_layout(still: Path) -> Path:
    src = Image.open(still).convert("RGB")
    w = round(SIZE[1] * src.size[0] / src.size[1] / 2) * 2
    canvas = Image.new("RGB", SIZE, (96, 96, 96))
    canvas.paste(src.resize((w, SIZE[1]), Image.LANCZOS), ((SIZE[0] - w) // 2, 0))
    dest = DIR / "plate_layout.png"
    canvas.save(dest)
    print(
        f"wrote {dest.name}: still {src.size[0]}x{src.size[1]} -> centre {w}x{SIZE[1]} of {SIZE[0]}x{SIZE[1]}",
        flush=True,
    )
    return dest


def edit(prompt: str, image: Path, dest: Path) -> None:
    """One image from the OpenAI image-edit endpoint (multipart form, standard library only)."""
    print(f"openai edit: {bt.MODEL} -> {dest.name}", flush=True)
    boundary = uuid.uuid4().hex
    parts = []
    for key, value in (
        ("model", bt.MODEL),
        ("prompt", prompt),
        ("size", f"{SIZE[0]}x{SIZE[1]}"),
        ("quality", "high"),
        ("n", "1"),
    ):
        parts.append(
            f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode()
        )
    parts.append(
        f'--{boundary}\r\nContent-Disposition: form-data; name="image"; filename="{image.name}"\r\n'
        f"Content-Type: image/png\r\n\r\n".encode()
        + image.read_bytes()
        + b"\r\n"
    )
    parts.append(f"--{boundary}--\r\n".encode())
    req = urllib.request.Request(
        "https://api.openai.com/v1/images/edits",
        data=b"".join(parts),
        headers={
            "Authorization": "Bearer " + os.environ["OPENAI_API_KEY"],
            "Content-Type": "multipart/form-data; boundary=" + boundary,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=900) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as exc:
        sys.exit(
            f"openai {bt.MODEL} edit failed: HTTP {exc.code} {exc.read()[:400].decode(errors='replace')}"
        )
    dest.write_bytes(base64.b64decode(data["data"][0]["b64_json"]))


def process() -> None:
    raw = Image.open(DIR / "plate_raw.png").convert("RGB")
    if raw.size != SIZE:
        raw = raw.resize(SIZE, Image.LANCZOS)
    raw.save(DIR / "room_plate.png")
    print(f"wrote room_plate.png {SIZE[0]}x{SIZE[1]}", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--layout",
        default="",
        help="16:9 still of the box room (taken from the room camera)",
    )
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--process-only", action="store_true")
    a = ap.parse_args()
    raw = DIR / "plate_raw.png"
    if not a.process_only and (a.force or not raw.exists()):
        layout = make_layout(Path(a.layout)) if a.layout else DIR / "plate_layout.png"
        if not layout.exists():
            sys.exit("no layout: pass --layout <still of the box room>")
        bt.load_key()
        if "OPENAI_API_KEY" not in os.environ:
            sys.exit("OPENAI_API_KEY is not set (services/negotiation/.env)")
        edit(PROMPT, layout, raw)
    if not raw.exists():
        sys.exit(f"missing {raw}")
    process()


if __name__ == "__main__":
    main()
