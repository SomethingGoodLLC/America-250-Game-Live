"""Editor Python: capture fixed Lexington preview stills via SceneCapture2D.

  UnrealEditor-Cmd PlayAmerica250.uproject \\
    -run=pythonscript -script=Scripts/capture_lexington_preview.py \\
    -unattended -nosplash -AllowCommandletRendering

Env:
  PA250_PREVIEW_LABEL=before|after   (default: after)
"""
import os
import subprocess
import time

import unreal

LOG = unreal.log
LABEL = os.environ.get("PA250_PREVIEW_LABEL", "after").strip().lower()
PROJECT = unreal.SystemLibrary.get_project_directory()
PREVIEW_DIR = os.path.abspath(os.path.join(PROJECT, "../../docs/preview"))
MAP = "/Game/PA250/Maps/L_Lexington"

SHOTS = (
    ("spawn", unreal.Vector(-5800, 200, 280), unreal.Rotator(-12, 20, 0), 55.0),
    ("green", unreal.Vector(-200, -900, 240), unreal.Rotator(-10, -140, 0), 60.0),
    ("wide", unreal.Vector(800, 5800, 380), unreal.Rotator(-14, -100, 0), 70.0),
)


def convert_exr_named_png(path):
    """If UE wrote OpenEXR into a .png path, tonemap to a real PNG via temp venv helper."""
    # Detect EXR magic.
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
    except OSError:
        return False
    if magic != b"\x76\x2f\x31\x01":  # OpenEXR
        return os.path.isfile(path)
    py = "/tmp/pa250exr/bin/python"
    if not os.path.isfile(py):
        LOG("PA250 preview: missing /tmp/pa250exr for EXR convert")
        return False
    code = r"""
import sys, numpy as np, OpenEXR, Imath
from PIL import Image
path = sys.argv[1]
f = OpenEXR.InputFile(path)
dw = f.header()['dataWindow']
w = dw.max.x - dw.min.x + 1
h = dw.max.y - dw.min.y + 1
pt = Imath.PixelType(Imath.PixelType.FLOAT)
def ch(n):
    return np.frombuffer(f.channel(n, pt), dtype=np.float32).reshape(h, w)
rgb = np.stack([ch('R'), ch('G'), ch('B')], -1)
# Brighten very dark captures a bit for preview readability.
mx = float(rgb.max()) if rgb.size else 0.0
if mx > 0:
    rgb = rgb / mx
rgb = np.clip(rgb, 0, 1)
Image.fromarray((rgb * 255).astype(np.uint8), 'RGB').save(path, 'PNG')
"""
    r = subprocess.run([py, "-c", code, path], capture_output=True, text=True)
    if r.returncode != 0:
        LOG("PA250 preview EXR convert failed: " + (r.stderr or r.stdout or ""))
        return False
    return True


def main():
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
        raise RuntimeError("missing map " + MAP)
    if not les.load_level(MAP):
        raise RuntimeError("load_level failed: " + MAP)

    unreal.SystemLibrary.execute_console_command(None, "viewmode lit")
    unreal.SystemLibrary.execute_console_command(None, "r.SkyLight.RealTimeReflectionCapture 1")
    # Give SkyAtmosphere / Lumen / realtime skylight a moment.
    time.sleep(2.0)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    rt = unreal.RenderingLibrary.create_render_target2d(
        world,
        1280,
        720,
        unreal.TextureRenderTargetFormat.RTF_RGBA8,
    )

    written = 0
    for stem, loc, rot, fov in SHOTS:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SceneCapture2D, loc, rot
        )
        actor.set_actor_label("PreviewCapture_%s" % stem)
        comp = actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        comp.set_editor_property("texture_target", rt)
        comp.set_editor_property("fov_angle", fov)
        try:
            comp.set_editor_property(
                "capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
            )
        except Exception:
            pass
        for prop, val in (("capture_every_frame", False), ("capture_on_movement", False)):
            try:
                comp.set_editor_property(prop, val)
            except Exception:
                pass
        try:
            unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)
        except Exception:
            pass
        # Warm the viewpoint; capture twice so GI settles.
        for _ in range(3):
            time.sleep(0.4)
            comp.capture_scene()
        time.sleep(0.2)

        filename = "lexington_%s_%s.png" % (LABEL, stem)
        dest = os.path.join(PREVIEW_DIR, filename)
        ok = False
        try:
            ok = bool(
                unreal.RenderingLibrary.export_render_target(world, rt, PREVIEW_DIR, filename)
            )
        except Exception as exc:  # noqa: BLE001
            LOG("PA250 preview export_render_target: " + str(exc))
        if not (ok and os.path.isfile(dest)):
            try:
                opts = unreal.ImageWriteOptions()
                opts.set_editor_property("format", unreal.DesiredImageFormat.PNG)
                try:
                    opts.set_editor_property("overwrite_file", True)
                except Exception:
                    pass
                unreal.ImageWriteBlueprintLibrary.export_to_disk(rt, dest, opts)
                for _ in range(40):
                    if os.path.isfile(dest):
                        break
                    time.sleep(0.05)
            except Exception as exc:  # noqa: BLE001
                LOG("PA250 preview ImageWrite: " + str(exc))

        if os.path.isfile(dest) and convert_exr_named_png(dest):
            LOG("PA250 preview wrote " + dest)
            written += 1
        else:
            LOG("PA250 preview FAILED " + dest)

        unreal.EditorLevelLibrary.destroy_actor(actor)

    LOG("PA250 preview done label=%s count=%d" % (LABEL, written))


main()
