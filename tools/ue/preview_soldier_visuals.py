"""Render two faction pawns in Lexington without saving the level."""
from pathlib import Path

import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
levels.load_level('/Game/PA250/Maps/L_Lexington')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
character_class = unreal.load_class(None, '/Script/PlayAmerica250.RFCharacter')
weapon_class = unreal.load_class(None, '/Script/PlayAmerica250.MusketWeapon')
for team, y in [('british', -105), ('continental', 105)]:
    soldier = actors.spawn_actor_from_class(character_class, unreal.Vector(0, y, 90), unreal.Rotator())
    soldier.set_team(team, 'line_infantry')
    mesh = soldier.get_component_by_class(unreal.SkeletalMeshComponent)
    mesh.set_update_animation_in_editor(True)
    mesh.play_animation(unreal.load_asset('/Game/american_soldier/demo/ThirdPersonIdle'), True)
    weapon = actors.spawn_actor_from_class(weapon_class, unreal.Vector())
    weapon.attach_to_component(mesh, 'hand_r', unreal.AttachmentRule.SNAP_TO_TARGET,
                               unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.KEEP_WORLD, False)
    weapon.set_actor_relative_rotation(unreal.Rotator(), False, False)
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(460, -440, 225))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 110)), False)
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(38)
unreal.EditorLevelLibrary.pilot_level_actor(camera)
frames = 0
handle = None
screenshot_task = None
requesting_screenshot = False

def tick(delta):
    global frames, screenshot_task, requesting_screenshot
    frames += 1
    # Screenshot preparation pumps Slate recursively; never quit inside that call.
    if frames >= 120 and screenshot_task is None and not requesting_screenshot:
        requesting_screenshot = True
        output = str(Path(unreal.Paths.project_saved_dir()).resolve() / 'PA250/PA250_FabSoldiers.png')
        screenshot_task = unreal.AutomationLibrary.take_high_res_screenshot(1280, 900, output, camera)
    if screenshot_task is not None and screenshot_task.is_task_done():
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)

handle = unreal.register_slate_post_tick_callback(tick)
