"""Create a prototype rider idle for SK_Mannequin, based on MM_Idle.

Run in UE 5.8 with the project's mannequin assets present. Attach the rider's
component to the horse's rider_seat bone; adjust yaw and seat offset in preview.
This is a blocking pose for iteration, not a final hand-animated riding cycle.
"""
import math
import unreal

DEST = '/Game/PA250/Characters/WarHorse/Rider/A_Rider_MountedIdle'
SOURCE = '/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle'
lib = unreal.EditorAssetLibrary
if lib.does_asset_exist(DEST):
    raise RuntimeError('Destination exists; preserve it and remove it manually to rebuild')
base = lib.load_asset(SOURCE)
if not base:
    raise RuntimeError('Missing mannequin idle')
asset = lib.duplicate_asset(SOURCE, DEST)
if not asset:
    raise RuntimeError('Could not duplicate idle')
controller = asset.get_editor_property('controller')
controller.open_bracket('Build mounted idle', False)
controller.set_frame_rate(unreal.FrameRate(30, 1), False)
controller.set_number_of_frames(unreal.FrameNumber(120), False)

# Quaternion multiplication keeps the original mannequin's local bone axes.
def mul(a, b):
    return unreal.Quat(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                       a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                       a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                       a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z)
def axis(degrees, x, y, z):
    h = math.radians(degrees)*0.5
    s = math.sin(h)
    return unreal.Quat(x*s, y*s, z*s, math.cos(h))

# Source transforms are local to each bone. Opposite signs account for the
# mirrored axes in the mannequin rig; use small periodic motion for breathing.
angles = {'thigh_l': -65, 'thigh_r': 65, 'calf_l': 55, 'calf_r': -55,
          'upperarm_l': -22, 'upperarm_r': 22, 'lowerarm_l': 25, 'lowerarm_r': -25}
for bone in ['root', 'pelvis'] + list(angles):
    pose = unreal.AnimationLibrary.get_bone_pose_for_frame(base, bone, 0, False)
    translations, rotations, scales = [], [], []
    for i in range(121):
        wave = math.sin(2*math.pi*i/120)
        pos = pose.translation
        if bone == 'root':
            # The source mannequin pelvis sits roughly 94 cm above its root.
            pos = unreal.Vector(pos.x, pos.y, pos.z - 94.0 + 0.6*wave)
        q = pose.rotation
        if bone in angles:
            q = mul(q, axis(angles[bone] + 1.5*wave, 0, 0, 1))
        elif bone == 'pelvis':
            q = mul(q, axis(1.0*wave, 1, 0, 0))
        translations.append(pos)
        rotations.append(q)
        scales.append(pose.scale3d)
    if not controller.set_bone_track_keys(bone, translations, rotations, scales, False):
        raise RuntimeError('Could not write bone track ' + bone)
controller.close_bracket(False)
lib.save_loaded_asset(asset)
assert asset.get_editor_property('skeleton') == base.get_editor_property('skeleton')
assert abs(asset.get_play_length()-4.0) < 0.01
unreal.log('MOUNTED_IDLE_OK ' + asset.get_path_name())
