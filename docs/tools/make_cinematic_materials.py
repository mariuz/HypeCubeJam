# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>), after
# docs/tools/make_cinematic_textures.py (plain Python) has written SourceAssets/Cinematic/*.png.
# The "cinematic look" pass (devlog item 26). Imports three small textures to /Game/HCJ/Cinematic and (re)builds:
#   /Game/HCJ/Backdrop/M_HCJ_Nebula      additive instanced card: cloud = a few samples of T_HCJ_CloudNoise at different
#                                        scales (warped by each other), bright veins, dark dust lanes and - Core > 0 - a
#                                        white-hot light BEHIND the cloud that the dust lanes cut into (the backlit look).
#                                        Six custom floats per instance: R, G, B, seed, Core (0..1), Soft (0 = detailed
#                                        cloud: three samples; 1 = a soft puff: one sample). Scalar Intensity. No
#                                        per-pixel noise maths.
#   /Game/Megascans/Asteroids/Materials/M_Asteroid   the rock material, rebuilt with the same parameters as before
#                                        (BaseColor, Normal, Roughness, AO, "Base Color Gain") plus a fresnel RIM LIGHT in
#                                        the emissive ("Rim Color", "Rim Strength", "Rim Power", "Rim Dir" = where the
#                                        back light of the sector stands) and CRATERS from T_HCJ_Craters laid over the
#                                        mesh UVs ("Crater Tiling", "Crater Strength": bends the normal, darkens the bowls).
#   /Game/HCJ/Cinematic/M_HCJ_DecorRock  the same rock, cheaper (base colour + normal only), for the decorative pebbles
#                                        and far boulders: a vertex nearer to the camera than "Near Cull" uu collapses
#                                        into the camera (the pebble is gone), so nothing small flashes across the view.
#                                        MI_HCJ_DecorRock_0..3 carry the textures of the four boulders.
#   /Game/HCJ/Effects/M_HCJ_EngineJet    the engine jet on a quad (U = 0 at the nozzle .. 1 at the tip), rolled to the
#                                        view: white-hot core, coloured body, running shock diamonds, taper.
#   /Game/HCJ/Effects/M_HCJ_ImpactStar   a hit: white core, halo and a burst of thin radial streaks of unequal length
#                                        that fly outwards. Parameters Color, Brightness, Age (0..1), Seed.
#   /Game/HCJ/Effects/M_HCJ_PortalRunes  one glyph of T_HCJ_Runes per instance (custom float 0 = glyph 0..15) on a quad,
#                                        glowing, breathing one after the other around the ring. Color, Brightness, Flash.
#   /Game/HCJ/Effects/M_HCJ_PortalRays   rays of light radiating from behind a portal, on a quad in the portal's plane.
#                                        Color, Brightness, Inner (ring radius as a share of half the quad), Flash.
# All effect materials are unlit and additive (no scene colour, no custom depth: they also work in the mobile renderer).
# Writes a short report: Saved/Logs/make_cinematic_materials.json
import json
import os
import traceback

import unreal

SRC = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceAssets", "Cinematic"))
TEX = "/Game/HCJ/Cinematic"
FX = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_cinematic_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType
TC = unreal.TextureCompressionSettings
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

