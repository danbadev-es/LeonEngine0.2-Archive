"""
From a built character to files: the textures, the material, the review sheets and the FBX.
"""

import os

import bpy
import numpy as np

from . import charbody, charsheet, png, texture
from .characters import HEIGHT


def bake_occlusion(obj, sizes, samples=24):
    """
    Ambient occlusion of the character in its bind pose, baked by Cycles into each sheet's UVs.
    Returns {sheet: array (size, size)}, row 0 at v = 0 like every other map here.
    """
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    try:
        preferences = bpy.context.preferences.addons["cycles"].preferences
        preferences.compute_device_type = "OPTIX"
        preferences.get_devices()
        for device in preferences.devices:
            device.use = device.type != "CPU"
        scene.cycles.device = "GPU"
    except Exception:
        scene.cycles.device = "CPU"

    hidden = [o for o in scene.objects if o is not obj and not o.hide_render]
    for other in hidden:
        other.hide_render = True
    images = {}
    for slot, sheet in enumerate(sorted(sizes)):
        mat = obj.data.materials[slot]
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        image = bpy.data.images.new("Bake_%d" % sheet, sizes[sheet], sizes[sheet], alpha=False, float_buffer=True)
        image.colorspace_settings.name = "Non-Color"
        node = nodes.new("ShaderNodeTexImage")
        node.image = image
        nodes.active = node
        images[sheet] = (image, node, mat)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    scene.render.bake.margin = 8
    bpy.ops.object.bake(type="AO", margin=8, use_clear=True)
    out = {}
    for sheet, (image, node, mat) in images.items():
        size = sizes[sheet]
        pixels = np.empty(size * size * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        out[sheet] = pixels.reshape(size, size, 4)[:, :, 0].copy()
        mat.node_tree.nodes.remove(node)
        bpy.data.images.remove(image)
    for other in hidden:
        other.hide_render = False
    return out


def save_textures(name, canvas, folder, occlusion=None):
    """T_<name>_Color / _Normal / _ORM, as AssetTool expects them. Returns the arrays for the preview."""
    shade = canvas.shade
    if occlusion is not None:
        # Baked occlusion goes fully into the occlusion channel and lightly into the colour, where
        # it stands in for the contact shadows a character's small folds never get from the lights.
        shade = shade * occlusion
    color = np.clip(canvas.color * (0.55 + 0.45 * shade[..., None]), 0.0, 1.0)
    normal = canvas.normal_map()
    orm = np.stack([np.clip(shade, 0.0, 1.0), np.clip(canvas.roughness, 0.04, 1.0), np.clip(canvas.metallic, 0.0, 1.0)], axis=-1)
    os.makedirs(folder, exist_ok=True)
    png.write(os.path.join(folder, "T_%s_Color.png" % name), png.to_bytes(png.srgb_encode(color)))
    png.write(os.path.join(folder, "T_%s_Normal.png" % name), png.to_bytes(normal * 0.5 + 0.5))
    png.write(os.path.join(folder, "T_%s_ORM.png" % name), png.to_bytes(orm))
    return color, normal, orm


def write_check(obj, stem, material, color, folder):
    """
    A note for inspect_character.py --convention: the highest vertex, its UV and the colour painted
    at that UV, so the way up of textures and UVs can be checked on the cooked assets.
    """
    import json
    mesh = obj.data
    top = max(range(len(mesh.vertices)), key=lambda i: mesh.vertices[i].co.z)
    loop = next(l for l in mesh.loops if l.vertex_index == top)
    u, v = mesh.uv_layers[0].data[loop.index].uv
    size = color.shape[0]
    pixel = png.to_bytes(png.srgb_encode(color[min(int(v * size), size - 1), min(int(u * size), size - 1)]))
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, stem + "_check.json"), "w", encoding="utf-8") as handle:
        json.dump({"stem": stem, "material": material, "uv": [float(u), float(v)], "rgb": [int(c) for c in pixel]}, handle)


def write_material(name, folder):
    """The .lmat for the set, in the same form as the level's materials."""
    stub = texture.TextureSet(name, size=4)
    return stub.write_material(folder)


