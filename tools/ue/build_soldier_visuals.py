"""Create a project-owned tintable coat material; never save the Fab originals."""
import unreal

path = '/Game/PA250/Materials/M_PA250_SoldierUniform'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.log('Soldier coat already exists; left unchanged')
else:
    source = unreal.load_asset('/Game/american_soldier/material/M_american_s_armor')
    material = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
        'M_PA250_SoldierUniform', '/Game/PA250/Materials', source)
    library = unreal.MaterialEditingLibrary
    base = library.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    output = library.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_BASE_COLOR)
    if not base:
        raise RuntimeError('Coat material has no base color input')
    grey = library.create_material_expression(material, unreal.MaterialExpressionDesaturation, 100, 0)
    tint = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, 100, 160)
    tint.set_editor_property('parameter_name', 'CoatColor')
    tint.set_editor_property('default_value', unreal.LinearColor(0.16, 0.28, 0.84, 1))
    multiply = library.create_material_expression(material, unreal.MaterialExpressionMultiply, 350, 0)
    assert library.connect_material_expressions(base, '', grey, str(library.get_material_expression_input_names(grey)[0]))
    assert library.connect_material_expressions(grey, '', multiply, 'A')
    assert library.connect_material_expressions(tint, '', multiply, 'B')
    assert library.connect_material_property(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log('Created ' + path)
