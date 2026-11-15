# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>), like import_lyria.py.
# Imports the ship computer's lines (SourceAssets/Voice/<id>.wav, 44.1 kHz 16-bit mono, rendered with the Flite
# speech synthesiser and processed outside the project; ids and texts in SourceAssets/Voice/lines.json) into /Game/HCJ/Audio/Voice on the project's
# interface sound class, so that the "Interface volume" slider and hcj.Audio.UiVolume apply to them.
# Prints one "HCJVOICE" line per wave and, at the end, the lines for Config/DefaultGame.ini.
import json
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "SourceAssets", "Voice")
DEST = "/Game/HCJ/Audio/Voice"
UI_CLASS = EAL.load_asset("/Game/HCJ/Audio/SC_HCJ_UI")

# The order of EHCJVoiceLine (Audio/HCJVoiceLogic.h) without None: UHCJAssetSettings::VoiceLines is indexed by it.
ORDER = ("welcome", "shield_fore", "shield_aft", "shield_port", "shield_starboard", "hull_critical", "torpedo_incoming",
         "missile_lock", "enemy_behind", "gravity_well", "tractor_caught", "sector_clear")


def asset_name(line_id):
    return "VO_Computer_" + "".join(part.capitalize() for part in line_id.split("_"))


with open(os.path.join(SOURCE, "lines.json"), "r", encoding="utf-8") as handle:
    texts = {line["id"]: line["text"] for line in json.load(handle)["lines"]}

if UI_CLASS is None:
    unreal.log_error("HCJVOICE the interface sound class /Game/HCJ/Audio/SC_HCJ_UI did not load")

ini = []
failed = 0
for line_id in ORDER:
    name = asset_name(line_id)
    source = os.path.join(SOURCE, line_id + ".wav")
    if not os.path.exists(source):
        unreal.log_error("HCJVOICE missing file: %s" % source)
        failed += 1
        continue
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    AT.import_asset_tasks([task])
    wave = EAL.load_asset(DEST + "/" + name)
    if not isinstance(wave, unreal.SoundWave):
        unreal.log_error("HCJVOICE import failed: %s" % source)
        failed += 1
        continue
    wave.set_editor_property("sound_class_object", UI_CLASS)
    wave.set_editor_property("sound_submix_object", None)
    wave.set_editor_property("attenuation_settings", None)
    wave.set_editor_property("concurrency_set", [])
    wave.set_editor_property("looping", False)
    EAL.save_loaded_asset(wave, False)
    unreal.log("HCJVOICE %s <- %s.wav \"%s\" dur=%.3f ch=%d sr=%d" % (
        wave.get_path_name(), line_id, texts.get(line_id, "?"), wave.get_editor_property("duration"),
        wave.get_editor_property("num_channels"), wave.get_editor_property("sample_rate")))
    ini.append("+VoiceLines=%s/%s.%s" % (DEST, name, name))

unreal.log("HCJVOICE for Config/DefaultGame.ini, [/Script/HypeCubeJam.HCJAssetSettings]:")
unreal.log("HCJVOICE VoiceVolume=1.0")
for entry in ini:
    unreal.log("HCJVOICE " + entry)
unreal.log("HCJVOICE done, %d imported, %d failed" % (len(ini), failed))
