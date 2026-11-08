"""Create project-owned tintable clothing materials; never save the Fab originals.

M_PA250_SoldierUniform - coat (mesh slot 1), from M_american_s_armor.
M_PA250_SoldierUnder   - waistcoat/breeches (mesh slot 0), from M_american_s_armor2skin.
Both: base colour -> desaturate -> x CoatColor.
"""
import unreal

MATERIALS = (
    ('M_PA250_SoldierUniform', '/Game/american_soldier/material/M_american_s_armor', (0.16, 0.28, 0.84)),
    ('M_PA250_SoldierUnder', '/Game/american_soldier/material/M_american_s_armor2skin', (0.5, 0.45, 0.35)),
)

for name, source_path, default in MATERIALS:
    path = '/Game/PA250/Materials/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log(name + ' already exists; left unchanged')
        continue
    source = unreal.load_asset(source_path)
    material = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, '/Game/PA250/Materials', source)
    library = unreal.MaterialEditingLibrary
    base = library.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    if not base:
        raise RuntimeError(name + ': source has no base color input')
    grey = library.create_material_expression(material, unreal.MaterialExpressionDesaturation, 100, 0)
    tint = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, 100, 160)
    tint.set_editor_property('parameter_name', 'CoatColor')
    tint.set_editor_property('default_value', unreal.LinearColor(default[0], default[1], default[2], 1))
    multiply = library.create_material_expression(material, unreal.MaterialExpressionMultiply, 350, 0)
    assert library.connect_material_expressions(base, '', grey, str(library.get_material_expression_input_names(grey)[0]))
    assert library.connect_material_expressions(grey, '', multiply, 'A')
    assert library.connect_material_expressions(tint, '', multiply, 'B')
    assert library.connect_material_property(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log('Created ' + path)
