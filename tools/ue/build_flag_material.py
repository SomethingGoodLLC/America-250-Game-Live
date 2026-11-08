"""Create the project-owned two-sided flag material (texture parameter 'Flag'); never touches other assets."""
import unreal

path = '/Game/PA250/Materials/M_PA250_Flag'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.log('Flag material already exists; left unchanged')
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset('M_PA250_Flag', '/Game/PA250/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('two_sided', True)
    library = unreal.MaterialEditingLibrary
    tex = library.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property('parameter_name', 'Flag')
    tex.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/DefaultTexture'))
    rough = library.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 260)
    rough.set_editor_property('r', 0.85)
    assert library.connect_material_property(tex, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    assert library.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log('Created ' + path)
