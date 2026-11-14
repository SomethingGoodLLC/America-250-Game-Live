"""Let the coat cloth take its faction colour and keep the hat black (safe to run again).

M_PA250_SoldierUniform is the pack's coat texture, desaturated, times CoatColor. The pack's
cloth is a near-black navy (about 0.014 linear), so every coat rendered black whatever the
colour, and only the pale facings showed it. Two steps, each added once:

1. Cloth: grey' = lerp(grey, grey ^ 0.35, cloth), cloth = saturate(3 * (B - R) / max(B, 0.02)),
   so the navy wool is lifted before the tint and neutral or warm parts (leather, facings) are not.
2. Hat: the cocked hat is in the same material and the same navy, so it took the coat's colour
   (red tricorns on the re-shot class cards, 2026-11-14). Above HAT_Z cm in the mesh's own
   reference pose the surface is black felt instead of tinted cloth; nothing else in this
   material reaches that high.

Game code sets CoatColor; see ARFCharacter::ApplyFactionVisuals for the boost.

  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript \
      -script=<repo>/tools/ue/patch_soldier_coat_curve.py -unattended -nullrhi -nosplash
"""

import unreal

EXPONENT = 0.35
HAT_Z = (
    163.0,
    167.0,
)  # cm above the feet in the reference pose: the hat starts in this band
FELT = (0.012, 0.011, 0.010)
mel = unreal.MaterialEditingLibrary
path = "/Game/PA250/Materials/M_PA250_SoldierUniform"
mat = unreal.load_asset(path)
top = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
has_hat = isinstance(top, unreal.MaterialExpressionLinearInterpolate)
mul = top
if has_hat:  # the hat step is already in: the tint is its A input
    mul = next(
        n
        for n in mel.get_inputs_for_material_expression(mat, top)
        if isinstance(n, unreal.MaterialExpressionMultiply)
    )
if not isinstance(mul, unreal.MaterialExpressionMultiply):
    raise RuntimeError(f"unexpected base colour node: {mul}")


def feeds(node, cls):
    return next(
        (
            n
            for n in mel.get_inputs_for_material_expression(mat, node)
            if isinstance(n, cls)
        ),
        None,
    )


if feeds(mul, unreal.MaterialExpressionLinearInterpolate):
    unreal.log("COAT_CURVE: cloth-only curve already present; left unchanged")
else:
    power = feeds(mul, unreal.MaterialExpressionPower)
    grey = (
        feeds(power, unreal.MaterialExpressionDesaturation)
        if power
        else feeds(mul, unreal.MaterialExpressionDesaturation)
    )
    tex = feeds(grey, unreal.MaterialExpressionTextureSample) if grey else None
    if not grey or not tex:
        raise RuntimeError("no desaturated texture sample feeding the tint")
    if not power:
        power = mel.create_material_expression(
            mat, unreal.MaterialExpressionPower, 220, -160
        )
        power.set_editor_property("const_exponent", EXPONENT)
        assert mel.connect_material_expressions(grey, "", power, "Base")

    def chan(name, y):
        m = mel.create_material_expression(
            mat, unreal.MaterialExpressionComponentMask, -200, y
        )
        for k in "rgba":
            m.set_editor_property(k, k == name)
        assert mel.connect_material_expressions(tex, "RGB", m, "")
        return m

    r, b = chan("r", -520), chan("b", -440)
    diff = mel.create_material_expression(
        mat, unreal.MaterialExpressionSubtract, 0, -480
    )
    assert mel.connect_material_expressions(b, "", diff, "A")
    assert mel.connect_material_expressions(r, "", diff, "B")
    floor = mel.create_material_expression(mat, unreal.MaterialExpressionMax, 0, -400)
    floor.set_editor_property("const_b", 0.02)
    assert mel.connect_material_expressions(b, "", floor, "A")
    ratio = mel.create_material_expression(
        mat, unreal.MaterialExpressionDivide, 160, -460
    )
    assert mel.connect_material_expressions(diff, "", ratio, "A")
    assert mel.connect_material_expressions(floor, "", ratio, "B")
    gain = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, 300, -460
    )
    gain.set_editor_property("const_b", 3.0)
    assert mel.connect_material_expressions(ratio, "", gain, "A")
    cloth = mel.create_material_expression(
        mat, unreal.MaterialExpressionSaturate, 440, -460
    )
    assert mel.connect_material_expressions(gain, "", cloth, "")
    lerp = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, 580, -200
    )
    assert mel.connect_material_expressions(grey, "", lerp, "A")
    assert mel.connect_material_expressions(power, "", lerp, "B")
    assert mel.connect_material_expressions(cloth, "", lerp, "Alpha")
    assert mel.connect_material_expressions(lerp, "", mul, "A")
    unreal.log(
        f"COAT_CURVE: grey ^ {EXPONENT:.2f} now applies to the coat cloth only in {path}"
    )

