"""
Building, unwrapping and exporting meshes.

A mesh is assembled in a `Mesh` (a thin wrapper over bmesh) out of boxes, panels and profiles, each
face tagged with the name of a library material. `finish` turns it into a Blender object with:

  * UV set 0 for the tiling textures: every face is projected flat along its dominant axis, in
    metres divided by the material's tile size. Texel density is therefore the same everywhere and
    a pattern runs unbroken from one face to the next one in the same plane.
  * UV set 1 for the lightmap: every face in its own place, nothing overlapping.
  * Bevelled hard edges and normals weighted by face area, which is most of what makes a box read
    as an object instead of a diagram.

Blender axes: X right, Y back, Z up. The exporter converts to the engine's Y up.
"""

import json
import math
import os

import bmesh
import bpy
from mathutils import Vector

from . import materials as material_library


class Mesh:
    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.materials = []  # names, in slot order

    # ------------------------------------------------------------------ pieces
    def slot(self, material):
        if material not in self.materials:
            self.materials.append(material)
        return self.materials.index(material)

    def quad(self, corners, material):
        """Four points, counter-clockwise seen from the side the face looks at."""
        face = self.bm.faces.new([self.bm.verts.new(Vector(corner)) for corner in corners])
        face.material_index = self.slot(material)
        return face

    def box(self, low, high, material, skip=""):
        """
        Axis-aligned box from corner `low` to corner `high`. `skip` names faces to leave out, any of
        'x' 'X' 'y' 'Y' 'z' 'Z' (lower case: the face at the low end of that axis).
        """
        x0, y0, z0 = low
        x1, y1, z1 = high
        faces = {
            "x": [(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)],
            "X": [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
            "y": [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
            "Y": [(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)],
            "z": [(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)],
            "Z": [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
        }
        for key, corners in faces.items():
            if key not in skip:
                self.quad(corners, material)

    def panel(self, origin, u_axis, v_axis, width, height, material, holes=()):
        """
        A flat rectangle starting at `origin`, `width` along u_axis and `height` along v_axis, facing
        u x v. `holes` are (u0, v0, u1, v1) rectangles left open: doorways and windows. The panel is
        cut along the edges of the holes so no face spans one.
        """
        origin, u_axis, v_axis = Vector(origin), Vector(u_axis).normalized(), Vector(v_axis).normalized()
        us = sorted({0.0, width} | {min(max(h[0], 0.0), width) for h in holes} | {min(max(h[2], 0.0), width) for h in holes})
        vs = sorted({0.0, height} | {min(max(h[1], 0.0), height) for h in holes} | {min(max(h[3], 0.0), height) for h in holes})
        for i in range(len(us) - 1):
            for j in range(len(vs) - 1):
                cu, cv = (us[i] + us[i + 1]) * 0.5, (vs[j] + vs[j + 1]) * 0.5
                if any(h[0] < cu < h[2] and h[1] < cv < h[3] for h in holes):
                    continue
                self.quad([origin + u_axis * us[i] + v_axis * vs[j], origin + u_axis * us[i + 1] + v_axis * vs[j],
                           origin + u_axis * us[i + 1] + v_axis * vs[j + 1], origin + u_axis * us[i] + v_axis * vs[j + 1]], material)

    def cylinder(self, base, axis, radius, length, material, segments=16, caps=True):
        """A cylinder from `base` along `axis`."""
        base, axis = Vector(base), Vector(axis).normalized()
        side = axis.orthogonal().normalized()
        other = axis.cross(side)
        ring = [side * math.cos(2 * math.pi * i / segments) * radius + other * math.sin(2 * math.pi * i / segments) * radius
                for i in range(segments)]
        low = [self.bm.verts.new(base + offset) for offset in ring]
        high = [self.bm.verts.new(base + axis * length + offset) for offset in ring]
        index = self.slot(material)
        for i in range(segments):
            j = (i + 1) % segments
            face = self.bm.faces.new([low[i], low[j], high[j], high[i]])
            face.material_index = index
            face.smooth = True
        if caps:
            self.bm.faces.new(list(reversed(low))).material_index = index
            self.bm.faces.new(high).material_index = index

    # ------------------------------------------------------------------ result
    def finish(self, bevel=0.0, bevel_segments=2, smooth_angle=40.0):
        """Returns the Blender object, unwrapped twice and ready to export."""
        self.bm.normal_update()
        data = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(data)
        self.bm.free()
        obj = bpy.data.objects.new(self.name, data)
        bpy.context.scene.collection.objects.link(obj)
        for name in self.materials:
            data.materials.append(bpy.data.materials.get("M_" + name) or bpy.data.materials.new("M_" + name))

        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        # Coincident corners of separately built pieces become one, so bevels run around them.
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.remove_doubles(threshold=0.0005)
        bpy.ops.object.mode_set(mode="OBJECT")

        if bevel > 0.0:
            modifier = obj.modifiers.new("Bevel", "BEVEL")
            modifier.width = bevel
            modifier.segments = bevel_segments
            modifier.limit_method = "ANGLE"
            modifier.angle_limit = math.radians(40.0)
            modifier.harden_normals = True
            modifier.miter_outer = "MITER_ARC"
            bpy.ops.object.modifier_apply(modifier="Bevel")
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(smooth_angle))
        weighted = obj.modifiers.new("Weighted", "WEIGHTED_NORMAL")
        weighted.keep_sharp = True
        bpy.ops.object.modifier_apply(modifier="Weighted")

        unwrap_texture(obj, self.materials)
        unwrap_lightmap(obj)
        obj.select_set(False)
        return obj


def unwrap_texture(obj, material_names):
    """UV set 0: each face flat along its dominant axis, in metres over the material's tile size."""
    data = obj.data
    layer = data.uv_layers[0] if data.uv_layers else data.uv_layers.new(name="UVMap")
    layer.name = "UVMap"
    tile = []
    for name in material_names:
        entry = material_library.LIBRARY.get(name)
        tile.append(entry[2] if entry else 1.0)
    for polygon in data.polygons:
        normal = polygon.normal
        size = tile[polygon.material_index] if polygon.material_index < len(tile) else 1.0
        axis = max(range(3), key=lambda i: abs(normal[i]))
        for loop_index in polygon.loop_indices:
            p = data.vertices[data.loops[loop_index].vertex_index].co
            if axis == 2:  # floors and ceilings
                uv = (p.x, p.y)
            elif axis == 0:  # faces looking along X: across is Y, up is Z
                uv = (p.y, p.z)
            else:
                uv = (p.x, p.z)
            layer.data[loop_index].uv = (uv[0] / size, uv[1] / size)


def unwrap_lightmap(obj, margin=0.02):
    """UV set 1: every face in its own place inside the unit square."""
    data = obj.data
    while len(data.uv_layers) < 2:
        data.uv_layers.new(name="Lightmap")
    data.uv_layers[1].name = "Lightmap"
    data.uv_layers.active_index = 1
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    # Stretched to fill the square: a lightmap has no pattern to distort, and unused texels are
    # texels the bake pays for and the surface does not get.
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=margin, area_weight=0.0, correct_aspect=True,
                             scale_to_bounds=True)
    bpy.ops.object.mode_set(mode="OBJECT")
    data.uv_layers.active_index = 0


def surface_area(obj):
    return sum(polygon.area for polygon in obj.data.polygons)


def lightmap_resolution(obj, texels_per_metre=24.0, lowest=16, highest=512):
    """A power of two that gives about texels_per_metre on the mesh's surface."""
    wanted = math.sqrt(max(surface_area(obj), 1.0e-4)) * texels_per_metre
    resolution = lowest
    while resolution < wanted and resolution < highest:
        resolution *= 2
    return resolution


def export(obj, folder, texels_per_metre=24.0):
    """
    Writes <folder>/<name>.fbx with the object at the origin, and <name>.json beside it with what
    the level builder needs to place it: size, lightmap resolution, material slots.
    """
    os.makedirs(folder, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    path = os.path.join(folder, obj.name + ".fbx")
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"MESH"},
        axis_forward="-Z",
        axis_up="Y",
        apply_scale_options="FBX_SCALE_NONE",
        mesh_smooth_type="OFF",
        use_mesh_modifiers=True,
        use_tspace=False,
        bake_anim=False,
        path_mode="STRIP",
    )
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = [min(c[i] for c in corners) for i in range(3)]
    high = [max(c[i] for c in corners) for i in range(3)]
    info = {
        "name": obj.name,
        "triangles": sum(len(p.vertices) - 2 for p in obj.data.polygons),
        "area": round(surface_area(obj), 3),
        # Engine axes: x, up, -y.
        "bounds_min": [round(low[0], 4), round(low[2], 4), round(-high[1], 4)],
        "bounds_max": [round(high[0], 4), round(high[2], 4), round(-low[1], 4)],
        "lightmap_resolution": lightmap_resolution(obj, texels_per_metre),
        "materials": [slot.material.name for slot in obj.material_slots],
    }
    with open(os.path.join(folder, obj.name + ".json"), "w", encoding="utf-8") as handle:
        json.dump(info, handle, indent=2)
    obj.select_set(False)
    return info


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
