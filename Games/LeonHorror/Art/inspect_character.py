"""
Reads cooked character assets and compares them with the mannequin's, with the system Python:

    python Games/LeonHorror/Art/inspect_character.py Games/LeonHorror/Content SK_Protagonist [SK_Patient ...]

For each character it prints the triangle count, the bounds, the material slots, whether the bone
names are the same set as YBot's, and how far the rest pose is from YBot's (the clips store
bone-local transforms, so a different rest pose would play them wrong).

    python Games/LeonHorror/Art/inspect_character.py --convention <check.json> <source.png> <exported.png> <content>

checks which way up a texture and the UVs arrive: see `convention` below.
"""

import math
import os
import struct
import sys


class Reader:
    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = handle.read()
        self.at = 0

    def take(self, fmt):
        values = struct.unpack_from("<" + fmt, self.data, self.at)
        self.at += struct.calcsize("<" + fmt)
        return values

    def string(self):
        (length,) = self.take("I")
        text = self.data[self.at:self.at + length].decode("utf-8", "replace")
        self.at += length
        return text


def read_skeleton(path):
    r = Reader(path)
    r.take("II")
    r.take("QQ")
    r.string()
    (count,) = r.take("I")
    bones = []
    for _ in range(count):
        name = r.string()
        (parent,) = r.take("i")
        translation = r.take("3f")
        rotation = r.take("4f")  # glm::quat in memory: x, y, z, w
        scale = r.take("3f")
        inverse_bind = r.take("16f")
        bones.append({"name": name, "parent": parent, "t": translation, "q": rotation, "s": scale, "ibp": inverse_bind})
    return bones


