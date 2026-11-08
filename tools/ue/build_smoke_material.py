"""Create the project-owned musket smoke material M_PA250_Smoke (translucent, two-sided).

Colour and opacity come from vertex colour (set per puff by UPA250SmokeSubsystem); the puff is a
soft disc from the UVs, broken up by world-space noise so drifting smoke billows, with depth fade
so it never cuts hard into the ground.
"""
import unreal

path = '/Game/PA250/Materials/M_PA250_Smoke'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.log('Smoke material already exists; left unchanged')
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    m = tools.create_asset('M_PA250_Smoke', '/Game/PA250/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('two_sided', True)
    lib = unreal.MaterialEditingLibrary

    def node(cls, x, y):
        return lib.create_material_expression(m, cls, x, y)

    def link(a, a_out, b, b_in):
        assert lib.connect_material_expressions(a, a_out, b, b_in), (a, a_out, b, b_in)

    vc = node(unreal.MaterialExpressionVertexColor, -1200, 0)
    uv = node(unreal.MaterialExpressionTextureCoordinate, -1200, 200)
    centre = node(unreal.MaterialExpressionConstant2Vector, -1200, 320)
    centre.set_editor_property('r', 0.5)
    centre.set_editor_property('g', 0.5)
    dist = node(unreal.MaterialExpressionDistance, -1000, 240)
    link(uv, '', dist, 'A')
    link(centre, '', dist, 'B')
    twice = node(unreal.MaterialExpressionMultiply, -850, 240)
    twice.set_editor_property('const_b', 2.0)
    link(dist, '', twice, 'A')
    inv = node(unreal.MaterialExpressionOneMinus, -700, 240)
    link(twice, '', inv, '')
    sat = node(unreal.MaterialExpressionSaturate, -560, 240)
    link(inv, '', sat, '')
    soft = node(unreal.MaterialExpressionPower, -420, 240)
    soft.set_editor_property('const_exponent', 1.7)
    link(sat, '', soft, 'Base')

    wpos = node(unreal.MaterialExpressionWorldPosition, -1200, 480)
    scale = node(unreal.MaterialExpressionMultiply, -1000, 480)
    scale.set_editor_property('const_b', 0.004)
    link(wpos, '', scale, 'A')
    noise = node(unreal.MaterialExpressionNoise, -820, 480)
    noise.set_editor_property('output_min', 0.45)
    noise.set_editor_property('output_max', 1.0)
    noise.set_editor_property('levels', 4)
    noise_in = [str(n) for n in lib.get_material_expression_input_names(noise)]
    unreal.log('Noise inputs: %s' % noise_in)
    link(scale, '', noise, next(n for n in noise_in if 'osition' in n))

    shape = node(unreal.MaterialExpressionMultiply, -280, 300)
    link(soft, '', shape, 'A')
    link(noise, '', shape, 'B')
    alpha = node(unreal.MaterialExpressionMultiply, -150, 200)
    link(shape, '', alpha, 'A')
    link(vc, 'A', alpha, 'B')
    fade = node(unreal.MaterialExpressionDepthFade, 0, 200)
    fade.set_editor_property('fade_distance_default', 90.0)
    fade_in = [str(n) for n in lib.get_material_expression_input_names(fade)]
    link(alpha, '', fade, next(n for n in fade_in if 'pacity' in n))
    assert lib.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY)

    assert lib.connect_material_property(vc, '', unreal.MaterialProperty.MP_BASE_COLOR)
    glow = node(unreal.MaterialExpressionMultiply, -150, -120)
    glow.set_editor_property('const_b', 0.18)
    link(vc, '', glow, 'A')
    assert lib.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = node(unreal.MaterialExpressionConstant, -150, -220)
    rough.set_editor_property('r', 1.0)
    assert lib.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)

    lib.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    unreal.log('Created ' + path)
