import unreal
lib=unreal.AnimationLibrary
hm=unreal.load_asset('/Game/PA250/Characters/WarHorse/SK_WarHorse')
sm=unreal.load_asset('/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER')
sa=unreal.load_asset('/Game/PA250/Characters/WarHorse/Rider/A_Continental_MountedIdle_V2')
def world(anim,mesh,bone,t):
 names=[]
 while str(bone)!='None':
  names.append(str(bone)); bone=mesh.get_bone_parent(bone)
 x=unreal.Transform()
 for name in names: x=x*lib.get_bone_pose_for_time(anim,name,t,False)
 return x
head=world(sa,sm,'head',0).translation
pelvis=world(sa,sm,'pelvis',0).translation
left=world(sa,sm,'calf_l',0).translation
right=world(sa,sm,'calf_r',0).translation
assert head.z > pelvis.z+45
assert left.y > pelvis.y+10 and right.y > pelvis.y+10, (left,right,pelvis)
unreal.log('RIDER_V2_HEAD_KNEES '+str([head,pelvis,left,right]))
for name in ('Idle','Walk'):
 anim=unreal.load_asset('/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_'+name)
 for i in range(5):
  t=anim.get_play_length()*i/5
  seat=world(anim,hm,'rider_seat',t)
  corrected=unreal.Transform(rotation=unreal.Rotator(roll=-90)) * seat
  up=corrected.transform_direction(unreal.Vector(0,0,1))
  unreal.log('RIDER_SEAT_UP '+name+' '+str(t)+' '+str(up))
  assert up.z > .9, (name,t,up)
unreal.log('RIDER_V2_VERIFY_OK')