def review_sheets(rig, character, args, samples):
    name = character.name
    folder = os.path.join(args["out"], "Sheets")
    charsheet.studio(samples)

    def pose(key):
        return lambda: charsheet.apply_pose(rig, charsheet.POSES[key])

    def clip(stem, fraction):
        return lambda: charsheet.apply_clip(rig, os.path.join(args["anims"], stem + ".fbx"), fraction)

    wanted = [w for w in args.get("sheet", "").split(",") if w]

    def make(suffix, shots, **options):
        if not wanted or suffix in wanted:
            charsheet.sheet(os.path.join(folder, "Character_%s_%s.png" % (name, suffix)), shots, **options)

    make("Turnaround", [(pose("Relaxed"), 0, {}), (None, 40, {}), (None, 90, {}), (None, 180, {})])
    make("Rest", [(pose("Rest"), 0, {}), (None, 180, {})], size=(800, 760), distance=6.6)
    make("Poses", [(pose("ArmsOverhead"), 30, {"distance": 6.6, "target": (0, 0, 1.12)}),
                                   (pose("DeepCrouch"), 55, {}), (pose("ArmsCrossed"), 20, {}), (pose("FullStride"), 100, {})])
    if name == "Protagonist":
        clips = [(clip("WalkForward", 0.25), 150, {}), (clip("PistolAimIdle", 0.0), 215, {}), (clip("MeleeSwing", 0.28), 40, {"distance": 6.2, "target": (0, 0, 1.05)}),
                 (clip("Interact", 0.45), 120, {})]
        closeups = [(pose("Relaxed"), 200, {"distance": 2.9, "target": (0.05, 0, 1.38), "height": 1.62}),
                    (None, 25, {"distance": 2.6, "target": (0, 0, 1.52), "height": 1.6}),
                    (None, 150, {"distance": 3.0, "target": (0, 0, 0.32), "height": 0.75}),
                    (clip("PistolAimIdle", 0.0), 300, {"distance": 2.6, "target": (-0.25, -0.35, 1.38), "height": 1.5})]
    else:
        clips = [(clip("EnemyShamble", 0.25), 35, {}), (clip("EnemyAttack", 0.5), 60, {}), (clip("EnemyIdle", 0.5), 320, {}),
                 (clip("HitReact", 0.2), 100, {})]
        closeups = [(pose("Relaxed"), 20, {"distance": 2.6, "target": (0, 0, 1.5), "height": 1.58}),
                    (None, 160, {"distance": 3.0, "target": (0, 0, 1.25), "height": 1.5}),
                    (None, 30, {"distance": 3.0, "target": (0, 0, 0.3), "height": 0.7}),
                    (clip("EnemyAttack", 0.5), 330, {"distance": 3.0, "target": (0, -0.45, 1.25), "height": 1.4})]
    make("Clips", clips)
    make("Closeups", closeups)
    rig.rest()


def produce(rig, character, args):
    b, name = character.builder, character.name
    stem = "SK_" + name
    clay = args["clay"]
    densities = b.pack(character.sizes)
    obj = b.to_object(stem, character.materials, character.sizes)
    weights = b.weights()
    rig.bind(obj, weights)

    pos = b.positions()
    height = pos[:, 2].max() - pos[:, 2].min()
    width = pos[:, 1].max() - pos[:, 1].min()
    print("CHARACTER %s: %d triangles, %d vertices, height %.3f m (floor at %.3f), depth %.3f m" % (
        name, b.triangle_count(), len(pos), height, pos[:, 2].min(), width))
    print("CHARACTER %s: scale for %.2f m in game = %.4f" % (name, HEIGHT[name], HEIGHT[name] / height))
    for sheet, density in densities.items():
        print("CHARACTER %s: sheet %d  %d px  %.0f texels per metre" % (name, sheet, character.sizes[sheet], density))

    raw = os.path.join(args["out"], "Raw", "Characters")
    if clay:
        for material in character.materials:
            charsheet.clay_material(material)
    else:
        canvases = charbody.paint(b, character.sizes, densities)
        occlusion = {} if args["quick"] else bake_occlusion(obj, character.sizes)
        for slot, sheet in enumerate(sorted(character.sizes)):
            material = character.materials[slot]
            color, normal, orm = save_textures(material[2:], canvases[sheet], raw, occlusion.get(sheet))
            write_material(material[2:], os.path.join(args["content"], "Materials"))
            charsheet.textured_material(material, color, normal, orm, os.path.join(args["out"], "Preview"))
            if slot == 0:
                write_check(obj, stem, material, color, os.path.join(args["out"], "Preview"))
            # The maps side by side at half size, to look the painting over.
            half = slice(None, None, max(1, character.sizes[sheet] // 1024))
            strip = np.concatenate([png.srgb_encode(color[half, half]), normal[half, half] * 0.5 + 0.5, orm[half, half]], axis=1)
            png.write(os.path.join(args["out"], "Sheets", "Character_%s_Maps%d.png" % (name, sheet)), png.to_bytes(strip))

    if not args["no-export"] and not clay:
        rig.export(obj, os.path.join(raw, stem + ".fbx"))
        print("EXPORTED", os.path.join(raw, stem + ".fbx"))
    if args["sheets"] or clay:
        review_sheets(rig, character, args, 24 if (args["quick"] or clay) else 64)
