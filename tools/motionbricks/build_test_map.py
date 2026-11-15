"""Create the reserved test map in Unreal Editor from a local export."""
import os
from pathlib import Path

import unreal


FOLDER = "/Game/Developers/MotionBricksEvaluation"
MAP = FOLDER + "/L_MotionBricksTraversal"
WALL_HEIGHT_CM = 76.0


def build():
    source = Path(os.environ["PA250_MB_MOTION"]).resolve(strict=True)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        raise RuntimeError(f"The reserved map already exists at {MAP}")
    meshes = {}
    for name in ("Manny", "Quinn"):
        path = "/Game/Characters/Mannequins/Meshes/SKM_" + name + "_Simple"
        meshes[name] = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(meshes[name], unreal.SkeletalMesh):
            raise RuntimeError(f"Missing mannequin mesh at {path}")
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not level.new_level(MAP):
        raise RuntimeError(f"Could not create {MAP}")
    clips = {}
    for name, mesh in meshes.items():
        clips[name] = unreal.MBImportLibrary.import_generated_motion(str(source), mesh, name, WALL_HEIGHT_CM)
        if not clips[name]:
            raise RuntimeError(f"The {name} MotionBricks import failed. See the Unreal log.")
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.MBTestGameMode.static_class())

    def spawn(actor_type, name, x=0, y=0, z=0, pitch=0, yaw=0):
        actor = actors.spawn_actor_from_class(actor_type.static_class(), unreal.Vector(x, y, z), unreal.Rotator(0, pitch, yaw))
        if not actor:
            raise RuntimeError(f"Could not spawn {name}")
        actor.set_actor_label(name)
        return actor

    course = spawn(unreal.MBTestCourse, "MotionBricks Course")
    height = WALL_HEIGHT_CM
    wall = course.get_editor_property("wall")
    wall.set_relative_location(unreal.Vector(0, 0, height / 2), False, False)
    wall.set_relative_scale3d(unreal.Vector(0.30, 0.70, height / 100))
    spawn(unreal.PlayerStart, "Approach Start", x=-clips["Manny"].get_editor_property("wall_distance") - 160, z=92)
    sun = spawn(unreal.DirectionalLight, "Key Light", z=500, pitch=-52, yaw=-38)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 4.0)
    fill = spawn(unreal.DirectionalLight, "Fill Light", z=450, pitch=-28, yaw=145)
    fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 1.0)
    if not level.save_current_level():
        raise RuntimeError(f"Could not save {MAP}")
    unreal.log(f"MB_MAP_CREATED {MAP} using {', '.join(clip.get_path_name() for clip in clips.values())}")


build()
