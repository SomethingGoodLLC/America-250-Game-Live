"""Builds /Game/PA250/Materials/M_PA250_Cannon: the project's own material for the purchased field gun.

The Fab pack "CANNON - GAME READY" (PTM-MODELS, Content/ThirdParty/PTM_Cannon) ships its five textures
(albedo, normal, gloss, metalness, ao) but its material instance, an FBX legacy Phong import, binds none
of them, so the gun renders flat grey. This material samples the pack's textures as they are; nothing in
the pack is changed. The pack flags its gloss, metalness and ao maps as sRGB colour, so they are sampled
as colour (a sampler must match its texture) and brought back to their stored values with a 1/2.2 power.
Roughness is one minus gloss. APA250FieldGun applies the material to the mesh when it stands a gun.

Run (editor closed):
  UnrealEditor-Cmd <uproject> -run=pythonscript -script=Scripts/build_cannon_material.py -unattended -nullrhi -nosplash
Verify in a fresh process: PA250_CANNON_MATERIAL_VERIFY_ONLY=1 (same command).
"""
import os
import unreal

PACK = '/Game/ThirdParty/PTM_Cannon'
DEST = '/Game/PA250/Materials'
NAME = 'M_PA250_Cannon'
PATH = DEST + '/' + NAME
VERIFY_ONLY = os.environ.get('PA250_CANNON_MATERIAL_VERIFY_ONLY') == '1'
mel = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def texture(name):
    asset = unreal.load_asset(PACK + '/' + name)
    if not isinstance(asset, unreal.Texture2D):
        raise RuntimeError('cannon material: pack texture %s/%s is missing' % (PACK, name))
    return asset


def verify():
    mat = unreal.load_asset(PATH)
    ok = isinstance(mat, unreal.Material)
    wired = {}
    if ok:
        for prop in ('MP_BASE_COLOR', 'MP_NORMAL', 'MP_ROUGHNESS', 'MP_METALLIC', 'MP_AMBIENT_OCCLUSION'):
            node = mel.get_material_property_input_node(mat, getattr(MP, prop))
            wired[prop] = type(node).__name__ if node else None
            ok = ok and node is not None
        # Walk back from each input to the textures it samples (get_used_textures is empty without a
        # renderer, and commandlets run -nullrhi).
        found = set()

        def walk(node, depth=0):
            if node is None or depth > 8:
                return
            if isinstance(node, unreal.MaterialExpressionTextureSample):
                tex = node.get_editor_property('texture')
                if tex:
                    found.add(tex.get_name())
            for upstream in mel.get_inputs_for_material_expression(mat, node):
                walk(upstream, depth + 1)

        for prop in ('MP_BASE_COLOR', 'MP_NORMAL', 'MP_ROUGHNESS', 'MP_METALLIC', 'MP_AMBIENT_OCCLUSION'):
            walk(mel.get_material_property_input_node(mat, getattr(MP, prop)))
        used = sorted(found)
        ok = ok and used == ['Cannon_albedo', 'Cannon_ao', 'Cannon_gloss', 'Cannon_metalness', 'Cannon_normal']
        unreal.log('PA250 cannon material: inputs %s, textures %s' % (wired, used))
    unreal.log('PA250 cannon material verify: %s' % ('PASS' if ok else 'FAIL'))
    return ok


def build():
    albedo, normal = texture('Cannon_albedo'), texture('Cannon_normal')
    gloss, metal, ao = texture('Cannon_gloss'), texture('Cannon_metalness'), texture('Cannon_ao')
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        mat = unreal.load_asset(PATH)
        mel.delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())

    def sample(tex, y, sampler):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -900, y)
        node.set_editor_property('texture', tex)
        node.set_editor_property('sampler_type', sampler)
        return node

    def stored_value(tex, y):
        # An sRGB-flagged data map: sample as colour, then undo the decode.
        srgb = tex.get_editor_property('srgb')
        node = sample(tex, y, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if not srgb:
            return node, 'R'
        power = mel.create_material_expression(mat, unreal.MaterialExpressionPower, -600, y)
        power.set_editor_property('const_exponent', 1.0 / 2.2)
        mel.connect_material_expressions(node, 'R', power, 'Base')
        return power, ''

    base = sample(albedo, -400, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_property(base, 'RGB', MP.MP_BASE_COLOR)
    nrm = sample(normal, -150, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mel.connect_material_property(nrm, 'RGB', MP.MP_NORMAL)
    g, g_out = stored_value(gloss, 100)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -350, 100)
    mel.connect_material_expressions(g, g_out, rough, '')
    mel.connect_material_property(rough, '', MP.MP_ROUGHNESS)
    m, m_out = stored_value(metal, 350)
    mel.connect_material_property(m, m_out, MP.MP_METALLIC)
    a, a_out = stored_value(ao, 600)
    mel.connect_material_property(a, a_out, MP.MP_AMBIENT_OCCLUSION)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    unreal.log('PA250 cannon material: built %s (gloss sRGB %s, metalness sRGB %s, ao sRGB %s)' % (
        PATH, gloss.get_editor_property('srgb'), metal.get_editor_property('srgb'), ao.get_editor_property('srgb')))


if not VERIFY_ONLY:
    build()
verify()
