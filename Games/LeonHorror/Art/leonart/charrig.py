"""
The shared character rig: the mannequin's armature, the skin weights taken from it, and the FBX
that the engine's importer turns into a skeleton with the same bones.

Every character is skinned to the armature of Raw/YBot.fbx, untouched, so the clips made for the
mannequin play on all of them. The mannequin's own mesh is only used as a reference for skin
weights (it was weighted for exactly these bones) and is never exported.

What the importer needs (Engine/Source/Developer/AssetPipeline/Private/FSkeletalImporter.cpp):
  * every bone that has a skin cluster, and every node above one, becomes a bone. Blender writes
    the armature object as a node above the hips, which would become a 53rd bone, so the exported
    file is rewritten with the hips attached to the scene root (see `_detach_armature_node`);
  * Blender writes a cluster for every bone of the armature, so the end bones that the mannequin's
    cooked skeleton does not have (finger tips, toe ends, head top) are removed before export;
  * the rest pose is rebuilt from the clusters' bind matrices, in the mesh's world space, in metres.
"""

import os

import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

PREFIX = "mixamorig:"

# Bones Blender imports that the cooked mannequin skeleton (52 bones) does not have.
END_BONES = ("HeadTop_End", "LeftToe_End", "RightToe_End") + tuple(
    side + "Hand" + finger + "4" for side in ("Left", "Right") for finger in ("Thumb", "Index", "Middle", "Ring", "Pinky"))


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30


