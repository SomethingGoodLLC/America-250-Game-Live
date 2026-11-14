"""Let the coat cloth take its faction colour (run once; safe to run again).

M_PA250_SoldierUniform is the pack's coat texture, desaturated, times CoatColor. The pack's
cloth is a near-black navy (about 0.014 linear), so every coat rendered black whatever the
colour, and only the pale facings showed it. This inserts a brightness curve (grey ^ 0.35)
between the desaturation and the tint: cloth 0.014 -> 0.22, facings 0.75 -> 0.90, folds and
seams keep their order. Game code and CoatColor values are unchanged.

  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=<repo>/tools/ue/patch_soldier_coat_curve.py -unattended -nullrhi -nosplash
"""

import unreal

EXPONENT = 0.35
mel = unreal.MaterialEditingLibrary
path = "/Game/PA250/Materials/M_PA250_SoldierUniform"
mat = unreal.load_asset(path)
mul = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
if not isinstance(mul, unreal.MaterialExpressionMultiply):
    raise RuntimeError(f"unexpected base colour node: {mul}")
inputs = [n for n in mel.get_inputs_for_material_expression(mat, mul) if n]
if any(isinstance(n, unreal.MaterialExpressionPower) for n in inputs):
    unreal.log("COAT_CURVE: already present; left unchanged")
else:
    grey = next(
        (n for n in inputs if isinstance(n, unreal.MaterialExpressionDesaturation)),
        None,
    )
    if not grey:
        raise RuntimeError("no desaturation node feeding the tint")
    power = mel.create_material_expression(
        mat, unreal.MaterialExpressionPower, 220, -160
    )
    power.set_editor_property("const_exponent", EXPONENT)
    assert mel.connect_material_expressions(grey, "", power, "Base")
    assert mel.connect_material_expressions(power, "", mul, "A")
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log(f"COAT_CURVE: inserted grey ^ {EXPONENT:.2f} into {path}")
