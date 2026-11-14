"""Import the battlefield kit (tools/art/battlefield_kit.py + build_battlefield_textures.py)
into /Game/PA250/Battlefield, or verify it in a fresh editor process.

  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript \
      -script=Scripts/import_battlefield_kit.py -unattended -nullrhi -nosplash
  PA250_BATTLEFIELD_VERIFY_ONLY=1 ...   read-only check of the saved assets
  PA250_BATTLEFIELD_ONLY=backdrop ...   import only the horizon strip and the pasture
                                        variation map (tools/art/build_battlefield_backdrop.py)

Source: SourceArt/battlefield/{textures/*.png, ue5/SM_*.fbx, kit_manifest.txt}.
Creates T_<Name>_{BaseColor,Normal,Roughness}, M_<Name> (one per surface, shared by every
mesh) and SM_<Asset> with its UCX hulls. Only assets carrying this importer's ownership tag
are replaced; anything else at the destination stops the run. No levels are saved.
Report: Saved/PA250/battlefield_kit_{import,verify}.json
"""
import json
import os
import traceback

import unreal

PROJECT = unreal.SystemLibrary.get_project_directory()
SOURCE = os.path.join(PROJECT, "SourceArt", "battlefield")
DEST = "/Game/PA250/Battlefield"
OWNER = "PA250_BattlefieldKit_v1"
TAG = "PA250_Importer"
VERIFY_ONLY = os.environ.get("PA250_BATTLEFIELD_VERIFY_ONLY") == "1"
ONLY = os.environ.get("PA250_BATTLEFIELD_ONLY", "")
# Single textures beside the PBR sets: name -> (sRGB, has a cut-out alpha)
BACKDROP = {"HorizonTreeline": (True, True), "PastureMacro": (False, False)}
REPORT = os.path.join(PROJECT, "Saved", "PA250", "battlefield_kit_%s.json" % ("verify" if VERIFY_ONLY else "import"))
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
# Untextured surfaces: (sRGB-linear base colour, roughness, metallic, specular)
FLAT = {
    "Glass": ((0.035, 0.045, 0.05), 0.12, 0.0, 0.5),
    "Iron": ((0.05, 0.045, 0.04), 0.55, 0.8, 0.5),
    "DarkVoid": ((0.02, 0.018, 0.016), 0.95, 0.0, 0.1),
}


def log(msg):
    unreal.log("BATTLEFIELD_KIT: " + msg)


def owned_or_none(path):
    if not EAL.does_asset_exist(path):
        return None
    ob = EAL.load_asset(path)
    if EAL.get_metadata_tag(ob, TAG) != OWNER:
        raise RuntimeError("Refusing to touch an asset this importer does not own: " + path)
    return ob


def own(ob):
    if not ob:
        raise RuntimeError("asset creation returned None")
    EAL.set_metadata_tag(ob, TAG, OWNER)
    return ob


def save(ob):
    if not EAL.save_loaded_asset(ob, only_if_is_dirty=False):
        raise RuntimeError("could not save " + ob.get_path_name())


def import_file(filename, folder, name, options=None):
    owned_or_none(folder + "/" + name)  # raises when a foreign asset is in the way
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    if options:
        task.options = options
    TOOLS.import_asset_tasks([task])
    ob = EAL.load_asset(folder + "/" + name)
    if not ob:
        raise RuntimeError("import produced nothing for %s (%s)" % (filename, list(task.imported_object_paths)))
    return own(ob)


def texture_sets():
    tex_dir = os.path.join(SOURCE, "textures")
    names = sorted(f[:-len("_BaseColor.png")] for f in os.listdir(tex_dir) if f.endswith("_BaseColor.png"))
    for n in names:
        for s in ("Normal", "Roughness"):
            if not os.path.exists(os.path.join(tex_dir, "%s_%s.png" % (n, s))):
                raise RuntimeError("incomplete texture set: %s_%s.png" % (n, s))
    return names


def import_textures(names):
    out = {}
    for n in names:
        for suffix in ("BaseColor", "Normal", "Roughness"):
            stem = "%s_%s" % (n, suffix)
            tex = import_file(os.path.join(SOURCE, "textures", stem + ".png"), DEST + "/Textures", "T_" + stem)
            if suffix == "Normal":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
                tex.set_editor_property("flip_green_channel", True)  # authored OpenGL +Y
            elif suffix == "Roughness":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("srgb", False)
            else:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
                tex.set_editor_property("srgb", True)
            tex.set_editor_property("virtual_texture_streaming", False)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
            save(tex)
            out[stem] = tex
    log("textures: %d sets" % len(names))
    return out


