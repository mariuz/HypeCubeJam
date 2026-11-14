# Plain Python 3 + Pillow (no engine): python docs/tools/prep_station4.py
# Prepares "Space Station 4" by Gerardo Justel (CC-BY, SourceAssets/SpaceStation4/space_station_4.glb, 155 MB: 20 meshes,
# 110 179 triangles, 3 materials, nine 4096 x 4096 textures) for docs/tools/import_station4.py:
#   SourceAssets/SpaceStation4/prepared/T_Station4_<1|2>_<BaseColor|ORM|Emissive|Normal>.png   the eight textures the
#       game uses, reduced to SIZE x SIZE (the clear-coat normal map of material 1 is left out)
#   SourceAssets/SpaceStation4/prepared/space_station_4_mesh.glb   the 20 meshes merged into one (node transforms baked
#       in, one primitive per material), without images, textures and texture references (a few MB instead of 155), so
#       that the editor import is quick and creates one static mesh with three material slots.
# "--mesh-only" skips the textures.
import io
import json
import os
import struct
import sys

from PIL import Image

Image.MAX_IMAGE_PIXELS = None
SIZE = 2048
LIGHTS_REACH = 0.6
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "SourceAssets", "SpaceStation4", "space_station_4.glb"))
OUT = os.path.join(os.path.dirname(SRC), "prepared")
# glTF texture index -> name (see the materials of the file: station_1 uses 0..4, station_2 uses 5..8)
NAMES = {0: "1_BaseColor", 1: "1_ORM", 2: "1_Emissive", 3: "1_Normal", 5: "2_BaseColor", 6: "2_ORM", 7: "2_Emissive", 8: "2_Normal"}


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(SRC, "rb") as f:
        magic, version, _ = struct.unpack("<III", f.read(12))
        assert magic == 0x46546C67 and version == 2, "not a glTF 2 binary"
        length, kind = struct.unpack("<II", f.read(8))
        assert kind == 0x4E4F534A
        gltf = json.loads(f.read(length))
        length, kind = struct.unpack("<II", f.read(8))
        assert kind == 0x004E4942
        blob = f.read(length)

    views = gltf["bufferViews"]
    for texture_index, name in (NAMES.items() if "--mesh-only" not in sys.argv else []):
        image = gltf["images"][gltf["textures"][texture_index]["source"]]
        view = views[image["bufferView"]]
        data = blob[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
        picture = Image.open(io.BytesIO(data)).convert("RGB")
        if picture.size != (SIZE, SIZE):
            picture = picture.resize((SIZE, SIZE), Image.LANCZOS)
        path = os.path.join(OUT, "T_Station4_%s.png" % name)
        picture.save(path, optimize=True)
        print("%s: %d x %d, %.1f MB" % (os.path.basename(path), SIZE, SIZE, os.path.getsize(path) / 1e6))

    # The mesh-only file: every primitive with its node transform baked in, merged into ONE mesh with one primitive per
    # material and a single node without a transform. (Left to the importer, "combine meshes" produced a third of the
    # model.) Positions, normals and the first UV set are kept; tangents are recomputed by the editor.
    accessors = gltf["accessors"]

    def read(index, count_per_element, fmt):
        accessor = accessors[index]
        view = views[accessor["bufferView"]]
        size = struct.calcsize("<" + fmt * count_per_element)
        stride = view.get("byteStride") or size
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        if stride == size:
            return list(struct.unpack_from("<%d%s" % (accessor["count"] * count_per_element, fmt), blob, start))
        values = []
        for i in range(accessor["count"]):
            values.extend(struct.unpack_from("<" + fmt * count_per_element, blob, start + i * stride))
        return values

    def node_matrix(node):
        if "matrix" in node:
            m = node["matrix"]  # column major
            return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        t = node.get("translation", [0.0, 0.0, 0.0])
        x, y, z, w = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
        sx, sy, sz = node.get("scale", [1.0, 1.0, 1.0])
        r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
             [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
             [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        return [[r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, t[0]], [r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, t[1]],
                [r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, t[2]], [0.0, 0.0, 0.0, 1.0]]

    def mul(a, b):
        return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]

    merged = {}  # material index -> positions, normals, uvs, indices

    def visit(index, parent):
        node = gltf["nodes"][index]
        world = mul(parent, node_matrix(node))
        if "mesh" in node:
            for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
                out = merged.setdefault(primitive.get("material", 0), ([], [], [], []))
                base = len(out[0]) // 3
                positions = read(primitive["attributes"]["POSITION"], 3, "f")
                normals = read(primitive["attributes"]["NORMAL"], 3, "f")
                uvs = read(primitive["attributes"]["TEXCOORD_0"], 2, "f")
                for i in range(0, len(positions), 3):
                    x, y, z = positions[i:i + 3]
                    out[0].extend((world[0][0] * x + world[0][1] * y + world[0][2] * z + world[0][3],
                                   world[1][0] * x + world[1][1] * y + world[1][2] * z + world[1][3],
                                   world[2][0] * x + world[2][1] * y + world[2][2] * z + world[2][3]))
                    x, y, z = normals[i:i + 3]
                    nx = world[0][0] * x + world[0][1] * y + world[0][2] * z
                    ny = world[1][0] * x + world[1][1] * y + world[1][2] * z
                    nz = world[2][0] * x + world[2][1] * y + world[2][2] * z
                    length = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
                    out[1].extend((nx / length, ny / length, nz / length))
                out[2].extend(uvs)
                out[3].extend(i + base for i in read(primitive["indices"], 1, "I"))
        for child in node.get("children", []):
            visit(child, world)

    identity = [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]
    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        visit(root, identity)

    # The "Emission" material is mostly a cloud of loose lights reaching 20 m from a hull that is 10 m long: in the game
    # they would be points of light adrift in the fight (and they wreck any fit by bounds). Only the lights within
    # LIGHTS_REACH of the hull's own bounding box are kept.
    hull = [v for m in merged if gltf["materials"][m]["name"] != "Emission" for v in merged[m][0]]
    hull_lo = [min(hull[i::3]) - LIGHTS_REACH for i in range(3)]
    hull_hi = [max(hull[i::3]) + LIGHTS_REACH for i in range(3)]
    for material in merged:
        if gltf["materials"][material]["name"] != "Emission":
            continue
        positions, normals, uvs, indices = merged[material]
        kept = []
        for t in range(0, len(indices), 3):
            triangle = indices[t:t + 3]
            if all(hull_lo[k] <= positions[3 * v + k] <= hull_hi[k] for v in triangle for k in range(3)):
                kept.extend(triangle)
        print("lights: %d of %d triangles kept (within %.1f m of the hull)" % (len(kept) // 3, len(indices) // 3, LIGHTS_REACH))
        # Only the vertices those triangles use are kept.
        remap = {}
        out = ([], [], [], [])
        for v in kept:
            if v not in remap:
                remap[v] = len(remap)
                out[0].extend(positions[3 * v:3 * v + 3])
                out[1].extend(normals[3 * v:3 * v + 3])
                out[2].extend(uvs[2 * v:2 * v + 2])
            out[3].append(remap[v])
        merged[material] = out

    new_blob = bytearray()
    new_views = []
    new_accessors = []
    primitives = []

    def add(values, fmt, kind, target, with_bounds=False):
        while len(new_blob) % 4:
            new_blob.append(0)
        data = struct.pack("<%d%s" % (len(values), fmt), *values)
        new_views.append({"buffer": 0, "byteOffset": len(new_blob), "byteLength": len(data), "target": target})
        new_blob.extend(data)
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[kind]
        accessor = {"bufferView": len(new_views) - 1, "componentType": 5126 if fmt == "f" else 5125, "count": len(values) // width, "type": kind}
        if with_bounds:
            accessor["min"] = [min(values[i::3]) for i in range(3)]
            accessor["max"] = [max(values[i::3]) for i in range(3)]
        new_accessors.append(accessor)
        return len(new_accessors) - 1

    lo = [1e30] * 3
    hi = [-1e30] * 3
    triangles = 0
    for material in sorted(merged):
        positions, normals, uvs, indices = merged[material]
        primitives.append({"attributes": {"POSITION": add(positions, "f", "VEC3", 34962, True), "NORMAL": add(normals, "f", "VEC3", 34962),
                                          "TEXCOORD_0": add(uvs, "f", "VEC2", 34962)},
                           "indices": add(indices, "I", "SCALAR", 34963), "material": material, "mode": 4})
        triangles += len(indices) // 3
        used = sorted(set(indices))
        for i in range(3):
            lo[i] = min(lo[i], min(positions[3 * v + i] for v in used))
            hi[i] = max(hi[i], max(positions[3 * v + i] for v in used))
        print("material %d (%s): %d vertices, %d triangles, bounds %s .. %s" % (material, gltf["materials"][material]["name"], len(positions) // 3, len(indices) // 3,
                                                                              [round(min(positions[i::3]), 2) for i in range(3)], [round(max(positions[i::3]), 2) for i in range(3)]))
    while len(new_blob) % 4:
        new_blob.append(0)

    out_gltf = {
        "asset": {"version": "2.0", "generator": "HypeCubeJam docs/tools/prep_station4.py"},
        "scene": 0, "scenes": [{"name": "Station4", "nodes": [0]}], "nodes": [{"name": "SM_Station4", "mesh": 0}],
        "meshes": [{"name": "SM_Station4", "primitives": primitives}],
        "materials": [{"name": m["name"], "doubleSided": True, "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.5, 1.0]}} for m in gltf["materials"]],
        "accessors": new_accessors, "bufferViews": new_views, "buffers": [{"byteLength": len(new_blob)}],
    }
    text = json.dumps(out_gltf, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    path = os.path.join(OUT, "space_station_4_mesh.glb")
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(new_blob)))
        f.write(struct.pack("<II", len(text), 0x4E4F534A))
        f.write(text)
        f.write(struct.pack("<II", len(new_blob), 0x004E4942))
        f.write(new_blob)
    print("%s: %.1f MB, 1 mesh, %d triangles, bounds (m, glTF axes: Y up) %s .. %s" % (
        os.path.basename(path), os.path.getsize(path) / 1e6, triangles, [round(v, 2) for v in lo], [round(v, 2) for v in hi]))


if __name__ == "__main__":
    sys.exit(main())
