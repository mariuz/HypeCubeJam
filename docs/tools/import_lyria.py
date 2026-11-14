# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Imports the three Lyria tracks (SourceAssets/Lyria/*.mp3; the editor's sound factory reads MP3 itself, nothing else
# is needed) into /Game/Audio/Lyria on the project's music sound class, and writes every music wave of the project
# (the new tracks and the Lyra stems) as 16-bit WAV to Saved/LyriaWav so that docs/tools/music_stats.py can measure them.
# Prints one "HCJLYRIA" line per wave.
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "SourceAssets", "Lyria")
OUT = os.path.join(PROJECT, "Saved", "LyriaWav")
DEST = "/Game/Audio/Lyria"
MUSIC_CLASS = EAL.load_asset("/Game/HCJ/Audio/SC_HCJ_Music")

TRACKS = (
    ("menu_Starward_Bound.mp3", "MX_Lyria_Menu_Starward_Bound"),
    ("explore_Gravity_Well_Hunt.mp3", "MX_Lyria_Explore_Gravity_Well_Hunt"),
    ("combat_Stellar_Breach.mp3", "MX_Lyria_Combat_Stellar_Breach"),
)

os.makedirs(OUT, exist_ok=True)


def export_wav(wave, name):
    task = unreal.AssetExportTask()
    task.set_editor_property("object", wave)
    task.set_editor_property("filename", os.path.join(OUT, name + ".wav"))
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    task.set_editor_property("exporter", unreal.SoundExporterWAV())
    return unreal.Exporter.run_asset_export_task(task)


for file_name, asset_name in TRACKS:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SOURCE, file_name))
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    AT.import_asset_tasks([task])
    wave = EAL.load_asset(DEST + "/" + asset_name)
    if not isinstance(wave, unreal.SoundWave):
        unreal.log_error("HCJLYRIA import failed: %s" % file_name)
        continue
    wave.set_editor_property("sound_class_object", MUSIC_CLASS)
    wave.set_editor_property("sound_submix_object", None)
    wave.set_editor_property("attenuation_settings", None)
    wave.set_editor_property("concurrency_set", [])
    # The game loops a track itself (two voices cross-faded at the seam, UHCJAudioDirector), so the wave is a one-shot.
    wave.set_editor_property("looping", False)
    wave.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
    EAL.save_loaded_asset(wave, False)
    ok = export_wav(wave, asset_name)
    unreal.log("HCJLYRIA track %s <- %s dur=%.3f ch=%d sr=%d wav=%s" % (
        wave.get_path_name(), file_name, wave.get_editor_property("duration"), wave.get_editor_property("num_channels"),
        wave.get_editor_property("sample_rate"), ok))

for path in sorted(EAL.list_assets("/Game/Audio/Sounds/Music", recursive=True, include_folder=False)):
    wave = EAL.load_asset(path)
    if isinstance(wave, unreal.SoundWave):
        ok = export_wav(wave, wave.get_name())
        unreal.log("HCJLYRIA stem %s dur=%.3f ch=%d sr=%d wav=%s" % (
            path, wave.get_editor_property("duration"), wave.get_editor_property("num_channels"),
            wave.get_editor_property("sample_rate"), ok))
unreal.log("HCJLYRIA done")
