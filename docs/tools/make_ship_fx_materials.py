# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds the materials that make a ship readable against dark space, under /Game/HCJ/Effects. All additive, unlit,
# two-sided, one Custom node, no texture:
#   M_HCJ_ShipRim      OVERLAY material of the hull mesh (UMeshComponent::SetOverlayMaterial: the hull is drawn a second
#                      time with it): a fresnel rim in the enemy colour that widens to the whole silhouette with distance,
#                      plus a small flat floor so that the unlit side of a hull is never pure black.
#                      Parameters: Color (vector), Brightness, Far (0 = next to the view .. 1 = far away), Floor.
#   M_HCJ_ShipGlint    a point of light on a quad facing the view: a hot core, a soft halo, a thin horizontal streak and
#                      four fine spikes. Used for the "minimum apparent size" glow of a distant ship and (Spikes = 0)
#                      for the glow of its engine. Parameters: Color (vector), Brightness, Spikes (0..1).
#   M_HCJ_EngineTrail  the engine trail on a quad from the tail of the ship (U = 0) to where the tail was a moment ago
#                      (U = 1), rolled towards the view: white-hot at the ship, tapering and fading to the end.
#                      Parameters: Color (vector), Brightness.
# Writes a short report: Saved/Logs/make_ship_fx_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_ship_fx_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

RIM = r"""
// N: world normal, V: world vector from the pixel to the camera. Far: 0 near .. 1 far.
float f = 1.0 - saturate(abs(dot(normalize(N), normalize(V))));
float far = saturate(Far);
float rim = pow(f, lerp(2.6, 0.45, far));
float glow = rim * lerp(1.0, 1.6, far) + far * 0.55;
float3 floorColor = float3(0.45, 0.55, 0.75) * Floor * (0.55 + 0.45 * f);
return Color * (Brightness * glow) + floorColor;
"""

GLINT = r"""
// UV: 0..1 across the quad.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float core = exp(-r * r * 60.0) * 2.2;
float halo = exp(-r * 4.5) * 0.8;
float streak = exp(-p.y * p.y * 900.0) * exp(-abs(p.x) * 3.0) * 0.9;
float a = atan2(p.y, p.x + 0.00001);
float spikes = pow(abs(cos(a * 2.0 + 0.785)), 120.0) * exp(-r * 5.0) * 0.8;
float round = 1.0 - smoothstep(0.55, 0.98, r);
float ends = 1.0 - smoothstep(0.75, 1.0, abs(p.x));
float3 col = Color * (halo + core * 0.6) * round + float3(1.0, 0.97, 0.92) * core * round + lerp(Color, float3(1.0, 1.0, 1.0), 0.35) * (streak * ends + spikes * round) * Spikes;
return col * Brightness;
"""

TRAIL = r"""
// UV.x: 0 at the ship .. 1 at the far end. UV.y: across.
float u = saturate(UV.x);
float v = (UV.y - 0.5) * 2.0;
float width = lerp(1.0, 0.25, u);
float w = v / width;
float body = exp(-w * w * 5.0);
float core = exp(-w * w * 60.0) * (1.0 - u) * (1.0 - u);
float fade = pow(1.0 - u, 1.6) * smoothstep(0.0, 0.04, u);
float edge = 1.0 - smoothstep(0.8, 1.0, abs(v));
float flick = 0.9 + 0.1 * sin(T * 37.0 + u * 21.0);
return (Color * body + float3(1.0, 0.95, 0.85) * core) * (fade * edge * flick * Brightness);
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


def build(name, code, sources, scalars, default_color):
    """One additive unlit two-sided material: Custom(<sources>, Color, <scalars>) -> emissive.
    sources: list of (input name, expression class, output pin)."""
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    nodes = [(n, MEL.create_material_expression(mat, cls, -1100, -300 + 140 * i), pin) for i, (n, cls, pin) in enumerate(sources)]
    color = vector(mat, "Color", default_color, -1100, 200)
    params = [(n, scalar(mat, n, v, -1100, 400 + 110 * i)) for i, (n, v) in enumerate(scalars)]

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", name)
    inputs = []
    for n in [n for n, _, _ in nodes] + ["Color"] + [n for n, _ in params]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    ok = [MEL.connect_material_expressions(e, pin, custom, n) for n, e, pin in nodes]
    ok.append(MEL.connect_material_expressions(color, "RGB", custom, "Color"))
    for n, e in params:
        ok.append(MEL.connect_material_expressions(e, "", custom, n))
    ok.append(MEL.connect_material_property(custom, "", MP.MP_EMISSIVE_COLOR))
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s, %d expressions" % (path, ok, MEL.get_num_material_expressions(mat)))
    return all(ok)


try:
    res["rim"] = build("M_HCJ_ShipRim", RIM,
                       [("N", unreal.MaterialExpressionVertexNormalWS, ""), ("V", unreal.MaterialExpressionCameraVectorWS, "")],
                       [("Brightness", 2.0), ("Far", 0.5), ("Floor", 0.03)], (1.0, 0.25, 0.1))
    res["glint"] = build("M_HCJ_ShipGlint", GLINT, [("UV", unreal.MaterialExpressionTextureCoordinate, "")],
                         [("Brightness", 3.0), ("Spikes", 1.0)], (1.0, 0.3, 0.12))
    res["trail"] = build("M_HCJ_EngineTrail", TRAIL,
                         [("UV", unreal.MaterialExpressionTextureCoordinate, ""), ("T", unreal.MaterialExpressionTime, "")],
                         [("Brightness", 3.0)], (1.0, 0.4, 0.1))
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
