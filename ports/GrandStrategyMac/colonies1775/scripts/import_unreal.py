import unreal, json, shutil, math
from pathlib import Path
root=Path(unreal.Paths.project_dir()).resolve().parent/'colonies1775'
out=root/'output';dest='/Game/Colonies1775'
manifest=json.load(open(out/'manifest.json'))
at=unreal.AssetToolsHelpers.get_asset_tools()
def import_png(name):
 task=unreal.AssetImportTask();task.filename=str(out/(name+'.png'));task.destination_path=dest;task.automated=True;task.save=True;task.replace_existing=True
 at.import_asset_tasks([task])
 return unreal.load_asset(dest+'/'+name)
def configure(tex,compression):
 tex.set_editor_property('compression_settings',compression)
 tex.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
 tex.set_editor_property('srgb',False)
 tex.set_editor_property('filter',unreal.TextureFilter.TF_NEAREST)
 tex.set_editor_property('never_stream',True)
 # Rebuild platform data now so the CPU read in ImportColonyMap and at runtime sees the new format.
 if hasattr(tex,'post_edit_change'):tex.post_edit_change()
 assert unreal.EditorAssetLibrary.save_loaded_asset(tex,False)
# Lookup texture: same settings as FMapEditorApp::CreateTexture, required by IsTextureValid (UserInterface = TC_EditorIcon).
lookup_tex=import_png('T_Colonies1775_Lookup')
configure(lookup_tex,unreal.TextureCompressionSettings.TC_EDITOR_ICON)
assert lookup_tex.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_EDITOR_ICON
unreal.log('COLONIES_LOOKUP_TEXTURE: TC_EDITOR_ICON, no mips, sRGB off, nearest, never stream, post_edit_change='+str(hasattr(lookup_tex,'post_edit_change')))
# Display texture: what players see. Ordinary color texture (sRGB, mipmapped, filtered) so borders stay smooth.
display_tex=import_png('T_Colonies1775_Display')
display_tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_DEFAULT)
display_tex.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
display_tex.set_editor_property('srgb',True)
display_tex.set_editor_property('filter',unreal.TextureFilter.TF_DEFAULT)
display_tex.set_editor_property('never_stream',True)
assert unreal.EditorAssetLibrary.save_loaded_asset(display_tex,False)
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Game', '/InteractiveMap'], True)
content=Path(unreal.Paths.project_content_dir()).resolve()/'Colonies1775'
for f in ['Lookup.json','MapData.json']:shutil.copyfile(out/f,content/f)
obj=unreal.ColoniesImportLibrary.import_colony_map(dest+'/DA_Colonies1775',lookup_tex,str(content/'Lookup.json'),str(content/'MapData.json'))
assert obj
unreal.EditorAssetLibrary.save_loaded_asset(obj)
unreal.log('COLONIES_IMPORT_SUCCESS: 13 colonies, 15 lookup entries, MapObject saved')

# Inspect the sample actor setup to make a separate colony level.
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level('/InteractiveMap/GSMap/Levels/WorldMapFlatCombined')
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 unreal.log('COLONY_ACTOR '+a.get_class().get_path_name()+' '+a.get_name()+' '+str(a.get_actor_location()))
 if isinstance(a,unreal.ClickableMap):
  for c in a.get_components_by_class(unreal.StaticMeshComponent):unreal.log('COLONY_MESH '+c.get_name()+' '+str(c.get_editor_property('static_mesh'))+' '+str(c.get_editor_property('relative_scale3d')))
# Colony-only material, unlit. Color comes from the display texture (colony colors + borders); the exact
# lookup texture drives the hover highlight. On this Mac the plugin's GPU recolor (ReplaceColorComputeShader ->
# DynamicTexture) arrives black, and its political materials blend whole-Earth mask/terrain textures, so
# neither can show this regional map. Plugin assets are not referenced or modified.
MEL=unreal.MaterialEditingLibrary
def make_material(name):
 path=dest+'/'+name
 m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else at.create_asset(name,dest,unreal.Material,unreal.MaterialFactoryNew())
 MEL.delete_all_material_expressions(m)
 m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
 return m
def texture_param(m,name,tex,sampler,y):
 e=MEL.create_material_expression(m,unreal.MaterialExpressionTextureSampleParameter2D,-700,y)
 e.set_editor_property('parameter_name',name);e.set_editor_property('texture',tex);e.set_editor_property('sampler_type',sampler)
 return e
