# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds the portal material under /Game/HCJ/Effects. Additive, unlit, two-sided, one Custom node, no texture:
#   M_HCJ_PortalSwirl  the inside of a portal on one quad (the engine's basic plane) that fills the ring: three layers of
#                      spiral arms and rings running inwards, the deeper layers smaller and shifted against the view
#                      (parallax from the camera vector in the quad's own space: it reads as a tunnel, not a disc), a bright
#                      core, a glowing rim, six sparks circling the rim, and - while Flash is above 0 - a ripple running
#                      from the centre to the rim and a brighter whole (a ship has just gone through).
#                      Parameters: Color (vector: the colour of the sector the portal leads to), Brightness, Speed,
#                      Twist (how tightly the arms wind; negative winds the other way), Flash (1 -> 0 after a passage).
# Writes a short report: Saved/Logs/make_portal_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_portal_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

SWIRL = r"""
// UV: 0..1 across the quad. Vl: camera vector in the quad's own space (z = out of the quad). T: seconds.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float2 par = Vl.xy / max(abs(Vl.z), 0.4);
float t = T * Speed;
float total = 0.0;
for (int i = 0; i < 3; i++)
{
	float depth = 0.10 + 0.16 * i;
	float2 q = p * (1.0 + 0.4 * i) - par * depth;
	float rq = length(q);
	float a = atan2(q.y, q.x + 0.00001);
	float sw = a * (2.0 + i) + Twist * (2.2 / (rq + 0.25)) - t * (0.9 + 0.5 * i);
	float arms = 0.5 + 0.5 * sin(sw + 1.7 * sin(rq * 6.0 - t * 1.3 + i));
	float rings = 0.5 + 0.5 * sin(rq * (9.0 + 4.0 * i) + t * (2.0 + i));
	float layer = arms * arms * (0.5 + 0.5 * rings) * smoothstep(1.1, 0.2, rq);
	total += layer * (1.0 - 0.25 * i);
}
float core = exp(-r * r * 12.0) * 0.7;
float rim = smoothstep(0.78, 0.96, r) * 1.5;
float mask = 1.0 - smoothstep(0.95, 1.0, r);
float sparks = 0.0;
for (int k = 0; k < 6; k++)
{
	float dir = (k % 2 == 0) ? 1.0 : -1.0;
	float ang = t * (0.7 + 0.13 * k) * dir + k * 1.047;
	float2 c = float2(cos(ang), sin(ang)) * (0.88 + 0.05 * sin(t * 2.0 + k));
	float d = length(p - c);
	sparks += exp(-d * d * 700.0);
}
float flash = saturate(Flash);
float wave = (r - (1.0 - flash)) * 8.0;
float ripple = exp(-wave * wave) * flash * 3.0;
float3 hot = lerp(Color, float3(1.0, 1.0, 1.0), 0.6);
float3 col = Color * (total * total * 0.45 + rim * 0.8 + 0.02) + hot * (core + sparks * 2.0 + ripple);
return col * (mask * Brightness * (1.0 + 1.5 * flash));
"""


def scalar(mat, name, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def build_swirl():
    name = "M_HCJ_PortalSwirl"
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1300, -300)
    camera = MEL.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1500, -150)
    local = MEL.create_material_expression(mat, unreal.MaterialExpressionTransform, -1300, -150)
    local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1300, 0)
    color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1300, 150)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.2, 1.0, 0.6, 1.0))
    params = [(n, scalar(mat, n, v, -1300, 350 + 110 * i)) for i, (n, v) in enumerate([("Brightness", 0.4), ("Speed", 1.0), ("Twist", 1.0), ("Flash", 0.0)])]

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -600, 100)
    custom.set_editor_property("code", SWIRL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", name)
    inputs = []
    for n in ["UV", "Vl", "T", "Color"] + [n for n, _ in params]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    ok = [
        MEL.connect_material_expressions(camera, "", local, ""),
        MEL.connect_material_expressions(uv, "", custom, "UV"),
        MEL.connect_material_expressions(local, "", custom, "Vl"),
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
    res["swirl"] = build_swirl()
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
