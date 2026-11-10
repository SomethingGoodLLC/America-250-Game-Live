"""Build experimental mounted idle and right slash for the American soldier rig.

Run with UnrealEditor-Cmd -run=pythonscript and this script's absolute path.
Existing output is preserved. Verify saddle alignment and sword grip visually.
"""
import math
import unreal

SRC = '/Game/american_soldier/demo/ThirdPersonIdle'
DEST = '/Game/PA250/Characters/WarHorse/Rider'
lib = unreal.EditorAssetLibrary
base = lib.load_asset(SRC)
if not base:
    raise RuntimeError('American soldier idle not found')

def mul(a, b):
    return unreal.Quat(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                       a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                       a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                       a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z)

def rot(angle, axis):
    s = math.sin(math.radians(angle)/2)
    return unreal.Quat(axis[0]*s, axis[1]*s, axis[2]*s, math.cos(math.radians(angle)/2))

def slash_weight(t):
    # Anticipation, cut, recovery; starts and ends in the same pose.
    if t < .25:
        return -0.35 * (t / .25)
    if t < .55:
        return -.35 + 1.35 * ((t-.25)/.30)
    return 1 - (t-.55)/.45

def build(name, frames, slash):
    path = DEST + '/' + name
    if lib.does_asset_exist(path):
        raise RuntimeError('Output exists: ' + path)
    anim = lib.duplicate_asset(SRC, path)
    if not anim:
        raise RuntimeError('Animation duplicate failed')
    ctl = anim.get_editor_property('controller')
    ctl.open_bracket('Mounted soldier', False)
    ctl.set_number_of_frames(unreal.FrameNumber(frames), False)
    angles = {'thigh_l': -65, 'thigh_r': -65, 'calf_l': 55, 'calf_r': 55,
              'upperarm_l': -22, 'upperarm_r': 22, 'lowerarm_l': 25, 'lowerarm_r': -25}
    bones = ['root','pelvis','spine_02','hand_r'] + list(angles)
    for bone in bones:
        pose = unreal.AnimationLibrary.get_bone_pose_for_frame(base, bone, 0, False)
        positions, rotations, scales = [], [], []
        for i in range(frames+1):
            t = i/frames
            wave = math.sin(t * 2 * math.pi)
            cut = slash_weight(t) if slash else 0
            pos = pose.translation
            if bone == 'root':
                pos = unreal.Vector(pos.x, pos.y, pos.z - 92.26 + (0.5*wave if not slash else 0))
            q = pose.rotation
            if bone in angles:
                q = mul(q, rot(angles[bone] + (wave if not slash else 0), (0,0,1)))
            if bone == 'spine_02': q = mul(q, rot(15*cut, (1,0,0)))
            if bone == 'upperarm_r': q = mul(q, rot(-85*cut, (0,1,0)))
            if bone == 'lowerarm_r': q = mul(q, rot(35*cut, (0,0,1)))
            if bone == 'hand_r': q = mul(q, rot(-25*cut, (1,0,0)))
            positions.append(pos); rotations.append(q); scales.append(pose.scale3d)
        if not ctl.set_bone_track_keys(bone, positions, rotations, scales, False):
            raise RuntimeError('Missing track ' + bone)
    ctl.close_bracket(False)
    lib.save_loaded_asset(anim)
    assert anim.get_editor_property('skeleton') == base.get_editor_property('skeleton')
    assert abs(anim.get_play_length() - frames/30) < .1
    unreal.log('MOUNTED_SOLDIER_OK ' + path)

build('A_Continental_MountedIdle_V2', 120, False)
build('A_Continental_MountedSlash_V2', 36, True)
