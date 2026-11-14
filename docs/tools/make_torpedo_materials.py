# Editor Python (run with UnrealEditor-Cmd <project> -run=pythonscript -script=<this file>).
# Builds the two materials of the photon torpedo under /Game/HCJ/Effects. Both are additive, unlit, two-sided and
# drawn on ONE quad (the engine's basic plane) that the game turns towards the local view; each is one Custom node
# working on the quad's texture coordinates, so there is no texture and nothing to import:
#   M_HCJ_TorpedoStar   the torpedo in flight: a white-hot core in a red-orange ball, four long and four short spikes
#                       that turn slowly and flicker, thin needles turning the other way.
#                       Parameters: Color (vector), Brightness, Phase (offsets the flicker per torpedo).
#   M_HCJ_TorpedoBlast  the detonation: a flash that collapses, a shockwave ring that runs outwards and fades, a
#                       star-burst in the first moments. Parameters: Color (vector), Brightness, Age (0 = just went
#                       off .. 1 = over; the game drives it over about 0.6 s).
# Also lists the user parameters of the Niagara systems the torpedo uses and the lengths of the sounds it could use
# (nothing is imported; all of them are already in the project).
# Writes a short report next to the project log: Saved/Logs/make_torpedo_materials.json
import json
import traceback

import unreal

DST = "/Game/HCJ/Effects"
OUT = unreal.Paths.project_saved_dir() + "Logs/make_torpedo_materials.json"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
res = {"log": []}

STAR = r"""
// UV: 0..1 across the quad. T: seconds. Phase: per torpedo.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float t = T + Phase;
float a = atan2(p.y, p.x + 0.00001) + t * 0.9;
float beat = 0.82 + 0.18 * sin(t * 31.0) * sin(t * 17.3 + 1.1);
float core = exp(-r * r * 90.0) * 2.4;                      // white-hot core
float ball = exp(-r * r * 14.0) * 1.3;                      // the red-orange ball around it
float halo = exp(-r * 3.4) * 0.22;                          // soft glow
float c4 = abs(cos(a * 2.0));
float s4 = abs(sin(a * 2.0));
float longSpike = pow(c4, 60.0) * exp(-r * 2.6) * (0.75 + 0.25 * sin(t * 43.0 + a * 3.0));
float shortSpike = pow(s4, 90.0) * exp(-r * 5.0) * (0.6 + 0.4 * sin(t * 57.0 + 2.0));
float a2 = atan2(p.y, p.x + 0.00001) - t * 2.3;
float needles = pow(abs(cos(a2 * 3.0)), 200.0) * exp(-r * 4.0) * 0.6;
float spikes = (longSpike * 1.6 + shortSpike * 1.1 + needles) * beat;
float edge = 1.0 - smoothstep(0.82, 1.0, r);                // nothing at the edge of the quad
float3 col = Color * (ball + halo + spikes) + float3(1.0, 0.92, 0.75) * (core + spikes * 0.25);
return col * Brightness * edge * beat;
"""

BLAST = r"""
// UV: 0..1 across the quad. Age: 0 = just went off .. 1 = over.
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float age = saturate(Age);
float fade = 1.0 - age;
float a = atan2(p.y, p.x + 0.00001);
float flash = exp(-r * r * (9.0 + 60.0 * age)) * fade * fade * 1.5;            // the fireball, collapsing
float ringR = 0.08 + 0.80 * sqrt(age);                                           // the shockwave: fast, then slowing
float w = 0.025 + 0.045 * age;
float ring = exp(-pow((r - ringR) / w, 2.0)) * fade * 1.8;
float inner = exp(-pow((r - ringR * 0.6) / (w * 0.7), 2.0)) * fade * fade * 0.6;
float fill = saturate(1.0 - r / max(ringR, 0.001)) * fade * fade * 0.25;        // faint light inside the ring
float rays = pow(abs(cos(a * 4.0)), 40.0) * exp(-r * 3.0) * fade * fade * fade * 2.0;   // the star-burst of the first frames
float edge = 1.0 - smoothstep(0.9, 1.0, r);
float3 col = Color * (ring + inner + fill + flash * 0.8 + rays) + float3(1.0, 0.95, 0.8) * (flash * 0.5 + ring * 0.2);
return col * Brightness * edge;
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


def build(name, code, scalars):
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
    color = vector(mat, "Color", (1.0, 0.3, 0.06), -1100, 100)
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
    res["star"] = build("M_HCJ_TorpedoStar", STAR, [("Brightness", 3.0), ("Phase", 0.0)])
    res["blast"] = build("M_HCJ_TorpedoBlast", BLAST, [("Brightness", 4.0), ("Age", 0.3)])

    # What the Niagara systems expose (for the colour of the trail), and how long the candidate sounds are.
    for p in ["/Game/NiagaraExamples/FX_Weapons/Trails/NS_SimpleRibbonTrail", "/Game/NiagaraExamples/FX_Weapons/Trails/NS_RocketTrail",
              "/Game/NiagaraExamples/FX_Weapons/Trails/NS_BulletTracer", "/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Small",
              "/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Medium"]:
        a = EAL.load_asset(p) if EAL.does_asset_exist(p) else None
        if a is None:
            res["log"].append("%s: missing" % p)
            continue
        try:
            store = a.get_editor_property("exposed_parameters")
            names = [str(v.get_editor_property("name")) for v in unreal.NiagaraUserRedirectionParameterStore.get_user_parameters(store)] if hasattr(unreal.NiagaraUserRedirectionParameterStore, "get_user_parameters") else ["?"]
        except Exception as e:  # the property is not always scriptable
            names = ["not readable: %s" % e]
        res["log"].append("%s: user parameters %s" % (p, names))
    for p in ["/Game/Audio/Sounds/SFX/Lyra_JumpPad_Whoosh_01", "/Game/Audio/Sounds/SFX/Lyra_Portal_01", "/Game/Audio/Sounds/SFX/Lyra_EndOfRound-Tick-01",
              "/Game/HoverVehicles/EpicGamesContent/ExampleContent/Audio/SND_Explosion01", "/Game/HoverVehicles/EpicGamesContent/ExampleContent/Audio/SND_Explosion02",
              "/Game/Audio/Sounds/Explosions/Explosions_Grenade_Punch-Close_01"]:
        a = EAL.load_asset(p) if EAL.does_asset_exist(p) else None
        res["log"].append("%s: %s" % (p, ("%.2f s" % a.get_editor_property("duration")) if a else "missing"))
except Exception:
    res["error"] = traceback.format_exc()

with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