class Rig:
    """The armature, its joints in world space (metres, Z up, facing -Y), and the weight reference."""

    def __init__(self, fbx_path):
        bpy.ops.import_scene.fbx(filepath=os.path.abspath(fbx_path), automatic_bone_orientation=False)
        self.armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")
        if self.armature.animation_data:
            self.armature.animation_data.action = None
        for action in list(bpy.data.actions):
            bpy.data.actions.remove(action)

        world = self.armature.matrix_world
        self.head, self.tail = {}, {}
        for bone in self.armature.data.bones:
            short = bone.name[len(PREFIX):]
            self.head[short] = np.array(world @ bone.head_local)
            self.tail[short] = np.array(world @ bone.tail_local)

        self._read_reference()
        self._remove_end_bones()
        self.bones = [bone.name[len(PREFIX):] for bone in self.armature.data.bones]
        self.index = {name: i for i, name in enumerate(self.bones)}
        # Reference weights were read against the reference's own group names; reorder to self.bones.
        columns = [self._ref_names.index(name) if name in self._ref_names else -1 for name in self.bones]
        weights = np.zeros((self._ref_weights.shape[0], len(self.bones)), dtype=np.float32)
        for target, source in enumerate(columns):
            if source >= 0:
                weights[:, target] = self._ref_weights[:, source]
        total = weights.sum(axis=1, keepdims=True)
        self.ref_weights = weights / np.maximum(total, 1.0e-8)

    def j(self, name):
        """World position of a joint (the head of the bone)."""
        return self.head[name].copy()

    # ------------------------------------------------------------------ the mannequin as reference
    def _read_reference(self):
        positions, triangles, rows, names = [], [], [], []
        for obj in [o for o in bpy.data.objects if o.type == "MESH"]:
            mesh = obj.data
            base = len(positions)
            group_names = [g.name[len(PREFIX):] for g in obj.vertex_groups]
            for name in group_names:
                if name not in names:
                    names.append(name)
            for vertex in mesh.vertices:
                positions.append(tuple(obj.matrix_world @ vertex.co))
                rows.append([(names.index(group_names[g.group]), g.weight) for g in vertex.groups])
            mesh.calc_loop_triangles()
            for tri in mesh.loop_triangles:
                triangles.append(tuple(base + v for v in tri.vertices))
            bpy.data.objects.remove(obj, do_unlink=True)
        weights = np.zeros((len(positions), len(names)), dtype=np.float32)
        for i, row in enumerate(rows):
            for column, weight in row:
                weights[i, column] = weight
        self._ref_names = names
        self._ref_weights = weights
        self.ref_positions = np.array(positions, dtype=np.float64)
        self.ref_triangles = np.array(triangles, dtype=np.int64)
        self.ref_bvh = BVHTree.FromPolygons([Vector(p) for p in positions], [tuple(int(i) for i in t) for t in triangles])

    def _remove_end_bones(self):
        bpy.context.view_layer.objects.active = self.armature
        bpy.ops.object.mode_set(mode="EDIT")
        edit_bones = self.armature.data.edit_bones
        for name in END_BONES:
            bone = edit_bones.get(PREFIX + name)
            if bone is not None:
                edit_bones.remove(bone)
        bpy.ops.object.mode_set(mode="OBJECT")

    # ------------------------------------------------------------------ weights
    def reference_weights(self, points, axis_points=None):
        """
        Skin weights for `points` (N x 3), read off the mannequin.

        The plain answer is the nearest point of the mannequin's surface, which goes wrong where a
        wide garment is as close to a neighbouring limb as to its own (the flank of a big torso
        and the underside of the arm). So when the caller knows the point of the bone axis each
        vertex was built around, the surface is looked for along the line from that axis point
        through the vertex first, and the nearest point is the fallback.
        """
        out = np.zeros((len(points), len(self.bones)), dtype=np.float32)
        tris, weights, positions = self.ref_triangles, self.ref_weights, self.ref_positions
        for i, point in enumerate(points):
            p = Vector(point)
            location, _normal, face, distance = self.ref_bvh.find_nearest(p)
            if axis_points is not None:
                a = Vector(axis_points[i])
                direction = p - a
                length = direction.length
                if length > 1.0e-4:
                    direction /= length
                    best = None
                    origin = a
                    for _ in range(6):
                        hit, _n, hit_face, _d = self.ref_bvh.ray_cast(origin, direction, 0.6)
                        if hit is None:
                            break
                        gap = (hit - p).length
                        if best is None or gap < best[0]:
                            best = (gap, hit, hit_face)
                        origin = hit + direction * 1.0e-4
                    if best is not None and best[0] <= distance * 1.5 + 0.02:
                        location, face = best[1], best[2]
            a, b, c = (int(v) for v in tris[face])
            pa, pb, pc = positions[a], positions[b], positions[c]
            # Barycentric coordinates of the surface point in its triangle.
            v0, v1, v2 = pb - pa, pc - pa, np.array(location) - pa
            d00, d01, d11 = v0 @ v0, v0 @ v1, v1 @ v1
            d20, d21 = v2 @ v0, v2 @ v1
            denom = d00 * d11 - d01 * d01
            if abs(denom) < 1.0e-18:
                out[i] = weights[a]
                continue
            v = (d11 * d20 - d01 * d21) / denom
            w = (d00 * d21 - d01 * d20) / denom
            u = 1.0 - v - w
            bary = np.clip(np.array([u, v, w]), 0.0, 1.0)
            bary /= bary.sum()
            out[i] = bary[0] * weights[a] + bary[1] * weights[b] + bary[2] * weights[c]
        return out

    def columns(self, names):
        return [self.index[name] for name in names]

    # ------------------------------------------------------------------ binding and export
    def bind(self, obj, weights):
        """Vertex groups from an N x bones array (already limited and normalised), and the modifier."""
        for name in self.bones:
            obj.vertex_groups.new(name=PREFIX + name)
        for column, name in enumerate(self.bones):
            group = obj.vertex_groups[PREFIX + name]
            values = weights[:, column]
            for vertex in np.nonzero(values > 0.0)[0]:
                group.add([int(vertex)], float(values[vertex]), "REPLACE")
        modifier = obj.modifiers.new("Armature", "ARMATURE")
        modifier.object = self.armature
        obj.parent = self.armature
        obj.matrix_parent_inverse = self.armature.matrix_world.inverted()

    def rest(self):
        """Back to the bind pose, with no action."""
        if self.armature.animation_data:
            self.armature.animation_data.action = None
        for pose_bone in self.armature.pose.bones:
            pose_bone.location = (0.0, 0.0, 0.0)
            pose_bone.scale = (1.0, 1.0, 1.0)
            pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pose_bone.rotation_euler = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()

    def export(self, obj, path):
        """One FBX with the armature and the skinned mesh, no animation, as the importer reads this rig."""
        self.rest()
        bpy.ops.object.select_all(action="DESELECT")
        self.armature.select_set(True)
        obj.select_set(True)
        bpy.context.view_layer.objects.active = self.armature
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        bpy.ops.export_scene.fbx(
            filepath=os.path.abspath(path),
            use_selection=True,
            object_types={"ARMATURE", "MESH"},
            add_leaf_bones=False,
            use_armature_deform_only=False,
            bake_anim=False,
            mesh_smooth_type="OFF",
            use_tspace=False,
            use_mesh_modifiers=False,  # only the armature modifier is on the mesh
            axis_forward="-Z",
            axis_up="Y",
            apply_scale_options="FBX_SCALE_NONE",
            bake_space_transform=False,
            primary_bone_axis="Y",
            secondary_bone_axis="X",
        )
        _detach_armature_node(os.path.abspath(path), self.armature.name, PREFIX + "Hips")


# --------------------------------------------------------------------------------------
# FBX rewrite
# --------------------------------------------------------------------------------------
def _fbx_local_matrix(properties):
    """The local matrix of a Model from its Lcl properties (translation, XYZ euler degrees, scale)."""
    from mathutils import Euler
    import math
    values = {"Lcl Translation": (0.0, 0.0, 0.0), "Lcl Rotation": (0.0, 0.0, 0.0), "Lcl Scaling": (1.0, 1.0, 1.0)}
    for prop in properties.elems:
        key = prop.props[0].decode()
        if key in values:
            values[key] = tuple(prop.props[4:7])
    rotation = Euler([math.radians(a) for a in values["Lcl Rotation"]], "XYZ").to_matrix().to_4x4()
    scale = Matrix.Diagonal(Vector(values["Lcl Scaling"] + (1.0,)))
    return Matrix.Translation(Vector(values["Lcl Translation"])) @ rotation @ scale


