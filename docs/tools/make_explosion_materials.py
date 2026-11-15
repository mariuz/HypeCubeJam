# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds the three materials of a ship's explosion under /Game/HCJ/Effects (UHCJEffectsDirector). Custom nodes only,
# no texture, nothing imported:
#   M_HCJ_Fireball    one puff of the fireball on a quad the game turns to the local view. ALPHA COMPOSITE (the
#                     fire adds light, the smoke covers what is behind it), unlit, two-sided. A ball with a billowing
#                     edge (two samples of /Game/HCJ/Cinematic/T_HCJ_CloudNoise, the cloud texture of the sky, which
#                     it carries outwards) grows fast and then slowly; it is white-hot at first, cools through yellow
#                     and orange to dark red and ends as dark smoke that thins out; a soft warm glow stays for a while.
#                     Parameters: Color (the orange of the fire), Age (0 = just went off .. 1 = gone; the game drives
#                     it), Seconds (how long the puff lives) and Fire (how many seconds of that it burns), Seed (which
#                     billows), Brightness, Smoke (0 = fire only .. 1 = a lot of smoke), Texture.
#   M_HCJ_ShockRing   the shockwave on a quad that lies in the plane of the wreck (not turned to the view): ADDITIVE,
#                     unlit, two-sided; a thin bright ring that runs outwards fast and slows, broken up along its
#                     length, with a faint wake behind the front. Parameters: Color, Age, Seed, Brightness.
#   M_HCJ_HullDebris  the hull plates (instances of the engine cube): OPAQUE, lit dark metal whose torn edges glow
#                     while the piece is hot. Heat is per-instance custom data 0 (1 = just blown off, 0 = cold).
#                     Parameter: Brightness.
# Writes a short report: Saved/Logs/make_explosion_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_explosion_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

FIREBALL = r"""
// UV: 0..1 across the quad. Tex: T_HCJ_CloudNoise. Age: 0 = just went off .. 1 = gone. Seconds: how long this puff
// lives. Fire: for how many seconds it burns. Returns premultiplied colour and opacity.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float age = saturate(Age);
float t = age * Seconds;
float grow = 1.0 - exp(-t * 3.4);                           // fast, then slow
float R = lerp(0.18, 0.74, grow) + 0.12 * age;
[branch]
if (r > 0.99)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
// Billows: two samples of the cloud texture (explicit mip: no gradients needed), the fine one pushed about by the
// coarse one; the pattern is carried outwards as the ball grows and rises a little.
// (Alpha of the texture: coarse clouds around 0.5; green: finer ones around 0.35. Together about 0.2 .. 0.9.)
float2 x = p * (0.5 / (0.5 + grow)) + float2(frac(Seed * 3.7), frac(Seed * 9.1)) + float2(0.0, t * 0.04);
float4 lo = Texture2DSampleLevel(Tex, TexSampler, x, 1.0);
float4 hi = Texture2DSampleLevel(Tex, TexSampler, x * 1.9 + (lo.rg - 0.3) * 0.08, 1.0);
float n = saturate(lo.a * 0.8 + hi.g * 0.9 - 0.15);
float edge = R * (0.55 + 0.75 * n);
float body = saturate((edge - r) / max(edge, 0.001));     // 1 in the middle, 0 at the billowing edge
float soft = smoothstep(0.0, 0.26, body);
// Heat: white-hot at first, gone after Fire seconds, hotter in the middle; the cells of the cloud keep glowing a
// while longer than the rest.
float burnt = t / max(Fire, 0.01);
float heat = saturate((1.15 - 1.15 * burnt) * (0.3 + 0.9 * body) + (n - 0.5) * 0.9 * saturate(1.25 - 0.4 * burnt)) * soft;
float3 cool = float3(0.45, 0.04, 0.0);
float3 hot = float3(1.0, 0.86, 0.55);
float3 fire = lerp(cool, Color, smoothstep(0.1, 0.45, heat));
fire = lerp(fire, hot, smoothstep(0.55, 0.95, heat));
float3 light = fire * heat * (0.5 + 2.0 * heat) * Brightness;
// Smoke: comes as the fire cools, thins out over the second half of the puff's life. Its outline is rounder than
// the flames' (the coarse clouds only).
float edgeS = R * (0.8 + 0.3 * lo.a);
float softS = smoothstep(0.0, 0.4, saturate((edgeS - r) / max(edgeS, 0.001)));
float density = softS * Smoke * smoothstep(0.1, 0.9, burnt) * (1.0 - smoothstep(0.55, 1.0, age)) * (0.5 + 0.7 * n);
float3 smoke = (float3(0.028, 0.025, 0.023) + cool * 0.18 * heat) * density;
// What stays: a soft warm glow where the ball was.
float glow = exp(-r * r * 5.0) * saturate(1.0 - 0.45 * burnt) * (1.0 - age) * 0.12 * Brightness;
float gone = (1.0 - smoothstep(0.86, 1.0, age)) * (1.0 - smoothstep(0.88, 1.0, r));
return float4((light + smoke + Color * glow) * gone, saturate(density + heat * 0.35) * gone);
"""