def import_backdrop():
    """T_HorizonTreeline (wood-edge photographs joined into one wrapping strip, alpha = the
    cut-out sky) and T_PastureMacro (linear variation map, 0.5 = no change). Skipped, with a
    log line, when tools/art/build_battlefield_backdrop.py has not been run."""
    done = []
    for name, (srgb, alpha) in sorted(BACKDROP.items()):
        src = os.path.join(SOURCE, "textures", name + ".png")
        if not os.path.exists(src):
            log("backdrop: %s.png not built; skipped" % name)
            continue
        tex = import_file(src, DEST + "/Textures", "T_" + name)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", srgb)
        tex.set_editor_property("virtual_texture_streaming", False)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        if alpha:
            # Keep the cut-out's coverage in the small mips, or the far treeline thins to twigs.
            tex.set_editor_property("alpha_coverage_thresholds", unreal.Vector4(0.0, 0.0, 0.0, 0.4))
            tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        save(tex)
        done.append("%s %dx%d" % (tex.get_name(), tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    log("backdrop: " + (", ".join(done) or "nothing imported"))


def _const(mat, value, prop, y):
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -250, y)
    node.set_editor_property("r", value)
    if not MEL.connect_material_property(node, "", prop):
        raise RuntimeError("could not connect %s" % prop)


def make_materials(names, textures):
    out = {}
    for name in list(names) + list(FLAT):
        path = DEST + "/Materials/M_" + name
        mat = owned_or_none(path)
        if not mat:
            mat = own(TOOLS.create_asset("M_" + name, DEST + "/Materials", unreal.Material, unreal.MaterialFactoryNew()))
        MEL.delete_all_material_expressions(mat)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        mat.set_editor_property("used_with_instanced_static_meshes", True)
        if name in FLAT:
            rgb, rough, metal, spec = FLAT[name]
            node = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -550, 0)
            node.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
            _const(mat, rough, unreal.MaterialProperty.MP_ROUGHNESS, 200)
            _const(mat, metal, unreal.MaterialProperty.MP_METALLIC, 350)
            _const(mat, spec, unreal.MaterialProperty.MP_SPECULAR, 500)
        else:
            for i, (suffix, sampler, pin, prop) in enumerate((
                ("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "RGB", unreal.MaterialProperty.MP_BASE_COLOR),
                ("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "R", unreal.MaterialProperty.MP_ROUGHNESS),
                ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "RGB", unreal.MaterialProperty.MP_NORMAL),
            )):
                node = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, i * 280)
                node.set_editor_property("texture", textures["%s_%s" % (name, suffix)])
                node.set_editor_property("sampler_type", sampler)
                if not MEL.connect_material_property(node, pin, prop):
                    raise RuntimeError("could not connect %s.%s" % (name, suffix))
        MEL.recompile_material(mat)
        save(mat)
        out[name] = mat
    log("materials: %d" % len(out))
    return out


def _world_xy(mat, tile_cm, y):
    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1300, y)
    xy = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1150, y)
    for k, v in (("r", True), ("g", True), ("b", False), ("a", False)):
        xy.set_editor_property(k, v)
    MEL.connect_material_expressions(wp, "", xy, "")
    div = MEL.create_material_expression(mat, unreal.MaterialExpressionDivide, -1000, y)
    div.set_editor_property("const_b", float(tile_cm))
    MEL.connect_material_expressions(xy, "", div, "A")
    return div