NEBULA = r"""
// UV: 0..1 across the card. Tex: T_HCJ_CloudNoise. Col: colour of the cloud. Seed, Core, Soft: per instance. T: seconds.
// Explicit mip levels everywhere (the cards are larger than the screen, the texture is magnified or nearly so): no
// gradients are needed, so the soft cards can leave early after ONE sample - they are most of what covers the screen.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float2 o = float2(frac(Seed * 0.137), frac(Seed * 0.731));
float2 q = UV * 0.8 + o;
float edge = smoothstep(1.0, 0.15, r);
edge *= edge;
float4 lo = Texture2DSampleLevel(Tex, TexSampler, q * 0.45, 0.0);
[branch]
if (Soft > 0.5)
{
	return Col * (lo.a * lo.a * lo.a * (0.95 * edge * Intensity));
}
float2 w = (lo.rg - 0.5) * 0.22;
float4 mid = Texture2DSampleLevel(Tex, TexSampler, q * 1.15 + w, 0.0);
float4 hi = Texture2DSampleLevel(Tex, TexSampler, q * 2.9 - w * 0.7 + float2(T * 0.0015, 0.0), 1.0);
float detail = mid.r * (0.45 + 1.1 * hi.g) + lo.a * 0.22;
float cloud = detail * detail * 0.9;
float veins = hi.b * (0.3 + mid.r);
float lanes = saturate(1.15 - 2.6 * mid.g * lo.g);
// The back light: a small white-hot core; around it the cloud itself is lit from behind (its structure stays, the
// dust lanes cut into it), and a faint wide skirt.
float back = Core * exp(-r * r * 120.0) * 1.6;
float lit = Core * (exp(-r * r * 12.0) * 0.9 + exp(-r * 3.0) * 0.08) * (0.04 + 2.6 * mid.r * (0.3 + hi.g) * (0.4 + lo.r));
float3 hot = lerp(Col, float3(1.0, 1.0, 1.0), 0.62);
float3 c = Col * ((cloud + lit) * (0.25 + 0.75 * lanes)) + hot * (veins * 0.6 + back * (0.5 + 0.9 * mid.r) + lit * 0.12);
return c * (edge * Intensity);
"""

ROCK_BASE = r"""
// B: base colour, G: gain, C: crater texel (b = cavity, 0.5 neutral), S: crater strength.
return B * G * lerp(1.0, 0.35 + 1.3 * C.b, saturate(S));
"""

ROCK_NORMAL = r"""
// N: tangent-space normal of the rock, C: crater texel (rg = normal, 0.5 flat), S: crater strength.
float3 n = N;
n.xy += (C.rg - 0.5) * (3.0 * S);
return normalize(n);
"""

ROCK_RIM = r"""
// PN: per-pixel world normal, V: world vector to the camera, L: unit vector towards the back light.
float3 n = normalize(PN);
float f = 1.0 - saturate(dot(n, normalize(V)));
float side = 0.3 + 0.7 * saturate(dot(n, normalize(L)) * 0.5 + 0.5);
return RimColor * (pow(f, RimPower) * RimStrength * side);
"""

NEAR_CULL = r"""
// WP: world position of the vertex, CP: camera position. A vertex nearer than Near collapses into the camera.
float d = distance(WP, CP);
return (CP - WP) * step(d, Near);
"""

JET = r"""
// UV.x: 0 at the nozzle .. 1 at the tip. UV.y: across.
float u = saturate(UV.x);
float v = (UV.y - 0.5) * 2.0;
float t = T + Phase;
float taper = lerp(1.0, 0.16, pow(u, 0.7));
float w = v / taper;
float flick = 0.88 + 0.12 * sin(t * 53.0 + u * 9.0) * sin(t * 31.7);
float body = exp(-w * w * 4.5);
float core = exp(-w * w * 34.0) * pow(1.0 - u, 1.4);
float diamonds = 0.72 + 0.28 * sin(u * 34.0 - t * 30.0);
float along = pow(1.0 - u, 1.25) * smoothstep(0.0, 0.03, u);
float edge = 1.0 - smoothstep(0.75, 1.0, abs(v));
float3 col = Color * (body * (0.5 + 0.5 * diamonds)) + float3(1.0, 1.0, 0.96) * (core * 1.7);
return col * (along * edge * flick * Brightness);
"""

