"""
Review renders: a quick lit picture of a batch of meshes, to catch a bad shape or a missing
material before anything is imported. It is a check, not a beauty shot: the engine is what the
assets are judged in.
"""

import math
import os

import bpy
from mathutils import Vector


def preview_material(name, texture_folder):
    """
    Gives the Blender material `M_<name>` the generated textures, when they exist.

    The files are written with row 0 at v = 0 (the engine's convention), which Blender shows
    upside down. For tiling surfaces that only mirrors the pattern, so the preview does not bother
    to flip them back; its normal map is left out for the same reason.
    """
    mat = bpy.data.materials.get("M_" + name)
    if mat is None:
        return
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    shader = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
    if shader is None:
        return
    color_path = os.path.join(texture_folder, "T_%s_Color.png" % name)
    orm_path = os.path.join(texture_folder, "T_%s_ORM.png" % name)
    if os.path.exists(color_path):
        image = nodes.new("ShaderNodeTexImage")
        image.image = bpy.data.images.load(color_path, check_existing=True)
        links.new(image.outputs["Color"], shader.inputs["Base Color"])
    if os.path.exists(orm_path):
        image = nodes.new("ShaderNodeTexImage")
        image.image = bpy.data.images.load(orm_path, check_existing=True)
        image.image.colorspace_settings.name = "Non-Color"
        split = nodes.new("ShaderNodeSeparateColor")
        links.new(image.outputs["Color"], split.inputs["Color"])
        links.new(split.outputs["Green"], shader.inputs["Roughness"])
        links.new(split.outputs["Blue"], shader.inputs["Metallic"])


def flat_material(name, values):
    """A material without textures, for the preview: colour, roughness, metalness, emission."""
    mat = bpy.data.materials.get("M_" + name)
    if mat is None:
        return
    mat.use_nodes = True
    shader = next((node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    if shader is None:
        return
    color = values.get("color", (0.8, 0.8, 0.8))
    shader.inputs["Base Color"].default_value = (color[0], color[1], color[2], 1.0)
    shader.inputs["Roughness"].default_value = values.get("roughness", 0.5)
    shader.inputs["Metallic"].default_value = values.get("metallic", 0.0)
    emissive = values.get("emissive")
    if emissive:
        shader.inputs["Emission Color"].default_value = (emissive[0], emissive[1], emissive[2], 1.0)
        shader.inputs["Emission Strength"].default_value = values.get("emissive_intensity", 1.0)


def render(objects, path, resolution=(1600, 900), samples=48):
    """
    Lays the objects out in a row on the ground and renders them from three quarters. Objects are
    moved for the picture and put back afterwards.
    """
    scene = bpy.context.scene
    original = [(obj, obj.location.copy()) for obj in objects]
    # Only this batch is in the picture.
    hidden = [obj for obj in scene.objects if obj not in objects and not obj.hide_render]
    for obj in hidden:
        obj.hide_render = True

    cursor = 0.0
    highest = 0.0
    for obj in objects:
        corners = [vertex.co for vertex in obj.data.vertices]
        low = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
        high = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
        obj.location = Vector((cursor - low.x, -(low.y + high.y) * 0.5, -low.z))
        cursor += (high.x - low.x) + 0.4
        highest = max(highest, high.z - low.z)
    width = max(cursor - 0.4, 0.5)

    ground_data = bpy.data.meshes.new("SheetGround")
    ground_data.from_pydata([(-50, -50, 0), (50, -50, 0), (50, 50, 0), (-50, 50, 0)], [], [(0, 1, 2, 3)])
    ground = bpy.data.objects.new("SheetGround", ground_data)
    scene.collection.objects.link(ground)
    ground_mat = bpy.data.materials.new("SheetGroundMaterial")
    ground_mat.diffuse_color = (0.18, 0.18, 0.19, 1.0)
    ground_data.materials.append(ground_mat)

    centre = Vector((width * 0.5, 0.0, highest * 0.45))
    distance = max(width, highest * 1.6) * 1.15 + 1.0
    camera_data = bpy.data.cameras.new("SheetCamera")
    camera_data.lens = 50
    camera = bpy.data.objects.new("SheetCamera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = centre + Vector((distance * 0.35, -distance, distance * 0.42))
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera

    lights = []
    for location, energy, size in (((-0.5, -0.8, 1.0), 4.0, 6.0), ((0.9, -0.3, 0.5), 1.6, 8.0), ((0.0, 0.9, 0.7), 1.2, 8.0)):
        data = bpy.data.lights.new("SheetLight", "AREA")
        data.energy = energy * distance * distance * 1.6
        data.size = size
        light = bpy.data.objects.new("SheetLight", data)
        scene.collection.objects.link(light)
        light.location = centre + Vector(location) * distance
        light.rotation_euler = (centre - light.location).to_track_quat("-Z", "Y").to_euler()
        lights.append(light)

    world = bpy.data.worlds.new("SheetWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.05, 0.05, 0.055, 1.0)
    scene.world = world

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
    scene.render.resolution_x, scene.render.resolution_y = resolution
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "AgX"
    scene.render.filepath = path
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.render.render(write_still=True)

    for obj, location in original:
        obj.location = location
    for obj in hidden:
        obj.hide_render = False
    for extra in [ground, camera] + lights:
        bpy.data.objects.remove(extra, do_unlink=True)
    return path


def setup_cycles(scene, samples):
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
    scene.view_settings.view_transform = "AgX"


def render_plan(objects, path, pixels_per_metre=48, samples=32):
    """
    The building from straight above, ceilings left out: the plan as it was actually modelled,
    with the wall finishes and the doorways where they ended up.
    """
    scene = bpy.context.scene
    shown = [obj for obj in objects if not obj.name.endswith("_Ceiling")]
    hidden = [obj for obj in scene.objects if obj not in shown and not obj.hide_render]
    for obj in hidden:
        obj.hide_render = True

    points = [vertex.co for obj in shown for vertex in obj.data.vertices]
    low = Vector((min(p.x for p in points), min(p.y for p in points), 0.0))
    high = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    centre = (low + high) * 0.5
    width, depth = high.x - low.x + 1.0, high.y - low.y + 1.0

    camera_data = bpy.data.cameras.new("PlanCamera")
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = max(width, depth)
    camera = bpy.data.objects.new("PlanCamera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = Vector((centre.x, centre.y, high.z + 20.0))
    scene.camera = camera

    sun_data = bpy.data.lights.new("PlanSun", "SUN")
    sun_data.energy = 3.0
    sun_data.angle = math.radians(25.0)
    sun = bpy.data.objects.new("PlanSun", sun_data)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(28.0), math.radians(18.0), 0.0)

    world = bpy.data.worlds.new("PlanWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.35, 0.35, 0.37, 1.0)
    scene.world = world

    setup_cycles(scene, samples)
    scene.render.resolution_x = int(max(width, depth) * pixels_per_metre * width / max(width, depth))
    scene.render.resolution_y = int(max(width, depth) * pixels_per_metre * depth / max(width, depth))
    scene.render.filepath = path
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.render.render(write_still=True)

    for obj in hidden:
        obj.hide_render = False
    for extra in (camera, sun):
        bpy.data.objects.remove(extra, do_unlink=True)
    return path