if has_hat:
    # The first hat step fed the pre-skinned position straight to the pixel shader, where it
    # does not exist ("not available in the Pixel shader": the material fell back to the grey
    # default). Repair that graph in place: the height goes through a vertex interpolator.
    lo = feeds(
        feeds(
            feeds(top, unreal.MaterialExpressionSaturate),
            unreal.MaterialExpressionDivide,
        ),
        unreal.MaterialExpressionSubtract,
    )
    z = feeds(lo, unreal.MaterialExpressionComponentMask)
    if z:
        vi = mel.create_material_expression(
            mat, unreal.MaterialExpressionVertexInterpolator, 750, 280
        )
        assert mel.connect_material_expressions(z, "", vi, "")
        assert mel.connect_material_expressions(vi, "", lo, "A")
        unreal.log("COAT_CURVE: hat height now passes through a vertex interpolator")
    else:
        unreal.log("COAT_CURVE: black hat already present; left unchanged")
else:
    pos = mel.create_material_expression(
        mat, unreal.MaterialExpressionPreSkinnedPosition, 500, 200
    )
    z = mel.create_material_expression(
        mat, unreal.MaterialExpressionComponentMask, 680, 200
    )
    for k in "rgba":
        z.set_editor_property(k, k == "b")
    assert mel.connect_material_expressions(pos, "", z, "")
    lo = mel.create_material_expression(
        mat, unreal.MaterialExpressionSubtract, 820, 200
    )
    lo.set_editor_property("const_b", HAT_Z[0])
    # Pre-skinned position exists only in the vertex shader: interpolate the height to pixels.
    vi = mel.create_material_expression(
        mat, unreal.MaterialExpressionVertexInterpolator, 750, 280
    )
    assert mel.connect_material_expressions(z, "", vi, "")
    assert mel.connect_material_expressions(vi, "", lo, "A")
    band = mel.create_material_expression(
        mat, unreal.MaterialExpressionDivide, 960, 200
    )
    band.set_editor_property("const_b", HAT_Z[1] - HAT_Z[0])
    assert mel.connect_material_expressions(lo, "", band, "A")
    hat = mel.create_material_expression(
        mat, unreal.MaterialExpressionSaturate, 1100, 200
    )
    assert mel.connect_material_expressions(band, "", hat, "")
    felt = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, 1100, 360
    )
    felt.set_editor_property(
        "constant", unreal.LinearColor(FELT[0], FELT[1], FELT[2], 1.0)
    )
    final = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, 1260, 0
    )
    assert mel.connect_material_expressions(mul, "", final, "A")
    assert mel.connect_material_expressions(felt, "", final, "B")
    assert mel.connect_material_expressions(hat, "", final, "Alpha")
    assert mel.connect_material_property(
        final, "", unreal.MaterialProperty.MP_BASE_COLOR
    )
    unreal.log(f"COAT_CURVE: hat above {HAT_Z[0]:.0f} cm is black felt in {path}")

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
