# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds the two materials of the warp drive and the cloaking device under /Game/HCJ/Effects. Unlit, two-sided,
# Custom nodes only, no texture:
#   M_HCJ_Cloak        what a cloaked hull is drawn with (it replaces every material of the hull while the ship is
#                      cloaked): TRANSLUCENT, a faint blue rim with bands that crawl over the hull, and a little
#                      refraction, so the stars behind it swim. Parameters: Opacity (0.1 another ship's shimmer, 0.4
#                      the own ship, 0.3 / 0.85 flickering when revealed), Phase (so that two ships do not shimmer in step).
#   M_HCJ_WarpStreaks  ADDITIVE star streaks on one quad in front of the own camera: thin radial lines that run
#                      outwards from the middle of the view, none in the middle (where the ship is).
#                      Parameters: Color (vector), Strength (0..1).
# Writes a short report: Saved/Logs/make_trek_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_trek_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

CLOAK_COLOR = r"""
// N: world normal, V: world vector from the pixel to the camera, P: world position, T: time.
float f = 1.0 - saturate(abs(dot(normalize(N), normalize(V))));
float rim = pow(f, 2.2);
float bands = 0.5 + 0.5 * sin(P.z * 0.045 + P.x * 0.02 + T * 2.3 + Phase);
bands = pow(bands, 6.0);
float o = saturate(Opacity);
// (The flat part keeps a hull that is seen face on from vanishing: the pilot must see where his own ship is.)
return float3(0.35, 0.65, 1.0) * (rim * 0.9 + bands * 0.35 * (0.3 + f) + 0.5 * o * o) * (0.25 + 1.6 * o);
"""

CLOAK_OPACITY = r"""
float f = 1.0 - saturate(abs(dot(normalize(N), normalize(V))));
float rim = pow(f, 1.6);
float bands = pow(0.5 + 0.5 * sin(P.z * 0.045 + P.x * 0.02 + T * 2.3 + Phase), 6.0);
return saturate(Opacity * (0.45 + 0.75 * rim + 0.5 * bands));
"""

CLOAK_REFRACTION = r"""
float f = 1.0 - saturate(abs(dot(normalize(N), normalize(V))));
return 1.0 + 0.08 * f + 0.03 * sin(T * 3.1 + Phase);
"""

STREAKS = r"""
// UV: 0..1 across the quad (2.4 x 3.8 units). Radial lines running outwards; nothing in the middle.
float2 q = (UV - 0.5) * 2.0 * float2(1.2, 1.9);
float r = length(q) / 1.9;
float a = atan2(q.y, q.x + 0.00001) / 6.2831853 + 0.5;
float cells = 110.0;
float n = floor(a * cells);
float h = frac(sin(n * 127.1 + 3.7) * 43758.5453);
float h2 = frac(sin(n * 311.7 + 1.3) * 24634.6345);
float within = abs(frac(a * cells) - 0.5);
float width = 0.08 + 0.22 * h2;
float ray = smoothstep(width, 0.0, within) * step(0.35, h);
float t = frac(r * (0.7 + 0.9 * h) - T * (1.1 + 1.9 * h2) + h * 7.0);
float dash = smoothstep(0.0, 0.08, t) * smoothstep(0.75, 0.2, t);
float fade = smoothstep(0.1, 0.45, r);
float3 col = lerp(Color, float3(1.0, 1.0, 1.0), 0.5 * h2);
return col * (ray * dash * fade * saturate(Strength) * 2.2);
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


def custom(mat, code, output, names, x, y, description):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", output)
    c.set_editor_property("description", description)
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    c.set_editor_property("inputs", inputs)
    return c


def get_material(name):
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    return mat, path


def build_cloak():
    mat, path = get_material("M_HCJ_Cloak")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    normal = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1100, -300)
    view = MEL.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1100, -160)
    pos = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1100, -20)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, 120)
    opacity = scalar(mat, "Opacity", 0.1, -1100, 260)
    phase = scalar(mat, "Phase", 0.0, -1100, 380)
    sources = [("N", normal), ("V", view), ("P", pos), ("T", time), ("Opacity", opacity), ("Phase", phase)]
    ok = []
    for code, output, prop, y, used in (
            (CLOAK_COLOR, unreal.CustomMaterialOutputType.CMOT_FLOAT3, MP.MP_EMISSIVE_COLOR, -200, ["N", "V", "P", "T", "Opacity", "Phase"]),
            (CLOAK_OPACITY, unreal.CustomMaterialOutputType.CMOT_FLOAT1, MP.MP_OPACITY, 100, ["N", "V", "P", "T", "Opacity", "Phase"]),
            (CLOAK_REFRACTION, unreal.CustomMaterialOutputType.CMOT_FLOAT1, MP.MP_REFRACTION, 400, ["N", "V", "T", "Phase"])):
        node = custom(mat, code, output, used, -500, y, "M_HCJ_Cloak")
        for n, e in sources:
            if n in used:
                ok.append(MEL.connect_material_expressions(e, "", node, n))
        ok.append(MEL.connect_material_property(node, "", prop))
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s, %d expressions" % (path, ok, MEL.get_num_material_expressions(mat)))
    return all(ok)


def build_streaks():
    mat, path = get_material("M_HCJ_WarpStreaks")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -200)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, -60)
    color = vector(mat, "Color", (0.55, 0.8, 1.0), -1100, 100)
    strength = scalar(mat, "Strength", 1.0, -1100, 320)
    node = custom(mat, STREAKS, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["UV", "T", "Color", "Strength"], -500, 0, "M_HCJ_WarpStreaks")
    ok = [MEL.connect_material_expressions(uv, "", node, "UV"), MEL.connect_material_expressions(time, "", node, "T"),
          MEL.connect_material_expressions(color, "RGB", node, "Color"), MEL.connect_material_expressions(strength, "", node, "Strength"),
          MEL.connect_material_property(node, "", MP.MP_EMISSIVE_COLOR)]
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s, %d expressions" % (path, ok, MEL.get_num_material_expressions(mat)))
    return all(ok)


try:
    res["cloak"] = build_cloak()
    res["streaks"] = build_streaks()
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