def read_mesh(path):
    r = Reader(path)
    r.take("II")
    r.take("QQQQ")
    r.string()
    skeleton = r.string()
    vertex_count, index_count, submesh_count, slot_count = r.take("IIII")
    low = r.take("3f")
    high = r.take("3f")
    r.take("4f")
    submeshes = []
    for _ in range(submesh_count):
        name = r.string()
        _offset, count, _voffset, _vcount, slot = r.take("IIIII")
        r.take("16f")
        r.take("6f")
        submeshes.append((name, count // 3, slot))
    slots = [(r.string(), r.string()) for _ in range(slot_count)]
    stride = 100
    start = r.at
    weights_ok, unweighted, uv_low, uv_high = True, 0, [9.0, 9.0], [-9.0, -9.0]
    used = set()
    for i in range(vertex_count):
        base = start + i * stride
        u, v = struct.unpack_from("<2f", r.data, base + 24)
        uv_low = [min(uv_low[0], u), min(uv_low[1], v)]
        uv_high = [max(uv_high[0], u), max(uv_high[1], v)]
        indices = struct.unpack_from("<4i", r.data, base + 68)
        weights = struct.unpack_from("<4f", r.data, base + 84)
        if abs(sum(weights) - 1.0) > 1.0e-3:
            weights_ok = False
        if weights[0] >= 0.9999 and indices[0] == 0 and sum(weights[1:]) == 0.0:
            unweighted += 1  # all on bone 0: either really the hips or a vertex that had no weights
        for index, weight in zip(indices, weights):
            if weight > 0.0:
                used.add(index)
    return {"skeleton": skeleton, "triangles": index_count // 3, "vertices": vertex_count, "low": low, "high": high,
            "submeshes": submeshes, "slots": slots, "weights_ok": weights_ok, "only_bone0": unweighted,
            "uv": (uv_low, uv_high), "bones_used": len(used)}


def compare(reference, other):
    by_name = {bone["name"]: bone for bone in reference}
    worst_t, worst_q, worst_s = 0.0, 0.0, 0.0
    for bone in other:
        ref = by_name.get(bone["name"])
        if ref is None:
            continue
        worst_t = max(worst_t, math.dist(bone["t"], ref["t"]))
        dot = abs(sum(a * b for a, b in zip(bone["q"], ref["q"])))
        worst_q = max(worst_q, math.degrees(2.0 * math.acos(min(1.0, dot))))
        worst_s = max(worst_s, max(abs(a - b) for a, b in zip(bone["s"], ref["s"])))
    parents_same = all(
        (bone["parent"] < 0 and by_name[bone["name"]]["parent"] < 0) or
        (bone["parent"] >= 0 and by_name[bone["name"]]["parent"] >= 0 and
         other[bone["parent"]]["name"] == reference[by_name[bone["name"]]["parent"]]["name"])
        for bone in other if bone["name"] in by_name)
    return worst_t, worst_q, worst_s, parents_same


def read_png(path):
    """Rows of an 8-bit RGB or RGBA PNG as bytes objects, with the number of channels."""
    import zlib
    with open(path, "rb") as handle:
        data = handle.read()
    at, chunks, header = 8, [], None
    while at < len(data):
        (length,) = struct.unpack_from(">I", data, at)
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            chunks.append(body)
        at += 12 + length
    width, height, depth, color_type = header[:4]
    channels = {2: 3, 6: 4, 0: 1}[color_type]
    if depth != 8:
        raise ValueError("only 8-bit PNG files")
    raw = zlib.decompress(b"".join(chunks))
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        previous = line
    return rows, width, channels


def convention(check_path, source_png, exported_png, content):
    """
    Follows one texel from the painter to the cooked assets: `check_path` is the note the build
    left (the top vertex of the character, its UV and the colour painted there), `source_png` the
    image given to AssetTool and `exported_png` the cooked .ltex exported back by AssetTool.
    """
    import json
    with open(check_path, "r", encoding="utf-8") as handle:
        note = json.load(handle)
    source, width, channels = read_png(source_png)
    exported, _w, exported_channels = read_png(exported_png)
    if exported_channels != channels:
        exported = [bytes(b for i, b in enumerate(row) if i % exported_channels < channels) for row in exported]
    # The cooked texture is block compressed, so the comparison is by how far the texels are apart.
    def apart(rows_a, rows_b):
        total = count = 0
        for y in range(0, len(rows_a), 16):
            a, b = rows_a[y], rows_b[y]
            total += sum(abs(a[i] - b[i]) for i in range(0, len(a), 7))
            count += len(range(0, len(a), 7))
        return total / max(count, 1)

    print("texture after import, mean difference from the source in 8-bit steps: rows in the same order %.2f, upside down %.2f" % (
        apart(exported, source), apart(exported, source[::-1])))

    mesh_path = os.path.join(content, "SkeletalMeshes", note["stem"] + ".lskeletalmesh")
    r = Reader(mesh_path)
    r.take("II")
    r.take("QQQQ")
    r.string()
    r.string()
    vertex_count, _index_count, submesh_count, slot_count = r.take("IIII")
    r.take("10f")
    for _ in range(submesh_count):
        r.string()
        r.take("IIIII")
        r.take("22f")
    for _ in range(slot_count):
        r.string()
        r.string()
    best = None
    for i in range(vertex_count):
        x, y, z = struct.unpack_from("<3f", r.data, r.at + i * 100)
        if best is None or y > best[0]:
            best = (y, struct.unpack_from("<2f", r.data, r.at + i * 100 + 24))
    u, v = best[1]
    print("top vertex: built with uv (%.4f, %.4f), cooked with uv (%.4f, %.4f): v %s" % (
        note["uv"][0], note["uv"][1], u, v, "kept" if abs(v - note["uv"][1]) < 1.0e-3 else "FLIPPED" if abs(1.0 - v - note["uv"][1]) < 1.0e-3 else "DIFFERENT"))
    row = min(int(v * len(exported)), len(exported) - 1)
    col = min(int(u * width), width - 1)
    texel = tuple(exported[row][col * channels:col * channels + 3])
    print("cooked texture at row v * height = %d, column %d: %s; the painter put %s there" % (row, col, texel, tuple(note["rgb"])))


def main():
    if sys.argv[1] == "--convention":
        convention(*sys.argv[2:6])
        return
    content = sys.argv[1]
    reference = read_skeleton(os.path.join(content, "Skeletons", "YBot.lskeleton"))
    reference_names = [bone["name"] for bone in reference]
    for stem in sys.argv[2:]:
        bones = read_skeleton(os.path.join(content, "Skeletons", stem + ".lskeleton"))
        names = [bone["name"] for bone in bones]
        mesh = read_mesh(os.path.join(content, "SkeletalMeshes", stem + ".lskeletalmesh"))
        worst_t, worst_q, worst_s, parents_same = compare(reference, bones)
        print(stem)
        print("  bones %d (YBot %d)  same name set: %s  same order: %s  same parents: %s" % (
            len(bones), len(reference), set(names) == set(reference_names), names == reference_names, parents_same))
        extra = sorted(set(names) - set(reference_names))
        missing = sorted(set(reference_names) - set(names))
        if extra or missing:
            print("  extra: %s  missing: %s" % (extra, missing))
        print("  rest pose vs YBot: translation %.5f m, rotation %.4f deg, scale %.5f (largest difference)" % (worst_t, worst_q, worst_s))
        print("  triangles %d  vertices %d  bones used %d  weights sum to 1: %s  vertices entirely on bone 0: %d" % (
            mesh["triangles"], mesh["vertices"], mesh["bones_used"], mesh["weights_ok"], mesh["only_bone0"]))
        print("  bounds min (%.3f %.3f %.3f) max (%.3f %.3f %.3f)  height %.3f m  width %.3f m" % (
            mesh["low"] + mesh["high"] + (mesh["high"][1] - mesh["low"][1], mesh["high"][0] - mesh["low"][0])))
        print("  uv range u %.3f..%.3f v %.3f..%.3f" % (mesh["uv"][0][0], mesh["uv"][1][0], mesh["uv"][0][1], mesh["uv"][1][1]))
        for name, triangles, slot in mesh["submeshes"]:
            print("  submesh %-24s %6d triangles  slot %d" % (name, triangles, slot))
        for name, path in mesh["slots"]:
            print("  slot %-24s %s" % (name, path))
        print("  skeleton path %s" % mesh["skeleton"])


main()