IMPACT = r"""
// UV: 0..1 across the quad. Age: 0 = just hit .. 1 = over.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float age = saturate(Age);
float fade = 1.0 - age;
float pop = fade * fade;
float a = atan2(p.y, p.x + 0.00001) / 6.2831853 + 0.5;
float core = exp(-r * r * (45.0 + 160.0 * age)) * 2.6 * pop;
float halo = exp(-r * 4.5) * 0.45 * pop;
float streaks = 0.0;
for (int i = 0; i < 2; i++)
{
	float n = 11.0 + 8.0 * i;
	float s = a * n + Seed * (3.0 + 4.0 * i);
	float id = floor(s);
	float fr = frac(s) - 0.5;
	float h = frac(sin(id * 12.9898 + Seed * 78.233 + i * 37.1) * 43758.5453);
	float len = (0.22 + 0.74 * h) * (0.45 + 0.55 * sqrt(age + 0.04)) * (1.0 - 0.35 * i);
	float thin = exp(-fr * fr * (70.0 + 260.0 * h));
	streaks += thin * smoothstep(len, len * 0.15, r) * smoothstep(0.0, 0.05, r) * (1.0 - 0.4 * i);
}
streaks *= fade * 1.5;
float edge = 1.0 - smoothstep(0.85, 1.0, r);
float3 col = Color * (halo + streaks) + float3(1.0, 0.95, 0.82) * (core + streaks * 0.6);
return col * (Brightness * edge);
"""

RUNES = r"""
// UV: 0..1 across the quad. Tex: T_HCJ_Runes (8 x 2 glyphs). Glyph: 0..15 (per instance). T: seconds.
float g = floor(Glyph + 0.5);
float2 cell = float2(fmod(g, 8.0), floor(g / 8.0));
float2 uv = (cell + saturate(UV)) / float2(8.0, 2.0);
float m = Texture2DSample(Tex, TexSampler, uv).r;
float breath = 0.62 + 0.38 * sin(T * 1.6 + g * 1.9);
float flash = saturate(Flash);
float3 hot = lerp(Color, float3(1.0, 1.0, 1.0), 0.22 * m);
return hot * (m * Brightness * (breath + 2.0 * flash));
"""

RAYS = r"""
// UV: 0..1 across the quad (the portal's plane). Inner: radius of the ring as a share of half the quad.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float a = atan2(p.y, p.x + 0.00001);
float t = T;
float rays = pow(0.5 + 0.5 * sin(a * 7.0 + t * 0.31), 6.0)
	+ pow(0.5 + 0.5 * sin(a * 13.0 - t * 0.23 + 1.7), 10.0) * 0.7
	+ pow(0.5 + 0.5 * sin(a * 23.0 + t * 0.41 + 0.6), 16.0) * 0.45;
float reach = saturate(1.0 - (r - Inner) / (1.0 - Inner));
float radial = smoothstep(Inner * 0.8, Inner * 1.02, r) * pow(reach, 2.4);
float halo = exp(-pow((r - Inner) / 0.09, 2.0)) * 0.35;
float flash = saturate(Flash);
return Color * ((rays * radial + halo) * Brightness * (1.0 + 2.0 * flash));
"""


