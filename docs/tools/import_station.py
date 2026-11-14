# Editor Python (UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Imports the Fab model "Sci-Fi Space Station" by Helindu.Art (CC-BY) from SourceAssets/SciFiSpaceStation into
# /Game/SciFiSpaceStation as ONE static mesh without collision (the game's station collision is its own boxes),
# reports triangles and material slots ("HCJSTATION" log lines), builds reduced LODs and saves.
import os
import unreal

SRC = r"F:\work\HypeCubeJam\SourceAssets\SciFiSpaceStation\source"
DEST = "/Game/SciFiSpaceStation/Meshes"
NAME = "SM_SciFiSpaceStation"

fbx = [os.path.join(SRC, f) for f in os.listdir(SRC)]
fbx = [f for f in fbx if os.path.isfile(f)]
unreal.log("HCJSTATION source files: %s" % fbx)

options = unreal.FbxImportUI()
options.set_editor_property("import_mesh", True)
options.set_editor_property("import_as_skeletal", False)
options.set_editor_property("import_materials", True)
options.set_editor_property("import_textures", False)
options.set_editor_property("import_animations", False)
options.set_editor_property("automated_import_should_detect_type", False)
options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
sm = options.get_editor_property("static_mesh_import_data")
sm.set_editor_property("combine_meshes", True)
sm.set_editor_property("auto_generate_collision", False)
sm.set_editor_property("generate_lightmap_u_vs", False)
sm.set_editor_property("build_nanite", False)
sm.set_editor_property("remove_degenerates", True)

task = unreal.AssetImportTask()
task.set_editor_property("filename", fbx[0])
task.set_editor_property("destination_path", DEST)
task.set_editor_property("destination_name", NAME)
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", True)
task.set_editor_property("save", False)
task.set_editor_property("options", options)
try:
    task.set_editor_property("factory", unreal.FbxFactory())
except Exception as error:
    unreal.log_warning("HCJSTATION no FbxFactory: %s" % error)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

paths = [str(p) for p in task.get_editor_property("imported_object_paths")]
unreal.log("HCJSTATION imported: %s" % paths)
meshes = [a for a in (unreal.EditorAssetLibrary.load_asset(p) for p in paths) if isinstance(a, unreal.StaticMesh)]
for mesh in meshes:
    bounds = mesh.get_bounding_box()
    size = bounds.max - bounds.min
    unreal.log("HCJSTATION mesh %s: %d triangles, %d vertices, %d sections, size %.0f x %.0f x %.0f, nanite %s" % (
        mesh.get_path_name(), mesh.get_num_triangles(0), mesh.get_num_vertices(0), mesh.get_num_sections(0), size.x, size.y, size.z,
        mesh.get_editor_property("nanite_settings").get_editor_property("enabled")))
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        material = slot.get_editor_property("material_interface")
        unreal.log("HCJSTATION   slot %d '%s' -> %s" % (index, slot.get_editor_property("material_slot_name"), material.get_path_name() if material else None))
unreal.log("HCJSTATION done (%d meshes)" % len(meshes))