def make_river_materials(textures):
    """M_Water (still river water: dark, glossy, two ripple layers drifting on world XY) and
    M_CreekBank (wet trampled margin, world-aligned). Used by the Concord crossing, whose
    pieces are spawned at run time in APA250BattleGameMode::SpawnCreekBridge."""
    ripple = import_file(os.path.join(SOURCE, "textures", "WaterRipple_Normal.png"), DEST + "/Textures", "T_WaterRipple_Normal")
    ripple.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    ripple.set_editor_property("srgb", False)
    ripple.set_editor_property("flip_green_channel", True)
    save(ripple)
    out = {}
    for name in ("Water", "CreekBank"):
        path = DEST + "/Materials/M_" + name
        mat = owned_or_none(path) or own(TOOLS.create_asset("M_" + name, DEST + "/Materials", unreal.Material, unreal.MaterialFactoryNew()))
        MEL.delete_all_material_expressions(mat)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        if name == "Water":
            rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
            rgb.set_editor_property("constant", unreal.LinearColor(0.016, 0.026, 0.02, 1.0))  # peaty meadow river
            MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
            _const(mat, 0.05, unreal.MaterialProperty.MP_ROUGHNESS, 200)
            _const(mat, 0.0, unreal.MaterialProperty.MP_METALLIC, 350)
            _const(mat, 0.6, unreal.MaterialProperty.MP_SPECULAR, 500)
            samples = []
            for i, (tile, sx, sy) in enumerate(((420.0, 0.012, 0.004), (910.0, -0.006, 0.009))):
                uv = _world_xy(mat, tile, 700 + i * 260)
                pan = MEL.create_material_expression(mat, unreal.MaterialExpressionPanner, -850, 700 + i * 260)
                pan.set_editor_property("speed_x", sx)
                pan.set_editor_property("speed_y", sy)
                MEL.connect_material_expressions(uv, "", pan, "Coordinate")
                smp = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -650, 700 + i * 260)
                smp.set_editor_property("texture", ripple)
                smp.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
                MEL.connect_material_expressions(pan, "", smp, "UVs")
                samples.append(smp)
            add = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -400, 800)
            MEL.connect_material_expressions(samples[0], "RGB", add, "A")
            MEL.connect_material_expressions(samples[1], "RGB", add, "B")
            flat = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 800)
            k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 950)
            k.set_editor_property("constant", unreal.LinearColor(0.35, 0.35, 0.5, 1.0))  # halves Z, flattens XY
            MEL.connect_material_expressions(add, "", flat, "A")
            MEL.connect_material_expressions(k, "", flat, "B")
            nrm = MEL.create_material_expression(mat, unreal.MaterialExpressionNormalize, -100, 800)
            MEL.connect_material_expressions(flat, "", nrm, "VectorInput")
            MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
        else:
            uv = _world_xy(mat, 340.0, 0)
            base = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 0)
            base.set_editor_property("texture", textures["TrampledTurf_BaseColor"])
            MEL.connect_material_expressions(uv, "", base, "UVs")
            dark = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 0)
            dark.set_editor_property("const_b", 0.78)  # damp, but close to the turf beside it
            MEL.connect_material_expressions(base, "RGB", dark, "A")
            MEL.connect_material_property(dark, "", unreal.MaterialProperty.MP_BASE_COLOR)
            nm = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 300)
            nm.set_editor_property("texture", textures["TrampledTurf_Normal"])
            nm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            MEL.connect_material_expressions(uv, "", nm, "UVs")
            MEL.connect_material_property(nm, "RGB", unreal.MaterialProperty.MP_NORMAL)
            _const(mat, 0.6, unreal.MaterialProperty.MP_ROUGHNESS, 500)
        MEL.recompile_material(mat)
        save(mat)
        out[name] = mat
    log("river materials: %d" % len(out))
    return out


