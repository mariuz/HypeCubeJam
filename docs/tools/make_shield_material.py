# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds /Game/HCJ/Effects/M_HCJ_ShieldBubble: the additive, unlit, two-sided material of the ship's shield bubble
# (one basic sphere per ship, scaled to an ellipsoid, hidden while nothing happens - see AHCJShipCharacter).
# Everything is one Custom node working in the sphere's own space, so the look does not depend on how the bubble
# is stretched or turned:
#   HitDirA / HitDirB   unit direction of an impact (two slots, used in turn)
#   HitAgeA / HitAgeB   0 = just hit .. 1 = over (the game drives them over about 0.4 s)
#   HitColorA / B       tint by the strength of the facing that took the hit (blue-white .. orange)
#   CollapseDir, Collapse, CollapseColor   a facing failed: its half of the bubble flickers (Collapse 0..1)
#   Brightness
# Writes a short report next to the project log: Saved/Logs/make_shield_material.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
NAME = "M_HCJ_ShieldBubble"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_shield_material.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

BUBBLE = r"""
// P: position on the sphere in its own space. Rim: fresnel term (1 at the silhouette). T: seconds.
float3 n = normalize(P);
float3 col = float3(0.0, 0.0, 0.0);
// a faint cell pattern so the surface reads as a field, not a blob
float cells = 0.6 + 0.4 * sin(n.x * 34.0) * sin(n.y * 34.0) * sin(n.z * 34.0 + T * 6.0);

float3 dirs[2] = { DirA, DirB };
float ages[2] = { AgeA, AgeB };
float3 tints[2] = { ColA, ColB };
for (int i = 0; i < 2; i++)
{
    float age = saturate(ages[i]);
    float fade = 1.0 - age;
    if (fade <= 0.0) { continue; }
    // angular distance from the impact: 0 at the point, 1 on the far side
    float ang = acos(clamp(dot(n, normalize(dirs[i])), -1.0, 1.0)) / 3.14159265;
    float core = exp(-ang * ang * 240.0) * fade * fade * 3.5;              // the hot spot
    float r = 0.04 + age * 0.5;                                              // ripple running outwards
    float ring = exp(-pow((ang - r) / 0.024, 2.0)) * fade * 1.6;
    float ring2 = exp(-pow((ang - r * 0.55) / 0.018, 2.0)) * fade * 0.7;
    float wash = exp(-ang * ang * 14.0) * fade * (0.02 + 0.45 * Rim) * cells;  // the hit side of the bubble lights up, rim first
    col += tints[i] * (core + ring + ring2 + wash);
}

// a failing facing: its half of the bubble breaks up in blocks
float c = saturate(Coll);
if (c > 0.0)
{
    float side = smoothstep(-0.15, 0.55, dot(n, normalize(DirC)));
    float3 cell = floor(n * 10.0 + floor(T * 24.0) * float3(1.3, 2.1, 0.7));
    float noise = frac(sin(dot(cell, float3(12.9898, 78.233, 37.719))) * 43758.5453);
    col += ColC * c * side * (0.01 + 0.3 * Rim + 0.35 * noise * noise * noise);
}
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


try:
    path = DST + "/" + NAME
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(NAME, DST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    # Position in the sphere's own space.
    wpos = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1300, -200)
    local = MEL.create_material_expression(mat, unreal.MaterialExpressionTransformPosition, -1100, -200)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -1100, -50)
    fres.set_editor_property("exponent", 2.5)
    fres.set_editor_property("base_reflect_fraction", 0.02)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, 100)

    dir_a = vector(mat, "HitDirA", (1.0, 0.0, 0.0), -1100, 200)
    age_a = scalar(mat, "HitAgeA", 1.0, -1100, 400)
    col_a = vector(mat, "HitColorA", (0.55, 0.85, 1.0), -1100, 500)
    dir_b = vector(mat, "HitDirB", (-1.0, 0.0, 0.0), -1100, 700)
    age_b = scalar(mat, "HitAgeB", 1.0, -1100, 900)
    col_b = vector(mat, "HitColorB", (0.55, 0.85, 1.0), -1100, 1000)
    dir_c = vector(mat, "CollapseDir", (1.0, 0.0, 0.0), -1100, 1200)
    coll = scalar(mat, "Collapse", 0.0, -1100, 1400)
    col_c = vector(mat, "CollapseColor", (1.0, 0.3, 0.06), -1100, 1500)
    bright = scalar(mat, "Brightness", 0.8, -1100, 1700)

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 300)
    custom.set_editor_property("code", BUBBLE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "ShieldBubble")
    names = ["P", "Rim", "T", "DirA", "AgeA", "ColA", "DirB", "AgeB", "ColB", "DirC", "Coll", "ColC", "Brightness"]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    ok = [
        MEL.connect_material_expressions(wpos, "", local, ""),
        MEL.connect_material_expressions(local, "", custom, "P"),
        MEL.connect_material_expressions(fres, "", custom, "Rim"),
        MEL.connect_material_expressions(time, "", custom, "T"),
        MEL.connect_material_expressions(dir_a, "RGB", custom, "DirA"),
        MEL.connect_material_expressions(age_a, "", custom, "AgeA"),
        MEL.connect_material_expressions(col_a, "RGB", custom, "ColA"),
        MEL.connect_material_expressions(dir_b, "RGB", custom, "DirB"),
        MEL.connect_material_expressions(age_b, "", custom, "AgeB"),
        MEL.connect_material_expressions(col_b, "RGB", custom, "ColB"),
        MEL.connect_material_expressions(dir_c, "RGB", custom, "DirC"),
        MEL.connect_material_expressions(coll, "", custom, "Coll"),
        MEL.connect_material_expressions(col_c, "RGB", custom, "ColC"),
        MEL.connect_material_expressions(bright, "", custom, "Brightness"),
        MEL.connect_material_property(custom, "", MP.MP_EMISSIVE_COLOR),
    ]
    res["log"].append("connections %s" % ok)
    res["all_connected"] = all(ok)
    res["expressions"] = MEL.get_num_material_expressions(mat)
    EAL.save_asset(path, only_if_is_dirty=False)
    res["saved"] = path

    # Lengths of the sounds a collapse could use (nothing is imported; they are already in the project).
    for p in ["/Game/Audio/Sounds/SFX/SFX_Chime_03A", "/Game/Audio/Sounds/SFX/Lyra_ItemReplenish_01", "/Game/Audio/Sounds/SFX/Lyra_Dash_01",
              "/Game/Audio/Sounds/Impacts/Lyra_EnemyDestroyed_01", "/Game/Audio/Sounds/SFX/Lyra_EndOfRound-Tick-01",
              "/Game/Audio/Sounds/Impacts/Lyra_ImpactHeadshot_01", "/Game/Audio/Sounds/SFX/Lyra_BasePickup_01", "/Game/Audio/Sounds/SFX/Lyra_Portal_02"]:
        a = EAL.load_asset(p) if EAL.does_asset_exist(p) else None
        res["log"].append("%s: %s" % (p, ("%.2f s" % a.get_editor_property("duration")) if a else "missing"))
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
