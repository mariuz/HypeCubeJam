# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Creates the project's sound classes / submixes under /Game/HCJ/Audio and re-homes the Lyra sound waves copied to
# /Game/Audio (see docs/ASSETS.md) onto them, so that they no longer reference Lyra's own classes.
# Prints one "HCJAUDIO" line per wave (duration, channels, sample rate).
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
DIR = "/Game/HCJ/Audio"


def make(name, cls, factory):
    path = DIR + "/" + name
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    return AT.create_asset(name, DIR, cls, factory)


submixes = {}
classes = {}
for group in ("Music", "SFX", "UI"):
    submix = make("SM_HCJ_" + group, unreal.SoundSubmix, unreal.SoundSubmixFactory())
    sound_class = make("SC_HCJ_" + group, unreal.SoundClass, unreal.SoundClassFactory())
    props = sound_class.get_editor_property("properties")
    props.set_editor_property("default_submix", submix)
    if group != "SFX":
        # music and interface sounds are not affected by world effects
        props.set_editor_property("is_ui_sound", group == "UI")
        props.set_editor_property("is_music", group == "Music")
    sound_class.set_editor_property("properties", props)
    submixes[group] = submix
    classes[group] = sound_class
    EAL.save_loaded_asset(submix, False)
    EAL.save_loaded_asset(sound_class, False)

UI_NAMES = ("Lyra_EnemyKilled", "Lyra_EnemyDestroyed", "Lyra_ImpactHeadshot", "Lyra_Plyr_BulletImpact", "Lyra_EndOfRound",
            "SFX_Chime", "Lyra_Plyr_Spawn")
LOOPS = ("Lyra_RingLoop",)

for path in sorted(EAL.list_assets("/Game/Audio", recursive=True, include_folder=False)):
    wave = EAL.load_asset(path)
    if not isinstance(wave, unreal.SoundWave):
        unreal.log_warning("HCJAUDIO not a wave: %s" % path)
        continue
    name = wave.get_name()
    if "/Music/" in path:
        group = "Music"
    elif name.startswith(UI_NAMES):
        group = "UI"
    else:
        group = "SFX"
    wave.set_editor_property("sound_class_object", classes[group])
    wave.set_editor_property("sound_submix_object", None)
    wave.set_editor_property("attenuation_settings", None)
    wave.set_editor_property("concurrency_set", [])
    # the music stems are one-shots with a tail, re-triggered on a bar clock by the game (UHCJAudioDirector)
    looping = name.startswith(LOOPS)
    wave.set_editor_property("looping", looping)
    wave.set_editor_property("virtualization_mode", unreal.VirtualizationMode.RESTART)
    EAL.save_loaded_asset(wave, False)
    unreal.log("HCJAUDIO %s %s dur=%.4f ch=%d sr=%d loop=%s" % (
        group, path, wave.get_editor_property("duration"), wave.get_editor_property("num_channels"),
        wave.get_editor_property("sample_rate"), looping))
unreal.log("HCJAUDIO done")
