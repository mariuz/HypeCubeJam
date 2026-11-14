# Editor Python, run in the EDITOR (not the pythonscript commandlet, where the static mesh editor subsystem does not exist):
#   UnrealEditor-Cmd <project> -ExecutePythonScript=<this file> -nullrhi -unattended -nosplash
# Gives /Game/SpaceStation4/Meshes/SM_Station4 (docs/tools/import_station4.py) its reduced LODs.
# Writes a report: Saved/Logs/station4_lods.json
import json
import traceback

import unreal

PATH = "/Game/SpaceStation4/Meshes/SM_Station4"
OUT = unreal.Paths.project_saved_dir() + "Logs/station4_lods.json"
# (share of the triangles, screen size at which the LOD comes in)
LODS = [(1.0, 1.0), (0.45, 0.5), (0.18, 0.22), (0.06, 0.08)]
res = {}
try:
    mesh = unreal.EditorAssetLibrary.load_asset(PATH)
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    res["subsystem"] = subsystem is not None
    options = unreal.StaticMeshReductionOptions()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    settings = []
    for share, screen in LODS:
        entry = unreal.StaticMeshReductionSettings()
        entry.set_editor_property("percent_triangles", share)
        entry.set_editor_property("screen_size", screen)
        settings.append(entry)
    options.set_editor_property("reduction_settings", settings)
    res["set_lods"] = subsystem.set_lods_with_notification(mesh, options, True)
    unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    res["triangles"] = [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())]
    res["sections"] = [mesh.get_num_sections(i) for i in range(mesh.get_num_lods())]
except Exception:
    res["error"] = traceback.format_exc()
with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