mat=make_material('M_ColoniesProvinces')
disp=texture_param(mat,'DisplayTexture',display_tex,unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,-200)
look=texture_param(mat,'LookupTexture',lookup_tex,unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,100)
# The plugin sets ProvinceHighlighted from an FColor, i.e. converted with the exact sRGB curve. Decode the
# raw lookup bytes the same way so the match is exact, then lighten the hovered colony.
hl=MEL.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-700,400)
hl.set_editor_property('parameter_name','ProvinceHighlighted');hl.set_editor_property('default_value',unreal.LinearColor(0,0,0,0))
shade=MEL.create_material_expression(mat,unreal.MaterialExpressionCustom,-300,0)
shade.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
def custom_input(n):
 i=unreal.CustomInput();i.set_editor_property('input_name',n);return i
shade.set_editor_property('inputs',[custom_input(n) for n in ('Base','Lookup','Highlight')])
shade.set_editor_property('code','''float3 lin = lerp(Lookup / 12.92, pow((Lookup + 0.055) / 1.055, 2.4), step(0.04045, Lookup));
float3 d = abs(lin - Highlight.rgb);
float hovered = step(max(d.x, max(d.y, d.z)), 0.002);
return lerp(Base, saturate(Base * 1.3 + 0.07), hovered);''')
assert MEL.connect_material_expressions(disp,'RGB',shade,'Base')
assert MEL.connect_material_expressions(look,'RGB',shade,'Lookup')
assert MEL.connect_material_expressions(hl,'',shade,'Highlight')
assert MEL.connect_material_property(shade,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.recompile_material(mat)
assert unreal.EditorAssetLibrary.save_loaded_asset(mat,False)
# Flat frame around the map: hides the plugin's runtime wraparound copies and the empty background.
frame_mat=make_material('M_ColoniesFrame')
fc=MEL.create_material_expression(frame_mat,unreal.MaterialExpressionConstant3Vector,-300,0)
fc.set_editor_property('constant',unreal.LinearColor(0.006,0.014,0.022,1))  # deep navy, darker than the sea
assert MEL.connect_material_property(fc,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.recompile_material(frame_mat)
assert unreal.EditorAssetLibrary.save_loaded_asset(frame_mat,False)
mi=unreal.load_asset(dest+'/MI_ColoniesPolitical') if unreal.EditorAssetLibrary.does_asset_exist(dest+'/MI_ColoniesPolitical') else at.create_asset('MI_ColoniesPolitical',dest,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
if not mi:mi=unreal.load_asset(dest+'/MI_ColoniesPolitical')
MEL.set_material_instance_parent(mi,mat)
MEL.clear_all_material_instance_parameters(mi)
MEL.set_material_instance_texture_parameter_value(mi,'LookupTexture',lookup_tex)
MEL.set_material_instance_texture_parameter_value(mi,'DisplayTexture',display_tex)
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.StringMapVisualProperty)
bp=unreal.load_asset(dest+'/BP_ColonyColors') if unreal.EditorAssetLibrary.does_asset_exist(dest+'/BP_ColonyColors') else at.create_asset('BP_ColonyColors',dest,unreal.Blueprint,factory)
if not bp:bp=unreal.load_asset(dest+'/BP_ColonyColors')
cdo=unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('name','Name');cdo.set_editor_property('material_instance',mi)
cdo.set_editor_property('property_color_map',{r['name']:unreal.Color(*bytes.fromhex(r['color']),255) for r in manifest['regions']})
obj.set_editor_property('visual_properties',[bp.generated_class()])
for asset in [mi,bp,obj]:unreal.EditorAssetLibrary.save_loaded_asset(asset)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in actors.get_all_level_actors():
 if isinstance(a,unreal.ClickableMap):
  a.set_editor_property('map_asset',obj);a.set_editor_property('start_map_mode','Name')
  a.set_actor_label('Thirteen Colonies 1775')
  for c in a.get_components_by_class(unreal.StaticMeshComponent):
   if c.get_editor_property('static_mesh'):c.set_material(0,mi);map_mesh=c
  # Fit from measured mesh bounds: U (longitude) runs along X, V (latitude) along Y. Keep the sample's
  # Y size and narrow X so the equirectangular 19x19 degree texture shows east-west at cos(38.5 deg).
  sample_y=a.get_actor_scale3d().y
  a.set_actor_scale3d(unreal.Vector(1,1,1))
  _,unit_ext,_=unreal.SystemLibrary.get_component_bounds(map_mesh)
  a.set_actor_scale3d(unreal.Vector(math.cos(math.radians(38.5))*sample_y*unit_ext.y/unit_ext.x,sample_y,1))
  map_origin,map_ext,_=unreal.SystemLibrary.get_component_bounds(map_mesh)
  unreal.log('COLONIES_MAP_BOUNDS: unit extent '+str(unit_ext)+' scale '+str(a.get_actor_scale3d())+' origin '+str(map_origin)+' extent '+str(map_ext))
 elif isinstance(a,(unreal.TextRenderActor,unreal.VolumetricCloud,unreal.ExponentialHeightFog,unreal.StaticMeshActor)):
  actors.destroy_actor(a)
pawn_class=unreal.MapPawn
pawn=actors.spawn_actor_from_class(pawn_class,unreal.Vector(map_origin.x,map_origin.y,map_origin.z+50))
pawn.set_editor_property('auto_possess_player',unreal.AutoReceiveInput.PLAYER0)
for c in pawn.get_components_by_class(unreal.PrimitiveComponent):
 c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
 c.set_editor_property('generate_overlap_events',False)
for a in actors.get_all_level_actors():
 if isinstance(a,unreal.ClickableMap):
  for c in a.get_components_by_class(unreal.BoxComponent):
   c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
   c.set_editor_property('generate_overlap_events',False)
# Four non-colliding frame planes just above the map surface, framing it on every side.
plane=unreal.load_asset('/Engine/BasicShapes/Plane')  # 100 x 100 units, facing +Z
# Overlap the map edge by 8 units (0.5% of it) so no sliver of the plugin's wraparound copy shows.
far=50000.0;ex,ey=map_ext.x-8,map_ext.y-8;z=map_origin.z+map_ext.z+1
for label,cx,cy,sx,sy in [('Left',-(ex+far/2),0,far,2*(ey+far)),('Right',ex+far/2,0,far,2*(ey+far)),
                          ('North',0,-(ey+far/2),2*ex,far),('South',0,ey+far/2,2*ex,far)]:
 f=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(map_origin.x+cx,map_origin.y+cy,z))
 f.set_actor_label('Colonies Frame '+label);f.set_actor_scale3d(unreal.Vector(sx/100,sy/100,1))
 smc=f.static_mesh_component;smc.set_static_mesh(plane);smc.set_material(0,frame_mat)
 smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);smc.set_editor_property('cast_shadow',False)