def _detach_armature_node(path, armature_name, root_bone_name):
    """
    Attaches the root bone to the scene root instead of the armature object's node, keeping its
    place in the world. The importer makes a bone of every node above a skinned bone; with the
    armature node in between, the skeleton would get a bone the mannequin's does not have.
    """
    import math
    from io_scene_fbx import encode_bin, parse_fbx

    root, version = parse_fbx.parse(path)

    def child(elem, name):
        return next(e for e in elem.elems if e.id == name)

    objects = child(root, b"Objects")
    models = {}
    for elem in objects.elems:
        if elem.id == b"Model":
            models[elem.props[1].split(b"\x00\x01")[0].decode()] = elem
    armature, bone = models[armature_name], models[root_bone_name]
    armature_id, bone_id = armature.props[0], bone.props[0]

    world = _fbx_local_matrix(child(armature, b"Properties70")) @ _fbx_local_matrix(child(bone, b"Properties70"))
    translation, rotation, scale = world.decompose()
    euler = rotation.to_euler("XYZ")
    new_values = {"Lcl Translation": tuple(translation), "Lcl Rotation": tuple(math.degrees(a) for a in euler),
                  "Lcl Scaling": tuple(scale)}
    found = set()
    properties = child(bone, b"Properties70")
    for prop in properties.elems:
        key = prop.props[0].decode()
        if key in new_values:
            prop.props[4:7] = list(new_values[key])
            found.add(key)
    # Blender leaves out a property that has its default value.
    for key in new_values:
        if key not in found:
            properties.elems.append(parse_fbx.FBXElem(
                b"P", [key.encode(), key.encode(), b"", b"A"] + list(new_values[key]), bytearray(b"SSSSDDD"), []))

    changed = 0
    for connection in child(root, b"Connections").elems:
        if connection.props[0] == b"OO" and connection.props[1] == bone_id and connection.props[2] == armature_id:
            connection.props[2] = 0
            changed += 1
    if changed != 1:
        raise RuntimeError("expected one hips-to-armature connection in %s, found %d" % (path, changed))

    adders = {
        b"Y": "add_int16", b"C": "add_bool", b"I": "add_int32", b"L": "add_int64", b"F": "add_float32",
        b"D": "add_float64", b"R": "add_bytes", b"S": "add_string", b"i": "add_int32_array",
        b"l": "add_int64_array", b"f": "add_float32_array", b"d": "add_float64_array",
        b"b": "add_bool_array", b"c": "add_byte_array",
    }

    def convert(parent, elem):
        out = encode_bin.FBXElem(elem.id)
        for value, kind in zip(elem.props, elem.props_type):
            code = bytes([kind])
            if code == b"C":
                value = bool(value[0]) if isinstance(value, (bytes, bytearray)) else bool(value)
            elif code in (b"Y", b"I", b"L"):
                value = int(value)
            elif code in (b"F", b"D"):
                value = float(value)
            getattr(out, adders[code])(value)
        for sub in elem.elems:
            if sub is not None:
                convert(out, sub)
        parent.elems.append(out)

    out_root = encode_bin.FBXElem(b"")
    for elem in root.elems:
        if elem is not None:
            convert(out_root, elem)
    encode_bin.write(path, out_root, version)


# --------------------------------------------------------------------------------------
# Weight tools (numpy, on the built mesh)
# --------------------------------------------------------------------------------------
def smooth_weights(weights, edges, iterations=2, factor=0.5, mask=None):
    """Averages each vertex with its neighbours. `mask` (N) scales how much each vertex moves."""
    count = weights.shape[0]
    a = np.concatenate([edges[:, 0], edges[:, 1]])
    b = np.concatenate([edges[:, 1], edges[:, 0]])
    degree = np.bincount(a, minlength=count).astype(np.float32)
    degree = np.maximum(degree, 1.0)[:, None]
    amount = factor if mask is None else (factor * mask)[:, None]
    for _ in range(iterations):
        total = np.zeros_like(weights)
        np.add.at(total, a, weights[b])
        weights = weights * (1.0 - amount) + (total / degree) * amount
    return weights


def limit_weights(weights, influences=4):
    """Keeps the heaviest `influences` per vertex and normalises, as the engine does on import."""
    order = np.argsort(-weights, axis=1)
    keep = np.zeros_like(weights, dtype=bool)
    rows = np.arange(weights.shape[0])[:, None]
    keep[rows, order[:, :influences]] = True
    out = np.where(keep & (weights > 0.004), weights, 0.0)
    total = out.sum(axis=1, keepdims=True)
    if np.any(total <= 0.0):
        raise RuntimeError("%d vertices without weights" % int((total <= 0.0).sum()))
    return (out / total).astype(np.float32)