def import_texture(name, srgb):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SRC, name + ".png"))
    task.set_editor_property("destination_path", TEX)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    tex = EAL.load_asset(TEX + "/" + name)
    tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("compression_settings", TC.TC_DEFAULT)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    EAL.save_asset(TEX + "/" + name, only_if_is_dirty=False)
    res["log"].append("texture %s: %d x %d" % (name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    return tex


def sampler_type_for(tex):
    cs = tex.get_editor_property("compression_settings")
    srgb = tex.get_editor_property("srgb")
    if cs == TC.TC_NORMALMAP:
        return ST.SAMPLERTYPE_NORMAL
    if cs == TC.TC_MASKS:
        return ST.SAMPLERTYPE_MASKS
    if cs == TC.TC_GRAYSCALE:
        return ST.SAMPLERTYPE_GRAYSCALE if srgb else ST.SAMPLERTYPE_LINEAR_GRAYSCALE
    if cs == TC.TC_ALPHA:
        return ST.SAMPLERTYPE_ALPHA
    return ST.SAMPLERTYPE_COLOR if srgb else ST.SAMPLERTYPE_LINEAR_COLOR


def new_material(path, name, blend, unlit, two_sided, instanced):
    full = path + "/" + name
    if EAL.does_asset_exist(full):
        mat = EAL.load_asset(full)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("two_sided", two_sided)
    mat.set_editor_property("used_with_instanced_static_meshes", instanced)
    return mat, full


def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(value[0], value[1], value[2], 1.0))


def custom(mat, description, code, inputs, out_type, x, y):
    """inputs: list of (input name, expression, output pin). Returns (node, list of connection results)."""
    c = node(mat, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type, description=description)
    pins = []
    for n, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        pins.append(ci)
    c.set_editor_property("inputs", pins)
    ok = [MEL.connect_material_expressions(e, pin, c, n) for n, e, pin in inputs]
    return c, ok


F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


def finish(mat, full, ok):
    # no MEL.recompile_material: a commandlet has no renderer; the material compiles on first load
    EAL.save_asset(full, only_if_is_dirty=False)
    res["log"].append("%s: connections %s, %d expressions" % (full, ok, MEL.get_num_material_expressions(mat)))
    return all(ok)


def build_fx(name, code, sources, scalars, default_color, instanced=False, texture=None, per_instance=()):
    """Additive unlit two-sided: Custom(<sources>, [Tex], Color, <per instance floats>, <scalars>) -> emissive."""
    mat, full = new_material(FX, name, unreal.BlendMode.BLEND_ADDITIVE, True, True, instanced)
    inputs = [(n, node(mat, cls, -1100, -400 + 130 * i), pin) for i, (n, cls, pin) in enumerate(sources)]
    if texture is not None:
        inputs.append(("Tex", node(mat, unreal.MaterialExpressionTextureObjectParameter, -1100, 40, parameter_name="Texture", texture=texture,
                                   sampler_type=sampler_type_for(texture)), ""))
    inputs.append(("Color", vector(mat, "Color", default_color, -1100, 200), "RGB"))
    for i, n in enumerate(per_instance):
        inputs.append((n, node(mat, unreal.MaterialExpressionPerInstanceCustomData, -1400, 100 + 110 * i, data_index=i, const_default_value=0.0), ""))
    for i, (n, v) in enumerate(scalars):
        inputs.append((n, scalar(mat, n, v, -1100, 400 + 110 * i), ""))
    c, ok = custom(mat, name, code, inputs, F3, -500, 100)
    ok.append(MEL.connect_material_property(c, "", MP.MP_EMISSIVE_COLOR))
    return finish(mat, full, ok)


def build_nebula(cloud_tex):
    mat, full = new_material("/Game/HCJ/Backdrop", "M_HCJ_Nebula", unreal.BlendMode.BLEND_ADDITIVE, True, True, True)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -1500, -400)
    time = node(mat, unreal.MaterialExpressionTime, -1500, -300)
    tex = node(mat, unreal.MaterialExpressionTextureObjectParameter, -1500, -200, parameter_name="Clouds", texture=cloud_tex,
               sampler_type=sampler_type_for(cloud_tex))
    data = [node(mat, unreal.MaterialExpressionPerInstanceCustomData, -1800, -50 + 110 * i, data_index=i, const_default_value=d)
            for i, d in enumerate([0.3, 0.6, 0.9, 0.0, 0.0, 0.0])]
    rg = node(mat, unreal.MaterialExpressionAppendVector, -1500, 0)
    rgb = node(mat, unreal.MaterialExpressionAppendVector, -1300, 0)
    intensity = scalar(mat, "Intensity", 1.0, -1500, 500)
    ok = [
        MEL.connect_material_expressions(data[0], "", rg, "A"),
        MEL.connect_material_expressions(data[1], "", rg, "B"),
        MEL.connect_material_expressions(rg, "", rgb, "A"),
        MEL.connect_material_expressions(data[2], "", rgb, "B"),
    ]
    c, ok2 = custom(mat, "M_HCJ_Nebula", NEBULA, [("UV", uv, ""), ("Tex", tex, ""), ("Col", rgb, ""), ("Seed", data[3], ""), ("Core", data[4], ""),
                                                  ("Soft", data[5], ""), ("T", time, ""), ("Intensity", intensity, "")], F3, -700, 0)
    ok += ok2
    ok.append(MEL.connect_material_property(c, "", MP.MP_EMISSIVE_COLOR))
    return finish(mat, full, ok)