# True colors for the unlit map: fixed exposure, no filmic tone curve, no bloom; a light vignette.
for a in actors.get_all_level_actors():
 if isinstance(a,unreal.PostProcessVolume):actors.destroy_actor(a)
ppv=actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector(0,0,0))
ppv.set_actor_label('Colonies Post Process');ppv.set_editor_property('unbound',True);ppv.set_editor_property('priority',10.0)
pps=ppv.get_editor_property('settings')
for k,v in [('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('auto_exposure_apply_physical_camera_exposure',False),
            ('auto_exposure_bias',0.0),('tone_curve_amount',0.0),('bloom_intensity',0.0),('vignette_intensity',0.25)]:
 pps.set_editor_property('override_'+k,True);pps.set_editor_property(k,v)
ppv.set_editor_property('settings',pps)
# Top-down camera: distance that shows the whole map with a 5% margin in a 1280x900 window. The engine keeps
# the vertical FOV (AspectRatio_MaintainYFOV): FOV 60 is horizontal at the 16:9 reference aspect.
fov=60.0;aspect=1280/900
tan_v=math.tan(math.radians(fov/2))/(16/9);tan_h=tan_v*aspect
arm=1.05*max(map_ext.x/tan_h,map_ext.y/tan_v)
pawn.set_editor_property('zoom_limit',unreal.Vector2D(arm*0.1,arm*1.25))
unreal.log('COLONIES_CAMERA: arm '+str(arm)+' pawn '+str(pawn.get_actor_location()))
for c in pawn.get_components_by_class(unreal.SpringArmComponent):
 c.set_editor_property('target_arm_length',arm)
 c.set_editor_property('relative_rotation',unreal.Rotator(pitch=-90,yaw=-90,roll=0))
 c.set_editor_property('do_collision_test',False)
 c.set_editor_property('use_pawn_control_rotation',False)
for c in pawn.get_components_by_class(unreal.CameraComponent):c.set_editor_property('field_of_view',fov)
assert unreal.EditorLoadingAndSavingUtils.save_map(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),dest+'/L_Colonies1775')
unreal.log('COLONIES_LEVEL_SUCCESS: /Game/Colonies1775/L_Colonies1775')