RING = r"""
// UV: 0..1 across the quad. Age: 0 = just went off .. 1 = over.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float age = saturate(Age);
float fade = 1.0 - age;
float ringR = 0.05 + 0.88 * (1.0 - pow(1.0 - age, 2.4));   // the front: fast, then slowing
float w = 0.010 + 0.034 * age;
float a = atan2(p.y, p.x + 0.00001);
float broken = 0.72 + 0.28 * sin(a * 9.0 + Seed * 20.0) * sin(a * 23.0 - Seed * 7.0);
float ring = exp(-pow((r - ringR) / w, 2.0)) * broken;
float wake = saturate(1.0 - (ringR - r) / 0.2) * step(r, ringR) * 0.07;  // faint light behind the front
float rim = 1.0 - smoothstep(0.92, 1.0, r);
float3 col = (Color * (ring * 1.5 + wake) + float3(1.0, 0.97, 0.9) * ring * 0.9) * fade * fade;
return col * Brightness * rim;
"""

HULL = r"""
// UV: 0..1 across a face of the cube. Heat: 1 = just blown off .. 0 = cold.
float2 q = abs(UV - 0.5) * 2.0;
float e = max(q.x, q.y);
float torn = smoothstep(0.5, 1.0, e);                        // the edges of the plate
float h = saturate(Heat);
float3 glow = lerp(float3(1.0, 0.22, 0.03), float3(1.0, 0.72, 0.32), h) * (torn * (0.2 + 7.0 * h) + 1.6 * h * h);
return glow * Brightness + float3(0.018, 0.02, 0.024);
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


def constant3(mat, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property("constant", unreal.LinearColor(value[0], value[1], value[2], 1.0))
    return e


def constant(mat, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", value)
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
    return mat, path


def finish(mat, path, ok):
    EAL.save_asset(path, only_if_is_dirty=False)
    res["log"].append("%s: connections %s, %d expressions" % (path, ok, MEL.get_num_material_expressions(mat)))
    return all(ok)


def build_fireball():
    mat, path = get_material("M_HCJ_Fireball")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1300, -300)
    color = vector(mat, "Color", (1.0, 0.5, 0.14), -1300, -150)
    params = [(n, scalar(mat, n, v, -1300, 50 + 110 * i)) for i, (n, v) in enumerate([("Age", 0.2), ("Seconds", 1.5), ("Fire", 0.6), ("Seed", 0.0), ("Brightness", 3.0), ("Smoke", 0.6)])]
    clouds = EAL.load_asset("/Game/HCJ/Cinematic/T_HCJ_CloudNoise")
    if clouds is None:
        raise RuntimeError("T_HCJ_CloudNoise is missing: run docs/tools/make_cinematic_materials.py first")
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -1300, -450)
    tex.set_editor_property("parameter_name", "Texture")
    tex.set_editor_property("texture", clouds)
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if clouds.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    node = custom(mat, FIREBALL, unreal.CustomMaterialOutputType.CMOT_FLOAT4, ["UV", "Tex", "Color"] + [n for n, _ in params], -700, 0, "M_HCJ_Fireball")
    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -300, -100)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(channel, on)
    alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -300, 100)
    for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(channel, on)
    ok = [MEL.connect_material_expressions(uv, "", node, "UV"), MEL.connect_material_expressions(tex, "", node, "Tex"), MEL.connect_material_expressions(color, "RGB", node, "Color")]
    for n, e in params:
        ok.append(MEL.connect_material_expressions(e, "", node, n))
    ok += [MEL.connect_material_expressions(node, "", rgb, ""), MEL.connect_material_expressions(node, "", alpha, ""),
           MEL.connect_material_property(rgb, "", MP.MP_EMISSIVE_COLOR), MEL.connect_material_property(alpha, "", MP.MP_OPACITY)]
    return finish(mat, path, ok)


def build_ring():
    mat, path = get_material("M_HCJ_ShockRing")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -200)
    color = vector(mat, "Color", (1.0, 0.6, 0.3), -1100, -50)
    params = [(n, scalar(mat, n, v, -1100, 150 + 110 * i)) for i, (n, v) in enumerate([("Age", 0.3), ("Seed", 0.0), ("Brightness", 3.0)])]
    node = custom(mat, RING, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["UV", "Color"] + [n for n, _ in params], -500, 0, "M_HCJ_ShockRing")
    ok = [MEL.connect_material_expressions(uv, "", node, "UV"), MEL.connect_material_expressions(color, "RGB", node, "Color")]
    for n, e in params:
        ok.append(MEL.connect_material_expressions(e, "", node, n))
    ok.append(MEL.connect_material_property(node, "", MP.MP_EMISSIVE_COLOR))
    return finish(mat, path, ok)


def build_hull():
    mat, path = get_material("M_HCJ_HullDebris")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("two_sided", False)
    # Drawn as instances: without this flag a cooked game would draw the default material instead.
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    res["log"].append("used_with_instanced_static_meshes: %s" % mat.get_editor_property("used_with_instanced_static_meshes"))
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -200)
    heat = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -1100, -50)
    heat.set_editor_property("data_index", 0)
    heat.set_editor_property("const_default_value", 0.0)
    brightness = scalar(mat, "Brightness", 1.0, -1100, 100)
    node = custom(mat, HULL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["UV", "Heat", "Brightness"], -500, 0, "M_HCJ_HullDebris")
    base = constant3(mat, (0.22, 0.23, 0.25), -500, -400)
    metal = constant(mat, 0.6, -500, -280)
    rough = constant(mat, 0.42, -500, -200)
    ok = [MEL.connect_material_expressions(uv, "", node, "UV"), MEL.connect_material_expressions(heat, "", node, "Heat"),
          MEL.connect_material_expressions(brightness, "", node, "Brightness"),
          MEL.connect_material_property(node, "", MP.MP_EMISSIVE_COLOR), MEL.connect_material_property(base, "", MP.MP_BASE_COLOR),
          MEL.connect_material_property(metal, "", MP.MP_METALLIC), MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)]
    return finish(mat, path, ok)


try:
    res["fireball"] = build_fireball()
    res["ring"] = build_ring()
    res["hull"] = build_hull()
    # What the trail of a burning piece is made of (already in the project; nothing is changed).
    for p in ["/Game/NiagaraExamples/FX_Weapons/Trails/NS_RocketTrail", "/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Small",
              "/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Medium", "/Game/NiagaraExamples/FX_Explosions/NS_Explosion"]:
        res["log"].append("%s: %s" % (p, "exists" if EAL.does_asset_exist(p) else "missing"))
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
