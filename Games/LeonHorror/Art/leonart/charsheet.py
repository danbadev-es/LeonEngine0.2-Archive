"""
Review pictures of a character: a turnaround in a plain studio light, and stills in poses that
are hard on a rig. They are how the shapes, the weights and the textures are judged before the
engine sees them.

The textures are shown as the engine will show them: the arrays are laid out with row 0 at v = 0,
and the copies Blender loads for the preview are flipped, because Blender reads a file's first row
as the top of the image (v = 1).
"""

import math
import os

import bpy
import numpy as np
from mathutils import Euler, Vector

from . import png
from .charrig import PREFIX


# --------------------------------------------------------------------------------------
# Materials for the preview
# --------------------------------------------------------------------------------------
def _image(name, array, srgb, folder):
    """
    A Blender image from an array laid out the engine's way (row 0 is v = 0). Blender puts the
    first row of a file at the top, v = 1, so the preview copy is written upside down.
    """
    path = os.path.join(folder, name + ".png")
    png.write(path, png.to_bytes(array[::-1]))
    image = bpy.data.images.load(path)
    image.colorspace_settings.name = "sRGB" if srgb else "Non-Color"
    return image


def textured_material(name, color, normal, orm, folder):
    """
    The Blender material `name` wired like the engine's: colour (linear array, shown as sRGB),
    tangent-space normal, occlusion / roughness / metalness packed in one image.
    """
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])

    tex_color = nodes.new("ShaderNodeTexImage")
    tex_color.image = _image(name + "_Color", png.srgb_encode(color), True, folder)
    tex_orm = nodes.new("ShaderNodeTexImage")
    tex_orm.image = _image(name + "_ORM", orm, False, folder)
    tex_normal = nodes.new("ShaderNodeTexImage")
    tex_normal.image = _image(name + "_Normal", normal * 0.5 + 0.5, False, folder)

    split = nodes.new("ShaderNodeSeparateColor")
    links.new(tex_orm.outputs["Color"], split.inputs["Color"])
    # Occlusion has no socket in the shader; darkening the colour by it is close to what the
    # engine does to the ambient light.
    occlude = nodes.new("ShaderNodeMix")
    occlude.data_type = "RGBA"
    occlude.blend_type = "MULTIPLY"
    occlude.inputs["Factor"].default_value = 0.8
    links.new(tex_color.outputs["Color"], occlude.inputs["A"])
    links.new(split.outputs["Red"], occlude.inputs["B"])
    links.new(occlude.outputs["Result"], shader.inputs["Base Color"])
    links.new(split.outputs["Green"], shader.inputs["Roughness"])
    links.new(split.outputs["Blue"], shader.inputs["Metallic"])
    bump = nodes.new("ShaderNodeNormalMap")
    links.new(tex_normal.outputs["Color"], bump.inputs["Color"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    return mat


def clay_material(name):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = next(node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    shader.inputs["Base Color"].default_value = (0.30, 0.30, 0.31, 1.0)
    shader.inputs["Roughness"].default_value = 0.65
    return mat


# --------------------------------------------------------------------------------------
# Poses
# --------------------------------------------------------------------------------------
# The same vocabulary as Scripts/make_animations.py: rotations are offsets from the T pose, in
# degrees, applied X then Y then Z about the rest axes.
def _arm(side, down, forward=0.0, inward=0.0):
    return (down, -forward, inward) if side == "L" else (down, forward, -inward)


def _elbow(side, bend):
    return (0.0, 0.0, bend if side == "L" else -bend)


RELAXED = {
    "LeftArm": _arm("L", 76), "RightArm": _arm("R", 76),
    "LeftForeArm": _elbow("L", 12), "RightForeArm": _elbow("R", 12),
    "Spine": (3, 0, 0), "Head": (-2, 0, 0),
}

POSES = {
    "Rest": {},
    "Relaxed": RELAXED,
    "ArmsOverhead": {
        "LeftArm": _arm("L", -70, 10), "RightArm": _arm("R", -70, 10),
        "LeftShoulder": (-18, 0, 0), "RightShoulder": (-18, 0, 0),
        "LeftForeArm": _elbow("L", 25), "RightForeArm": _elbow("R", 25), "Spine1": (-6, 0, 0),
    },
    "DeepCrouch": dict(RELAXED, **{
        "Hips": (0, 0, 0), "Spine": (28, 0, 0), "Spine1": (12, 0, 0), "Head": (-24, 0, 0),
        "LeftUpLeg": (100, 0, 0), "RightUpLeg": (88, 0, 0), "LeftLeg": (-125, 0, 0), "RightLeg": (-118, 0, 0),
        "LeftFoot": (28, 0, 0), "RightFoot": (30, 0, 0),
        "LeftArm": _arm("L", 80, 52, 6), "RightArm": _arm("R", 80, 58, 6),
        "LeftForeArm": _elbow("L", 60), "RightForeArm": _elbow("R", 48),
    }),
    "ArmsCrossed": dict(RELAXED, **{
        "LeftArm": _arm("L", 72, 30, 26), "RightArm": _arm("R", 66, 34, 30),
        "LeftForeArm": _elbow("L", 112), "RightForeArm": _elbow("R", 116),
        "Spine": (4, 0, 0),
    }),
    "FullStride": dict(RELAXED, **{
        "LeftUpLeg": (48, 0, 0), "LeftLeg": (-22, 0, 0), "RightUpLeg": (-34, 0, 0), "RightLeg": (-58, 0, 0),
        "RightFoot": (22, 0, 0), "LeftFoot": (-12, 0, 0),
        "LeftArm": _arm("L", 74, -36), "RightArm": _arm("R", 74, 44),
        "LeftForeArm": _elbow("L", 38), "RightForeArm": _elbow("R", 70),
        "Spine": (8, 8, 0), "Spine1": (2, 6, 0), "Hips": (0, -8, 0), "Head": (-4, -6, 0),
    }),
}


def apply_pose(rig, pose):
    rig.rest()
    for pose_bone in rig.armature.pose.bones:
        short = pose_bone.name[len(PREFIX):]
        pose_bone.rotation_mode = "XYZ"
        degrees = pose.get(short, (0.0, 0.0, 0.0))
        pose_bone.rotation_euler = Euler([math.radians(a) for a in degrees], "XYZ")
    bpy.context.view_layer.update()


def apply_clip(rig, fbx_path, fraction):
    """Puts the armature at `fraction` (0..1) of an animation FBX made for the mannequin."""
    rig.rest()
    before = set(bpy.data.objects)
    actions_before = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(fbx_path), automatic_bone_orientation=False)
    for obj in set(bpy.data.objects) - before:
        bpy.data.objects.remove(obj, do_unlink=True)
    action = next(iter(set(bpy.data.actions) - actions_before))
    for pose_bone in rig.armature.pose.bones:
        pose_bone.rotation_mode = "QUATERNION"
    if rig.armature.animation_data is None:
        rig.armature.animation_data_create()
    data = rig.armature.animation_data
    data.action = action
    if hasattr(data, "action_slot") and getattr(action, "slots", None):
        data.action_slot = action.slots[0]
    start, end = action.frame_range
    bpy.context.scene.frame_set(int(round(start + (end - start) * fraction)))
    bpy.context.view_layer.update()
    return action


# --------------------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------------------
def studio(samples=48):
    """A neutral three-light studio on a grey floor. Returns the camera."""
    scene = bpy.context.scene
    for name in ("StudioKey", "StudioFill", "StudioRim", "StudioFloor", "StudioCamera"):
        old = bpy.data.objects.get(name)
        if old is not None:
            bpy.data.objects.remove(old, do_unlink=True)
    target = Vector((0.0, 0.0, 0.95))
    for name, location, energy, size in (("StudioKey", (-2.2, -3.2, 3.0), 300.0, 2.5), ("StudioFill", (3.0, -2.0, 1.4), 90.0, 4.0),
                                         ("StudioRim", (0.8, 3.4, 2.6), 240.0, 2.5)):
        data = bpy.data.lights.new(name, "AREA")
        data.energy, data.size = energy, size
        light = bpy.data.objects.new(name, data)
        scene.collection.objects.link(light)
        light.location = location
        light.rotation_euler = (target - light.location).to_track_quat("-Z", "Y").to_euler()

    floor_data = bpy.data.meshes.new("StudioFloor")
    floor_data.from_pydata([(-30, -30, 0), (30, -30, 0), (30, 30, 0), (-30, 30, 0)], [], [(0, 1, 2, 3)])
    floor = bpy.data.objects.new("StudioFloor", floor_data)
    scene.collection.objects.link(floor)
    floor_mat = bpy.data.materials.new("StudioFloorMaterial")
    floor_mat.use_nodes = True
    shader = next(node for node in floor_mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    shader.inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1.0)
    shader.inputs["Roughness"].default_value = 0.9
    floor_data.materials.append(floor_mat)

    world = bpy.data.worlds.get("StudioWorld") or bpy.data.worlds.new("StudioWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.09, 0.09, 0.10, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    scene.world = world

    camera_data = bpy.data.cameras.new("StudioCamera")
    camera_data.lens = 85
    camera = bpy.data.objects.new("StudioCamera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera

    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    try:
        preferences = bpy.context.preferences.addons["cycles"].preferences
        preferences.compute_device_type = "OPTIX"
        preferences.get_devices()
        for device in preferences.devices:
            device.use = device.type != "CPU"
        scene.cycles.device = "GPU"
    except Exception:
        scene.cycles.device = "CPU"
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "Standard"
    return camera


def _shot(camera, angle, path, size, target=(0.0, 0.0, 0.92), distance=5.6, height=1.25, lens=85):
    scene = bpy.context.scene
    a = math.radians(angle)
    camera.data.lens = lens
    camera.location = Vector((distance * math.sin(a), -distance * math.cos(a), height))
    camera.rotation_euler = (Vector(target) - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.resolution_x, scene.render.resolution_y = size
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(path)
    pixels = np.empty(size[0] * size[1] * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    os.remove(path)
    return png.to_bytes(pixels.reshape(size[1], size[0], 4)[::-1, :, :3])


def sheet(path, shots, size=(400, 760), **camera_options):
    """
    shots: a list of (prepare, angle, options): `prepare()` poses the character (None keeps the
    pose), `angle` is where the camera stands (0 in front, 90 on the character's left, 180
    behind), `options` override the framing (target, distance, height, lens). One row of pictures.
    """
    camera = bpy.data.objects["StudioCamera"]
    scratch = os.path.splitext(path)[0] + "_shot.png"
    tiles = []
    for prepare, angle, options in shots:
        if prepare is not None:
            prepare()
        tiles.append(_shot(camera, angle, scratch, size, **dict(camera_options, **options)))
    png.write(path, np.concatenate(tiles, axis=1))
    print("SHEET", path)
    return path