ROCK_DIR = "/Game/Megascans/Asteroids/Materials"
ROCK_INSTANCES = ["MI_Icelandic_Boulder_tazj1", "MI_Icelandic_Boulder_tdpsdbcda", "MI_Icelandic_Porous_Rocks_skxqf", "MI_Nordic_Beach_Boulder_venjcjyqx"]


def rock_textures(instance_name):
    mi = EAL.load_asset(ROCK_DIR + "/" + instance_name)
    return {str(n): MEL.get_material_instance_texture_parameter_value(mi, n) for n in MEL.get_texture_parameter_names(mi)}


def tex_param(mat, name, tex, x, y):
    return node(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=tex, sampler_type=sampler_type_for(tex))


def rim_nodes(mat, x, y):
    pn = node(mat, unreal.MaterialExpressionPixelNormalWS, x, y)
    cam = node(mat, unreal.MaterialExpressionCameraVectorWS, x, y + 100)
    rim_dir = vector(mat, "Rim Dir", (0.0, 0.0, 1.0), x, y + 200)
    rim_color = vector(mat, "Rim Color", (0.45, 0.75, 1.0), x, y + 400)
    rim_strength = scalar(mat, "Rim Strength", 0.5, x, y + 600)
    rim_power = scalar(mat, "Rim Power", 3.0, x, y + 700)
    return custom(mat, "RockRim", ROCK_RIM, [("PN", pn, ""), ("V", cam, ""), ("L", rim_dir, "RGB"), ("RimColor", rim_color, "RGB"),
                                             ("RimStrength", rim_strength, ""), ("RimPower", rim_power, "")], F3, x + 500, y + 200)


def build_rock(crater_tex, defaults):
    mat, full = new_material(ROCK_DIR, "M_Asteroid", unreal.BlendMode.BLEND_OPAQUE, False, False, True)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -1900, 300)
    base = tex_param(mat, "BaseColor", defaults["BaseColor"], -1500, -500)
    normal = tex_param(mat, "Normal", defaults["Normal"], -1500, -250)
    rough = tex_param(mat, "Roughness", defaults["Roughness"], -1500, 0)
    ao = tex_param(mat, "AO", defaults["AO"], -1500, 250)
    gain = vector(mat, "Base Color Gain", (1.0, 1.0, 1.0), -1500, 500)
    tiling = scalar(mat, "Crater Tiling", 2.0, -1900, 420)
    strength = scalar(mat, "Crater Strength", 0.6, -1500, 950)
    crater_uv = node(mat, unreal.MaterialExpressionMultiply, -1700, 700)
    craters = tex_param(mat, "Craters", crater_tex, -1500, 700)
    ok = [
        MEL.connect_material_expressions(uv, "", crater_uv, "A"),
        MEL.connect_material_expressions(tiling, "", crater_uv, "B"),
        MEL.connect_material_expressions(crater_uv, "", craters, "UVs"),
    ]
    c_base, ok2 = custom(mat, "RockBase", ROCK_BASE, [("B", base, "RGB"), ("G", gain, "RGB"), ("C", craters, "RGB"), ("S", strength, "")], F3, -800, -400)
    c_normal, ok3 = custom(mat, "RockNormal", ROCK_NORMAL, [("N", normal, "RGB"), ("C", craters, "RGB"), ("S", strength, "")], F3, -800, -100)
    c_rim, ok4 = rim_nodes(mat, -1500, 1100)
    ok += ok2 + ok3 + ok4
    ok += [
        MEL.connect_material_property(c_base, "", MP.MP_BASE_COLOR),
        MEL.connect_material_property(c_normal, "", MP.MP_NORMAL),
        MEL.connect_material_property(rough, "R", MP.MP_ROUGHNESS),
        MEL.connect_material_property(ao, "R", MP.MP_AMBIENT_OCCLUSION),
        MEL.connect_material_property(c_rim, "", MP.MP_EMISSIVE_COLOR),
    ]
    return finish(mat, full, ok)


