# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>), after docs/tools/prep_station4.py.
# Imports "Space Station 4" by Gerardo Justel (CC-BY; SourceAssets/SpaceStation4) into /Game/SpaceStation4:
#   Textures/T_Station4_<1|2>_<BaseColor|ORM|Emissive|Normal>   the eight prepared 2048 maps, with a size limit each
#   Materials/M_HCJ_Station        lit, opaque: base colour x BaseGain, AO / roughness / metallic x MetalScale from the
#                                  ORM map, normal map, emission = emissive map x EmissiveTint x EmissiveBoost (the
#                                  windows) + base colour x AmbientFloor (the unlit side is never pure black)
#   Materials/MI_Station4_1 / _2   its two instances (the two texture sets of the model)
#   Materials/M_HCJ_StationLights  unlit: Color x Brightness (the model's "Emission" material: lamps and strips)
#   Meshes/SM_Station4             the 20 meshes of the glb merged into ONE static mesh with three sections (Interchange
#                                  glTF import, node transforms baked), no collision (the game's station collision is its
#                                  own boxes), no Nanite. The reduced LODs are made afterwards by station4_lods.py: in
#                                  this commandlet there is no static mesh editor subsystem and the older library
#                                  returns -1 (the attempt below is kept; its result is in the report).
# Writes a report: Saved/Logs/import_station4.json
import json
import os
import traceback

import unreal

ROOT = "/Game/SpaceStation4"
SRC = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "SourceAssets", "SpaceStation4", "prepared"))
OUT = unreal.Paths.project_saved_dir() + "Logs/import_station4.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

# name -> (compression, sRGB, sampler type, largest size in the game)
TEXTURES = {
    "BaseColor": (unreal.TextureCompressionSettings.TC_DEFAULT, True, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 2048),
    "ORM": (unreal.TextureCompressionSettings.TC_MASKS, False, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 1024),
    "Emissive": (unreal.TextureCompressionSettings.TC_DEFAULT, True, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 2048),
    "Normal": (unreal.TextureCompressionSettings.TC_NORMALMAP, False, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 1024),
}
# LOD: (share of the triangles, screen size at which it comes in)
LODS = [(1.0, 1.0), (0.45, 0.5), (0.18, 0.22), (0.06, 0.08)]


def log(text):
    res["log"].append(text)
    unreal.log("HCJSTATION4 " + text)


def import_textures():
    tasks = []
    for set_index in (1, 2):
        for name in TEXTURES:
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", os.path.join(SRC, "T_Station4_%d_%s.png" % (set_index, name)))
            task.set_editor_property("destination_path", ROOT + "/Textures")
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", False)
            tasks.append(task)
    tools.import_asset_tasks(tasks)
    out = {}
    for set_index in (1, 2):
        for name, (compression, srgb, _, size) in TEXTURES.items():
            path = "%s/Textures/T_Station4_%d_%s" % (ROOT, set_index, name)
            texture = EAL.load_asset(path)
            texture.set_editor_property("compression_settings", compression)
            texture.set_editor_property("srgb", srgb)
            texture.set_editor_property("max_texture_size", size)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
            EAL.save_asset(path, only_if_is_dirty=False)
            out[(set_index, name)] = texture
            log("%s: %d x %d source, limit %d, %s" % (path, texture.blueprint_get_size_x(), texture.blueprint_get_size_y(), size, compression))
    return out


def new_material(name):
    path = ROOT + "/Materials/" + name
    if EAL.does_asset_exist(path):
        material = EAL.load_asset(path)
        MEL.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    return material, path


def scalar(material, name, value, x, y):
    e = MEL.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def vector(material, name, value, x, y):
    e = MEL.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(value[0], value[1], value[2], 1.0))
    return e


def build_station_material(textures):
    material, path = new_material("M_HCJ_Station")
    material.set_editor_property("two_sided", True)
    samples = {}
    for row, (name, (_, _, sampler, _)) in enumerate(TEXTURES.items()):
        e = MEL.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1200, -400 + 260 * row)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("texture", textures[(1, name)])
        e.set_editor_property("sampler_type", sampler)
        samples[name] = e
    gain = scalar(material, "BaseGain", 1.5, -1200, 700)
    metal = scalar(material, "MetalScale", 0.3, -1200, 800)
    boost = scalar(material, "EmissiveBoost", 10.0, -1200, 900)
    floor = scalar(material, "AmbientFloor", 0.035, -1200, 1000)
    tint = vector(material, "EmissiveTint", (1.0, 1.0, 1.0), -1200, 1100)

    def mul(a, a_out, b, b_out, x, y):
        e = MEL.create_material_expression(material, unreal.MaterialExpressionMultiply, x, y)
        MEL.connect_material_expressions(a, a_out, e, "A")
        MEL.connect_material_expressions(b, b_out, e, "B")
        return e

    base = mul(samples["BaseColor"], "RGB", gain, "", -700, -400)
    metallic = mul(samples["ORM"], "B", metal, "", -700, -100)
    glow = mul(mul(samples["Emissive"], "RGB", boost, "", -700, 200), "", tint, "RGB", -500, 200)
    ambient = mul(base, "", floor, "", -500, 400)
    emissive = MEL.create_material_expression(material, unreal.MaterialExpressionAdd, -300, 300)
    MEL.connect_material_expressions(glow, "", emissive, "A")
    MEL.connect_material_expressions(ambient, "", emissive, "B")
    ok = [
        MEL.connect_material_property(base, "", MP.MP_BASE_COLOR),
        MEL.connect_material_property(metallic, "", MP.MP_METALLIC),
        MEL.connect_material_property(samples["ORM"], "G", MP.MP_ROUGHNESS),
        MEL.connect_material_property(samples["ORM"], "R", MP.MP_AMBIENT_OCCLUSION),
        MEL.connect_material_property(samples["Normal"], "RGB", MP.MP_NORMAL),
        MEL.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR),
    ]
    EAL.save_asset(path, only_if_is_dirty=False)
    log("%s: connections %s" % (path, ok))

    instances = []
    for set_index in (1, 2):
        name = "MI_Station4_%d" % set_index
        ipath = ROOT + "/Materials/" + name
        instance = EAL.load_asset(ipath) if EAL.does_asset_exist(ipath) else tools.create_asset(name, ROOT + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(instance, material)
        for tname in TEXTURES:
            MEL.set_material_instance_texture_parameter_value(instance, tname, textures[(set_index, tname)])
        EAL.save_asset(ipath, only_if_is_dirty=False)
        instances.append(instance)
    return instances


def build_lights_material():
    material, path = new_material("M_HCJ_StationLights")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    color = vector(material, "Color", (0.742, 0.78, 0.348), -700, 0)
    brightness = scalar(material, "Brightness", 6.0, -700, 200)
    e = MEL.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 100)
    MEL.connect_material_expressions(color, "RGB", e, "A")
    MEL.connect_material_expressions(brightness, "", e, "B")
    ok = MEL.connect_material_property(e, "", MP.MP_EMISSIVE_COLOR)
    EAL.save_asset(path, only_if_is_dirty=False)
    log("%s: connection %s" % (path, ok))
    return material


