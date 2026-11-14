# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds the two planet materials under /Game/HCJ/Backdrop. No new texture: the surface uses the planet atlas of the
# engine's CelestialVault plugin (eight 256 x 256 pictures of the planets as discs, T_SolarSystemPlanets).
#   M_HCJ_PlanetSurface  unlit, opaque, on a sphere. The disc picture of the planet is projected onto the sphere along the
#                        line of sight (a body in the sky is always seen from the same side), or - Kind 1 - the surface is
#                        procedural cream / tan bands (Saturn, whose picture has its rings in it). On top: a slowly moving
#                        band / cloud pattern, a day side lit from the sector's sun with a soft terminator and a dark
#                        night side, and an atmosphere rim (fresnel) in the planet's colour that is strong on the day
#                        side and a thin line on the night side.
#                        Parameters: vectors Facing (unit, from the planet to the viewer), Right, Up (the frame of the
#                        picture), Sun (unit, towards the sun), Atmosphere (rim colour); scalars Column (atlas column
#                        0..7), Kind, Brightness, Clouds (strength of the moving pattern), Rim (strength of the rim).
#   M_HCJ_PlanetRing     unlit, translucent, two-sided, on a quad around the planet (the engine's basic plane): banded
#                        rings with a gap, lit by the sun, with the shadow of the planet across them.
#                        Parameters: vectors Color, Sun; scalars Inner (inner radius as a fraction of the quad's half
#                        width), PlanetRadius (uu, for the shadow), Brightness, Opacity, Seed.
# Writes a short report: Saved/Logs/make_planet_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Backdrop"
ATLAS = "/CelestialVault/Textures/T_SolarSystemPlanets"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_planet_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

SURFACE_UV = r"""
// The disc picture seen along the line of sight: the middle of the picture is the point of the sphere nearest the viewer.
float3 n = normalize(N);
float2 d = float2(dot(n, Right), -dot(n, Up));
return float2((Column + 0.5 + d.x * 0.468) / 8.0, 0.5 + d.y * 0.468);
"""

SURFACE = r"""
float3 n = normalize(N);
float3 l = float3(dot(n, Facing), dot(n, Right), dot(n, Up));
float ndv = saturate(l.x);
float lat = l.z;
float lon = atan2(l.y, l.x + 0.0001);
float3 base = Tex;
if (Kind > 0.5)
{
	float b = 0.5 + 0.5 * sin(lat * 24.0 + 0.7 * sin(lat * 67.0));
	float b2 = 0.5 + 0.5 * sin(lat * 9.0 + 1.3);
	base = lerp(float3(0.62, 0.47, 0.27), float3(0.92, 0.82, 0.6), b * 0.55 + b2 * 0.45);
	base *= 0.85 + 0.15 * (1.0 - abs(lat));
}
// a slow drift of bands and cloud streaks around the axis
float t = T * 0.02;
float drift = sin(lon * 4.0 + t * 3.0 + 3.0 * sin(lat * 7.0 + t)) * sin(lat * 15.0 + lon * 1.5 - t * 2.0);
float streaks = sin(lat * 41.0 + 2.0 * sin(lon * 3.0 - t * 4.0));
base *= 1.0 + Clouds * (0.14 * drift + 0.05 * streaks);
// the picture is a photograph with its own limb shading: lift the limb a little before lighting it ourselves
base *= 1.0 + 0.35 * pow(1.0 - ndv, 2.0);
float ndl = dot(n, normalize(Sun));
float day = smoothstep(-0.10, 0.45, ndl);
float3 lit = base * (0.035 + 1.2 * day);
float fres = pow(1.0 - ndv, 2.5);
float glow = fres * (0.10 + 1.5 * smoothstep(-0.35, 0.35, ndl));
// a soft scattering band along the terminator
float dusk = exp(-ndl * ndl * 30.0) * 0.12;
float3 col = lit + Atmosphere * (glow * Rim + dusk * Rim * 0.5);
return col * Brightness;
"""

RING_COLOR = r"""
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float x = saturate((r - Inner) / max(1.0 - Inner, 0.001));
float bands = 0.62 + 0.2 * sin(x * 38.0 + Seed) + 0.12 * sin(x * 97.0 + 1.7 * Seed) + 0.06 * sin(x * 211.0);
// the shadow of the planet: points behind it (seen from the sun) within its radius of the sun line
float3 rel = (Wp - Center) / max(PlanetRadius, 1.0);
float3 s = normalize(Sun);
float along = dot(rel, s);
float off = length(rel - along * s);
float shadow = 1.0 - (1.0 - smoothstep(0.92, 1.06, off)) * step(along, 0.0) * 0.94;
float3 warm = lerp(Color * 0.7, Color, x);
return warm * (bands * shadow * Brightness);
"""