def build_decor_rock(defaults):
    mat, full = new_material(TEX, "M_HCJ_DecorRock", unreal.BlendMode.BLEND_OPAQUE, False, False, True)
    base = tex_param(mat, "BaseColor", defaults["BaseColor"], -1500, -500)
    normal = tex_param(mat, "Normal", defaults["Normal"], -1500, -250)
    gain = vector(mat, "Base Color Gain", (1.0, 1.0, 1.0), -1500, 0)
    mul = node(mat, unreal.MaterialExpressionMultiply, -900, -400)
    rough = node(mat, unreal.MaterialExpressionConstant, -900, -100, r=0.9)
    wp = node(mat, unreal.MaterialExpressionWorldPosition, -1500, 300)
    cp = node(mat, unreal.MaterialExpressionCameraPositionWS, -1500, 400)
    near = scalar(mat, "Near Cull", 700.0, -1500, 500)
    c_cull, ok = custom(mat, "NearCull", NEAR_CULL, [("WP", wp, ""), ("CP", cp, ""), ("Near", near, "")], F3, -900, 350)
    c_rim, ok2 = rim_nodes(mat, -1500, 800)
    ok += ok2
    ok += [
        MEL.connect_material_expressions(base, "RGB", mul, "A"),
        MEL.connect_material_expressions(gain, "RGB", mul, "B"),
        MEL.connect_material_property(mul, "", MP.MP_BASE_COLOR),
        MEL.connect_material_property(normal, "RGB", MP.MP_NORMAL),
        MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS),
        MEL.connect_material_property(c_rim, "", MP.MP_EMISSIVE_COLOR),
        MEL.connect_material_property(c_cull, "", MP.MP_WORLD_POSITION_OFFSET),
    ]
    good = finish(mat, full, ok)
    # one instance per boulder, with that boulder's textures
    for index, instance_name in enumerate(ROCK_INSTANCES):
        name = "MI_HCJ_DecorRock_%d" % index
        path = TEX + "/" + name
        if EAL.does_asset_exist(path):
            mi = EAL.load_asset(path)
        else:
            mi = tools.create_asset(name, TEX, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, mat)
        textures = rock_textures(instance_name)
        for n in ("BaseColor", "Normal"):
            MEL.set_material_instance_texture_parameter_value(mi, n, textures[n])
        MEL.update_material_instance(mi)
        EAL.save_asset(path, only_if_is_dirty=False)
        res["log"].append("%s <- %s" % (path, instance_name))
    return good


try:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    clouds = import_texture("T_HCJ_CloudNoise", False)
    runes = import_texture("T_HCJ_Runes", False)
    craters = import_texture("T_HCJ_Craters", False)
    TEXCOORD = ("UV", unreal.MaterialExpressionTextureCoordinate, "")
    TIME = ("T", unreal.MaterialExpressionTime, "")
    res["nebula"] = build_nebula(clouds)
    defaults = rock_textures(ROCK_INSTANCES[0])
    res["rock"] = build_rock(craters, defaults)
    res["decor"] = build_decor_rock(defaults)
    res["jet"] = build_fx("M_HCJ_EngineJet", JET, [TEXCOORD, TIME], [("Brightness", 3.0), ("Phase", 0.0)], (0.2, 1.0, 0.4))
    res["impact"] = build_fx("M_HCJ_ImpactStar", IMPACT, [TEXCOORD], [("Brightness", 3.0), ("Age", 0.0), ("Seed", 0.0)], (1.0, 0.6, 0.2))
    res["runes"] = build_fx("M_HCJ_PortalRunes", RUNES, [TEXCOORD, TIME], [("Brightness", 2.0), ("Flash", 0.0)], (0.2, 1.0, 0.6),
                            instanced=True, texture=runes, per_instance=("Glyph",))
    res["rays"] = build_fx("M_HCJ_PortalRays", RAYS, [TEXCOORD, TIME], [("Brightness", 0.5), ("Inner", 0.3), ("Flash", 0.0)], (0.2, 1.0, 0.6))
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
