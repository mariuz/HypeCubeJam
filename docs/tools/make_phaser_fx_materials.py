# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds two materials under /Game/HCJ/Effects. Like the torpedo materials (make_torpedo_materials.py) both are
# additive, unlit, two-sided, one Custom node on the texture coordinates of the engine's basic plane, no texture:
#   M_HCJ_PhaserBeam      the phaser beam on a quad stretched from the muzzle to the end of the beam and rolled towards
#                         the view: a white-hot core in an orange-red body with a soft glow, a fast flicker and a
#                         ripple running along the beam. U runs along the beam, V across it.
#                         Parameters: Color (vector), Brightness, Length (uu, so the ripple keeps its size), Phase.
#   M_HCJ_ExplosionFlare  the flare of an explosion on a quad facing the view with its U axis level on the screen: a
#                         bloom flash that collapses, a long thin horizontal (anamorphic) streak, a star of thin rays.
#                         Parameters: Color (vector), Brightness, Age (0 = just went off .. 1 = over), Seed (turns the star).
# Writes a short report: Saved/Logs/make_phaser_fx_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_phaser_fx_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

BEAM = r"""
// UV.x: 0 at the muzzle .. 1 at the end of the beam. UV.y: across. Length: uu. T: seconds.
float v = (UV.y - 0.5) * 2.0;
float x = UV.x * Length * 0.01;
float t = T + Phase;
float flick = 0.84 + 0.16 * sin(t * 71.0) * sin(t * 43.7 + 1.3);
float ripple = 0.8 + 0.2 * sin(x * 2.3 - t * 60.0) * sin(x * 0.7 + t * 23.0);
float core = exp(-v * v * 520.0) * 1.7;
float body = exp(-v * v * 42.0) * 1.5 * ripple;
float glow = exp(-abs(v) * 4.0) * 0.2;
float ends = saturate(min(UV.x, 1.0 - UV.x) * Length / 50.0);
float edge = 1.0 - smoothstep(0.8, 1.0, abs(v));
float3 col = Color * (body + glow) + float3(1.0, 0.93, 0.8) * core;
return col * Brightness * flick * ends * edge;
"""

FLARE = r"""
// UV: 0..1 across the quad. Age: 0 = just went off .. 1 = over.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float age = saturate(Age);
float fade = 1.0 - age;
float pop = fade * fade;
float bloom = exp(-r * r * (6.0 + 50.0 * age)) * pop * 1.3 + exp(-r * 3.2) * pop * 0.22;
float sy = exp(-p.y * p.y * (1100.0 - 600.0 * age));
float sx = exp(-abs(p.x) * (1.5 + 3.5 * age));
float streak = sy * sx * fade * 2.4;
float halo = exp(-p.y * p.y * 130.0) * exp(-abs(p.x) * 4.5) * pop * 0.45;
float a = atan2(p.y, p.x + 0.00001) + Seed;
float star = (pow(abs(cos(a * 2.0)), 90.0) * exp(-r * 4.0) * 1.3
	+ pow(abs(sin(a * 2.0)), 150.0) * exp(-r * 7.0) * 0.8
	+ pow(abs(cos(a * 3.0 + 0.6)), 220.0) * exp(-r * 5.0) * 0.5) * pop * fade;
// Round things end in a round falloff well inside the quad (no square outline); the streak only towards its two ends.
float round = 1.0 - smoothstep(0.45, 0.92, r);
float ends = 1.0 - smoothstep(0.8, 1.0, abs(p.x));
float3 streakColor = lerp(Color, float3(0.55, 0.75, 1.0), 0.55);
float3 col = (Color * (bloom + star) + float3(1.0, 0.96, 0.85) * (bloom * 0.6 + star * 0.4)) * round + streakColor * (streak + halo) * ends;
return col * Brightness;
"""


def scalar(mat, name, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def vector(mat, name, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(value[0], value[1], value[2], 1.0))
    return e


def build(name, code, scalars, default_color):
    """One additive unlit two-sided material: Custom(UV, T, Color, <scalars>) -> emissive."""
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -200)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, -50)
    color = vector(mat, "Color", default_color, -1100, 100)
    params = [(n, scalar(mat, n, v, -1100, 300 + 110 * i)) for i, (n, v) in enumerate(scalars)]

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", name)
    names = ["UV", "T", "Color"] + [n for n, _ in params]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    ok = [
        MEL.connect_material_expressions(uv, "", custom, "UV"),
        MEL.connect_material_expressions(time, "", custom, "T"),
        MEL.connect_material_expressions(color, "RGB", custom, "Color"),
    ]
    for n, e in params:
        ok.append(MEL.connect_material_expressions(e, "", custom, n))
    ok.append(MEL.connect_material_property(custom, "", MP.MP_EMISSIVE_COLOR))
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s, %d expressions" % (path, ok, MEL.get_num_material_expressions(mat)))
    return all(ok)


try:
    res["beam"] = build("M_HCJ_PhaserBeam", BEAM, [("Brightness", 3.0), ("Length", 3000.0), ("Phase", 0.0)], (1.0, 0.36, 0.06))
    res["flare"] = build("M_HCJ_ExplosionFlare", FLARE, [("Brightness", 3.0), ("Age", 0.2), ("Seed", 0.0)], (1.0, 0.6, 0.25))
    # Lengths of the sounds the beam is built from (all already in the project).
    for p in ["/Game/Audio/Sounds/SFX/Lyra_RingLoop_01", "/Game/Audio/Sounds/SFX/Lyra_Dash_01", "/Game/Audio/Sounds/SFX/Lyra_ItemReplenish_01",
              "/Game/Audio/Sounds/SFX/Lyra_BasePickup_01", "/Game/Audio/Sounds/SFX/Lyra_PickUpWeapon_01", "/Game/Audio/Sounds/SFX/Lyra_JumpPad_Whoosh_01"]:
        a = EAL.load_asset(p) if EAL.does_asset_exist(p) else None
        res["log"].append("%s: %s" % (p, ("%.2f s, looping %s" % (a.get_editor_property("duration"), a.get_editor_property("looping"))) if a else "missing"))
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