def import_meshes(manifest, materials):
    out = {}
    for asset_name, info in sorted(manifest.items()):
        name = info["mesh"]
        opts = unreal.FbxImportUI()
        opts.set_editor_property("automated_import_should_detect_type", False)
        opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        opts.set_editor_property("import_mesh", True)
        opts.set_editor_property("import_as_skeletal", False)
        opts.set_editor_property("import_materials", False)
        opts.set_editor_property("import_textures", False)
        opts.set_editor_property("import_animations", False)
        data = opts.static_mesh_import_data
        for key, value in dict(combine_meshes=False, auto_generate_collision=False, one_convex_hull_per_ucx=True,
                               build_nanite=False, generate_lightmap_u_vs=False, convert_scene=True,
                               convert_scene_unit=True, force_front_x_axis=False, import_uniform_scale=1.0,
                               remove_degenerates=False,
                               normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():
            data.set_editor_property(key, value)
        mesh = import_file(os.path.join(SOURCE, "ue5", name + ".fbx"), DEST + "/Meshes", name, opts)
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            keys = [str(slot.get_editor_property("material_slot_name")), str(slot.get_editor_property("imported_material_slot_name"))]
            key = next((k for k in keys if k in materials), None)
            if key is None:
                raise RuntimeError("unknown material slot %s on %s" % (keys, name))
            mesh.set_material(index, materials[key])
        mesh.get_editor_property("body_setup").set_editor_property(
            "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        save(mesh)
        out[name] = mesh
    log("meshes: %d" % len(out))
    return out


def verify(manifest, names):
    report = {"status": "verifying", "engine": unreal.SystemLibrary.get_engine_version(), "destination": DEST,
              "mode": "fresh_process_verification" if VERIFY_ONLY else "import", "meshes": {}, "materials": {}, "textures": {}}
    problems = []
    for n in names:
        for suffix in ("BaseColor", "Normal", "Roughness"):
            tex = owned_or_none("%s/Textures/T_%s_%s" % (DEST, n, suffix))
            if not tex:
                problems.append("missing texture %s_%s" % (n, suffix))
                continue
            srgb = tex.get_editor_property("srgb")
            comp = tex.get_editor_property("compression_settings")
            if srgb != (suffix == "BaseColor"):
                problems.append("%s_%s sRGB flag wrong" % (n, suffix))
            if suffix == "Normal" and (not tex.get_editor_property("flip_green_channel")
                                       or comp != unreal.TextureCompressionSettings.TC_NORMALMAP):
                problems.append("%s normal map settings wrong" % n)
            report["textures"][tex.get_name()] = {"srgb": srgb, "compression": str(comp),
                                                  "size": [tex.blueprint_get_size_x(), tex.blueprint_get_size_y()]}
    for n in list(names) + list(FLAT):
        mat = owned_or_none(DEST + "/Materials/M_" + n)
        if not mat:
            problems.append("missing material " + n)
            continue
        need = [unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_ROUGHNESS]
        if n not in FLAT:
            need.append(unreal.MaterialProperty.MP_NORMAL)
        if not all(MEL.get_material_property_input_node(mat, p) for p in need):
            problems.append("material %s is missing a PBR input" % n)
        report["materials"]["M_" + n] = "PBR inputs connected"
    for n in ("Water", "CreekBank"):
        mat = owned_or_none(DEST + "/Materials/M_" + n)
        if not mat or not MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_NORMAL):
            problems.append("river material %s missing or without a normal" % n)
        else:
            report["materials"]["M_" + n] = "base colour, roughness, normal connected"
    for asset_name, info in sorted(manifest.items()):
        mesh = owned_or_none(DEST + "/Meshes/" + info["mesh"])
        if not mesh:
            problems.append("missing mesh " + info["mesh"])
            continue
        box = mesh.get_bounding_box()
        # Blender (x, y, z) m -> Unreal (100x, -100y, 100z) cm
        want_min = [100 * info["min_m"][0], -100 * info["max_m"][1], 100 * info["min_m"][2]]
        want_max = [100 * info["max_m"][0], -100 * info["min_m"][1], 100 * info["max_m"][2]]
        got_min, got_max = [box.min.x, box.min.y, box.min.z], [box.max.x, box.max.y, box.max.z]
        err = max(abs(a - b) for a, b in zip(got_min + got_max, want_min + want_max))
        tris = mesh.get_num_triangles(0)
        geom = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
        hulls = len(geom.get_editor_property("convex_elems"))
        slots = [s.material_interface.get_path_name() if s.material_interface else None for s in mesh.static_materials]
        if err > 0.5:
            problems.append("%s bounds off by %.2f cm (axis mapping?)" % (info["mesh"], err))
        if tris != info["triangles"]:
            problems.append("%s triangles %d != %d" % (info["mesh"], tris, info["triangles"]))
        if hulls != info["hulls"]:
            problems.append("%s hulls %d != %d" % (info["mesh"], hulls, info["hulls"]))
        if not all(p and p.startswith(DEST + "/Materials/") for p in slots) or len(slots) != len(info["slots"]):
            problems.append("%s material slots %s" % (info["mesh"], slots))
        report["meshes"][info["mesh"]] = {"triangles": tris, "min_cm": got_min, "max_cm": got_max, "bounds_error_cm": err,
                                          "convex_hulls": hulls, "slots": len(slots), "uv_channels": mesh.get_num_tex_coords(0)}
    report["asset_count"] = len(EAL.list_assets(DEST, recursive=True, include_folder=False))
    report["problems"] = problems
    report["status"] = "passed" if not problems else "failed"
    with open(REPORT, "w") as f:
        json.dump(report, f, indent=1)
        f.write("\n")
    log("%s %s" % ("VERIFIED" if not problems else "FAILED", json.dumps(
        {"assets": report["asset_count"], "meshes": len(report["meshes"]), "problems": problems})))
    if problems:
        raise RuntimeError("battlefield kit verification failed: " + "; ".join(problems))


def main():
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    manifest_path = os.path.join(SOURCE, "kit_manifest.txt")
    if not os.path.exists(manifest_path):
        raise RuntimeError("missing %s (run tools/art/battlefield_kit.py)" % manifest_path)
    with open(manifest_path) as f:
        manifest = json.load(f)
    names = texture_sets()
    if ONLY == "backdrop" and not VERIFY_ONLY:
        EAL.make_directory(DEST + "/Textures")
        import_backdrop()
        return
    if not VERIFY_ONLY:
        # Process-local: the legacy FBX importer, whose options are set explicitly above.
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
        for folder in ("Meshes", "Textures", "Materials"):
            EAL.make_directory(DEST + "/" + folder)
        textures = import_textures(names)
        materials = make_materials(names, textures)
        make_river_materials(textures)
        import_backdrop()
        import_meshes(manifest, materials)
    verify(manifest, names)


try:
    main()
except Exception:
    with open(REPORT, "w") as f:
        json.dump({"status": "failed", "traceback": traceback.format_exc()}, f, indent=1)
    unreal.log_error(traceback.format_exc())
    raise