def import_mesh():
    folder = ROOT + "/Meshes"
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("use_source_name_for_asset", False)
    pipeline.set_editor_property("asset_name", "SM_Station4")
    mesh_pipeline = pipeline.get_editor_property("mesh_pipeline")
    mesh_pipeline.set_editor_property("import_static_meshes", True)
    mesh_pipeline.set_editor_property("import_skeletal_meshes", False)
    mesh_pipeline.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh_pipeline.set_editor_property("collision", False)
    mesh_pipeline.set_editor_property("build_nanite", False)
    mesh_pipeline.set_editor_property("generate_lightmap_u_vs", False)
    common = pipeline.get_editor_property("common_meshes_properties")
    common.set_editor_property("bake_meshes", True)
    common.set_editor_property("recompute_normals", False)
    common.set_editor_property("recompute_tangents", True)
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)

    source = unreal.InterchangeManager.create_source_data(os.path.join(SRC, "space_station_4_mesh.glb"))
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    result = manager.import_asset(folder, source, params)
    log("interchange import_asset -> %s" % (result,))
    found = [a for a in (EAL.load_asset(p) for p in EAL.list_assets(folder, recursive=True)) if isinstance(a, unreal.StaticMesh)]
    log("static meshes in %s: %s" % (folder, [m.get_path_name() for m in found]))
    return found


def finish_mesh(mesh, instances, lights):
    slots = mesh.get_editor_property("static_materials")
    names = []
    for slot in slots:
        name = str(slot.get_editor_property("material_slot_name"))
        names.append(name)
        lowered = name.lower()
        material = lights if "emission" in lowered else instances[1] if "station_2" in lowered else instances[0]
        slot.set_editor_property("material_interface", material)
    mesh.set_editor_property("static_materials", slots)

    # LODs: the editor subsystem does not exist in a commandlet; the older library is tried and has returned -1 there so
    # far, which is why docs/tools/station4_lods.py exists.
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is not None:
        options = unreal.StaticMeshReductionOptions()
        make = unreal.StaticMeshReductionSettings
    else:
        options = unreal.EditorScriptingMeshReductionOptions()
        make = unreal.EditorScriptingMeshReductionSettings
    options.set_editor_property("auto_compute_lod_screen_size", False)
    settings = []
    for share, screen in LODS:
        entry = make()
        entry.set_editor_property("percent_triangles", share)
        entry.set_editor_property("screen_size", screen)
        settings.append(entry)
    options.set_editor_property("reduction_settings", settings)
    if subsystem is not None:
        count = subsystem.set_lods_with_notification(mesh, options, True)
        subsystem.remove_collisions_with_notification(mesh, True)
    else:
        count = unreal.EditorStaticMeshLibrary.set_lods(mesh, options)
        unreal.EditorStaticMeshLibrary.remove_collisions(mesh)
    EAL.save_asset(mesh.get_path_name().split(".")[0], only_if_is_dirty=False)

    bounds = mesh.get_bounding_box()
    size = bounds.max - bounds.min
    res["mesh"] = {
        "path": mesh.get_path_name(), "slots": names, "lods": count,
        "triangles": [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())],
        "sections": mesh.get_num_sections(0),
        "min": [bounds.min.x, bounds.min.y, bounds.min.z], "max": [bounds.max.x, bounds.max.y, bounds.max.z],
        "size": [size.x, size.y, size.z],
        "nanite": bool(mesh.get_editor_property("nanite_settings").get_editor_property("enabled")),
    }
    log("mesh: %s" % res["mesh"])


try:
    textures = import_textures()
    instances = build_station_material(textures)
    lights = build_lights_material()
    meshes = import_mesh()
    if len(meshes) == 1:
        finish_mesh(meshes[0], instances, lights)
    else:
        res["error"] = "expected one merged static mesh, found %d" % len(meshes)
except Exception:
    res["error"] = traceback.format_exc()
    unreal.log_error("HCJSTATION4 " + res["error"])

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