RING_OPACITY = r"""
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float x = (r - Inner) / max(1.0 - Inner, 0.001);
float inside = smoothstep(0.0, 0.03, x) * (1.0 - smoothstep(0.96, 1.0, x));
float bands = 0.62 + 0.2 * sin(x * 38.0 + Seed) + 0.12 * sin(x * 97.0 + 1.7 * Seed) + 0.06 * sin(x * 211.0);
float gapA = (x - 0.60) * 26.0;
float gapB = (x - 0.86) * 60.0;
float gaps = (1.0 - 0.92 * exp(-gapA * gapA)) * (1.0 - 0.6 * exp(-gapB * gapB));
float dense = smoothstep(0.0, 0.25, x) * 0.5 + 0.5;
return saturate(inside * bands * gaps * dense * Opacity);
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


def custom(mat, name, code, output, names, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property("code", code)
    e.set_editor_property("output_type", output)
    e.set_editor_property("description", name)
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    e.set_editor_property("inputs", inputs)
    return e


def new_material(name):
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    return mat, path


def build_surface():
    mat, path = new_material("M_HCJ_PlanetSurface")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    normal = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1700, -300)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1700, -150)
    vectors = {n: vector(mat, n, v, -1700, 100 * i) for i, (n, v) in enumerate([
        ("Facing", (1.0, 0.0, 0.0)), ("Right", (0.0, 1.0, 0.0)), ("Up", (0.0, 0.0, 1.0)), ("Sun", (0.5, -0.6, 0.6)), ("Atmosphere", (0.4, 0.6, 1.0))])}
    scalars = {n: scalar(mat, n, v, -1700, 600 + 100 * i) for i, (n, v) in enumerate([
        ("Column", 4.0), ("Kind", 0.0), ("Brightness", 1.0), ("Clouds", 1.0), ("Rim", 1.0)])}

    uv = custom(mat, "planet atlas uv", SURFACE_UV, unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["N", "Right", "Up", "Column"], -1200, -300)
    ok = [MEL.connect_material_expressions(normal, "", uv, "N"),
          MEL.connect_material_expressions(vectors["Right"], "RGB", uv, "Right"),
          MEL.connect_material_expressions(vectors["Up"], "RGB", uv, "Up"),
          MEL.connect_material_expressions(scalars["Column"], "", uv, "Column")]
    sample = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -900, -300)
    sample.set_editor_property("texture", EAL.load_asset(ATLAS))
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    ok.append(MEL.connect_material_expressions(uv, "", sample, "UVs"))

    names = ["N", "Tex", "T", "Facing", "Right", "Up", "Sun", "Atmosphere", "Kind", "Brightness", "Clouds", "Rim"]
    shade = custom(mat, "planet surface", SURFACE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, -500, 0)
    ok += [MEL.connect_material_expressions(normal, "", shade, "N"),
           MEL.connect_material_expressions(sample, "RGB", shade, "Tex"),
           MEL.connect_material_expressions(time, "", shade, "T")]
    for n in ("Facing", "Right", "Up", "Sun", "Atmosphere"):
        ok.append(MEL.connect_material_expressions(vectors[n], "RGB", shade, n))
    for n in ("Kind", "Brightness", "Clouds", "Rim"):
        ok.append(MEL.connect_material_expressions(scalars[n], "", shade, n))
    ok.append(MEL.connect_material_property(shade, "", MP.MP_EMISSIVE_COLOR))
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s" % (path, ok))
    return all(ok)


def build_ring():
    mat, path = new_material("M_HCJ_PlanetRing")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1300, -300)
    position = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1300, -150)
    center = MEL.create_material_expression(mat, unreal.MaterialExpressionObjectPositionWS, -1300, 0)
    color = vector(mat, "Color", (0.85, 0.75, 0.6), -1300, 150)
    sun = vector(mat, "Sun", (0.5, -0.6, 0.6), -1300, 300)
    scalars = {n: scalar(mat, n, v, -1300, 500 + 100 * i) for i, (n, v) in enumerate([
        ("Inner", 0.55), ("PlanetRadius", 100000.0), ("Brightness", 1.0), ("Opacity", 0.9), ("Seed", 0.0)])}

    shade = custom(mat, "planet ring colour", RING_COLOR, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   ["UV", "Wp", "Center", "Color", "Sun", "Inner", "PlanetRadius", "Brightness", "Seed"], -600, -100)
    alpha = custom(mat, "planet ring opacity", RING_OPACITY, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "Inner", "Opacity", "Seed"], -600, 400)
    ok = [MEL.connect_material_expressions(uv, "", shade, "UV"),
          MEL.connect_material_expressions(position, "", shade, "Wp"),
          MEL.connect_material_expressions(center, "", shade, "Center"),
          MEL.connect_material_expressions(color, "RGB", shade, "Color"),
          MEL.connect_material_expressions(sun, "RGB", shade, "Sun"),
          MEL.connect_material_expressions(uv, "", alpha, "UV")]
    for n in ("Inner", "PlanetRadius", "Brightness", "Seed"):
        ok.append(MEL.connect_material_expressions(scalars[n], "", shade, n))
    for n in ("Inner", "Opacity", "Seed"):
        ok.append(MEL.connect_material_expressions(scalars[n], "", alpha, n))
    ok.append(MEL.connect_material_property(shade, "", MP.MP_EMISSIVE_COLOR))
    ok.append(MEL.connect_material_property(alpha, "", MP.MP_OPACITY))
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s" % (path, ok))
    return all(ok)


try:
    res["surface"] = build_surface()
    res["ring"] = build_ring()
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
