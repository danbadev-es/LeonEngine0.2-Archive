"""
LeonHorror - look at the solved clips (the enemy kinds' and the player's) on the characters they were made for.

Run by Blender, after import_animations.py --enemies / --player has written the clips' FBX files:

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
        --python Games/LeonHorror/Scripts/review_enemy_clips.py -- [--only PatientWalk,OrderlyDeath] \
        [--frames 8] [--samples 16] [--no-render]

For every clip it plays the FBX in Raw/Anim on the character's own mesh (the one built by
Art/build_characters.py into Intermediate/Art/Raw/Characters), then

  * prints, over every frame: how many triangles of each forearm and hand cut into the trunk, the
    head or the legs, how many triangles of one shin cut into the other, how far below the floor
    the mesh goes, how far a foot that should be planted moves, and for a loop how far the last
    frame is from the first;
  * writes Intermediate/Art/Sheets/Clip_<Name>.png: evenly spaced frames, a row from the front
    three-quarter and a row from the side (rendered with Art/leonart/charsheet.py).

The torsos of the two characters differ a lot, so a clip is only ever judged on its own character.
"""

import math
import os
import sys

import bpy
import numpy as np
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJECT, "Art"))
sys.path.insert(0, HERE)

from leonart import charsheet, png  # noqa: E402
import enemy_clips  # noqa: E402
import player_clips  # noqa: E402

ALL_CLIPS = {**enemy_clips.CLIPS, **player_clips.CLIPS}


def character_of(name):
    """The Patient's and the Orderly's clips carry their names; everything else is the player's."""
    return next((kind for kind in ("Patient", "Orderly") if name.startswith(kind)), "Protagonist")

P = "mixamorig:"

# Where the camera looks, per clip: (target, distance, height, tile size). The deaths end on the floor.
STANDING = ((0.0, -0.1, 0.92), 6.6, 1.3, (360, 600))
VIEWS = {
    "PatientAttack": ((0.0, -0.3, 0.9), 7.0, 1.3, (400, 600)),
    "OrderlyAttack": ((0.0, -0.3, 1.05), 8.0, 1.4, (420, 660)),
    "PatientDeath": ((-0.45, -0.2, 0.45), 6.4, 2.2, (560, 460)),
    "OrderlyDeath": ((0.0, -0.75, 0.5), 7.4, 2.4, (600, 480)),
    "MeleeSwing": ((0.0, -0.35, 1.05), 8.4, 1.5, (460, 640)),
    "PlayerPickupLow": ((0.0, -0.25, 0.75), 6.8, 1.3, (400, 600)),
    "PlayerDeathBackward": ((0.0, 0.55, 0.5), 7.6, 2.4, (600, 480)),
}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = {"only": "", "frames": "8", "samples": "16", "anims": os.path.join(PROJECT, "Raw", "Anim"),
            "characters": os.path.join(PROJECT, "Intermediate", "Art", "Raw", "Characters"),
            "preview": os.path.join(PROJECT, "Intermediate", "Art", "Preview"),
            "out": os.path.join(PROJECT, "Intermediate", "Art", "Sheets"), "no-render": False}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip("-")
        if key in args and isinstance(args[key], bool):
            args[key] = True
            i += 1
        elif key in args and i + 1 < len(argv):
            args[key] = argv[i + 1]
            i += 2
        else:
            raise SystemExit("unknown option " + argv[i])
    return args


def load_character(name, args):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    path = os.path.join(args["characters"], "SK_%s.fbx" % name)
    if not os.path.isfile(path):
        raise SystemExit(path + " is missing: build it with Art/build_characters.py --only " + name)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
    armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.type == "MESH")
    for material in mesh.data.materials:
        # The copies the character build left for Blender (already the right way up for it).
        color = os.path.join(args["preview"], material.name + "_Color.png")
        material.use_nodes = True
        shader = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
        shader.inputs["Roughness"].default_value = 0.75
        if os.path.isfile(color):
            texture = material.node_tree.nodes.new("ShaderNodeTexImage")
            texture.image = bpy.data.images.load(color)
            material.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
    return armature, mesh


def load_clip(armature, path):
    """The action of an animation FBX, put on the character's armature. Returns (action, first, last)."""
    before = set(bpy.data.objects)
    actions_before = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(path), automatic_bone_orientation=False)
    for obj in set(bpy.data.objects) - before:
        bpy.data.objects.remove(obj, do_unlink=True)
    action = next(iter(set(bpy.data.actions) - actions_before))
    for pose_bone in armature.pose.bones:
        pose_bone.rotation_mode = "QUATERNION"
    if armature.animation_data is None:
        armature.animation_data_create()
    data = armature.animation_data
    data.action = action
    if hasattr(data, "action_slot") and getattr(action, "slots", None):
        data.action_slot = action.slots[0]
    first, last = action.frame_range
    return action, int(round(first)), int(round(last))


class Checker:
    """Splits the mesh by the bone that owns each vertex, to ask whether one part cuts into another."""

    def __init__(self, mesh, armature):
        self.mesh, self.armature = mesh, armature
        groups = [g.name[len(P):] for g in mesh.vertex_groups]
        owner = []
        for vertex in mesh.data.vertices:
            best = max(vertex.groups, key=lambda g: g.weight) if vertex.groups else None
            owner.append(groups[best.group] if best else "Hips")

        def part(bone):
            side = "L" if bone.startswith("Left") else "R" if bone.startswith("Right") else ""
            if "ForeArm" in bone or "Hand" in bone:
                return "hand" + side
            if bone.endswith("Shoulder") or bone.endswith("Arm"):
                return "upperarm" + side
            if bone.endswith("UpLeg"):
                return "body"
            if "Leg" in bone or "Foot" in bone or "Toe" in bone:
                return "shin" + side
            return "body"

        self.owner = owner
        self.part = [part(bone) for bone in owner]
        mesh.data.calc_loop_triangles()
        self.triangles = {}
        for tri in mesh.data.loop_triangles:
            kinds = {self.part[v] for v in tri.vertices}
            if len(kinds) == 1:
                self.triangles.setdefault(kinds.pop(), []).append(tuple(tri.vertices))
        self.baseline = None

    def positions(self):
        depsgraph = bpy.context.evaluated_depsgraph_get()
        evaluated = self.mesh.evaluated_get(depsgraph)
        data = evaluated.to_mesh()
        co = np.empty(len(data.vertices) * 3, dtype=np.float64)
        data.vertices.foreach_get("co", co)
        evaluated.to_mesh_clear()
        co = co.reshape(-1, 3)
        world = np.array(self.mesh.matrix_world)
        return co @ world[:3, :3].T + world[:3, 3]

    def measure(self):
        co = self.positions()
        trees = {kind: BVHTree.FromPolygons(co.tolist(), tris) for kind, tris in self.triangles.items()}
        body_and_legs = ("body", "shinL", "shinR")
        # Which part of the body a hand is in, by the bone that owns the triangles it crosses.
        self.cut_into = {}
        for hand in ("handL", "handR"):
            owners = {}
            for kind in body_and_legs:
                for _mine, theirs in trees[hand].overlap(trees[kind]):
                    bone = self.owner[self.triangles[kind][theirs][0]]
                    owners[bone] = owners.get(bone, 0) + 1
            self.cut_into[hand] = owners
        cuts = {
            "handL": sum(self.cut_into["handL"].values()),
            "handR": sum(self.cut_into["handR"].values()),
            "hands": len(trees["handL"].overlap(trees["handR"])),
            "shins": len(trees["shinL"].overlap(trees["shinR"])),
        }
        self.lowest_bone = self.owner[int(co[:, 2].argmin())]
        return cuts, float(co[:, 2].min())

    def joint(self, bone):
        pose_bone = self.armature.pose.bones[P + bone]
        return self.armature.matrix_world @ pose_bone.head


def check_clip(name, checker, first, last, loop):
    scene = bpy.context.scene
    worst = {"handL": 0, "handR": 0, "hands": 0, "shins": 0}
    where, into = {}, {}
    lowest, lowest_frame, lowest_bone = 0.0, first, "-"
    for frame in range(first, last + 1):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        cuts, low = checker.measure()
        for key, value in cuts.items():
            value -= checker.baseline[key]
            if value > worst[key]:
                worst[key], where[key] = value, frame
                if key in checker.cut_into:
                    into[key] = max(checker.cut_into[key], key=checker.cut_into[key].get)
        if low < lowest:
            lowest, lowest_frame, lowest_bone = low, frame, checker.lowest_bone
    print("CHECK %-14s cuts into the body: left forearm/hand %d%s, right %d%s; hand into hand %d%s; shin into shin %d%s; lowest point %.3f m at frame %d (%s); at the last frame %.3f m (%s)" % (
        name, worst["handL"], " (frame %d, mostly %s)" % (where["handL"], into["handL"]) if worst["handL"] else "",
        worst["handR"], " (frame %d, mostly %s)" % (where["handR"], into["handR"]) if worst["handR"] else "",
        worst["hands"], " (frame %d)" % where["hands"] if worst["hands"] else "",
        worst["shins"], " (frame %d)" % where["shins"] if worst["shins"] else "", lowest, lowest_frame, lowest_bone, low, checker.lowest_bone))
    if name.endswith("Death"):
        # The pose that is held: what of it is under the floor, by the bone that owns it.
        co = checker.positions()
        under = {}
        for bone, z in zip(checker.owner, co[:, 2]):
            if z < -0.015:
                under[bone] = min(under.get(bone, 0.0), float(z))
        print("CHECK %-14s held pose, under the floor: %s" % (name, ", ".join("%s %.3f" % item for item in sorted(under.items(), key=lambda i: i[1])) or "nothing"))
    if loop:
        scene.frame_set(first)
        bpy.context.view_layer.update()
        start = checker.positions()
        scene.frame_set(last)
        bpy.context.view_layer.update()
        print("CHECK %-14s loop: the last frame is %.5f m from the first at its worst vertex" % (name, float(np.abs(checker.positions() - start).max())))


def check_walk(name, checker, first, last, speed):
    """While a toe joint is on the floor it must travel back at the walking speed, as the ground does."""
    scene = bpy.context.scene
    for side in ("Left", "Right"):
        rows = []
        for frame in range(first, last + 1):
            scene.frame_set(frame)
            bpy.context.view_layer.update()
            rows.append(checker.joint(side + "ToeBase").copy())
        # Frames where the ball of the foot is at its floor height.
        floor = min(p.z for p in rows)
        speeds = []
        for a, b in zip(rows, rows[1:]):
            if a.z < floor + 0.004 and b.z < floor + 0.004:
                speeds.append((b.y - a.y) * 30.0)  # +Y is backwards
        if speeds:
            print("CHECK %-14s %s foot on the floor for %d of %d frames, travelling back at %.3f..%.3f m/s (wanted %.3f)" % (
                name, side.lower(), len(speeds), last - first, min(speeds), max(speeds), speed))


def add_pipe(armature):
    """
    A stand-in for the pipe, for the swing: 0.8 m along the right hand bone from just behind the
    grip, as the game hangs SM_Held_Pipe (ALeonHorrorCharacter::UpdateHeldItems).
    """
    scale = armature.matrix_world.to_scale().x
    bpy.ops.mesh.primitive_cylinder_add(radius=0.014 / scale, depth=0.8 / scale, vertices=10)
    pipe = bpy.context.active_object
    pipe.name = "PipeStandIn"
    pipe.parent = armature
    pipe.parent_type = "BONE"
    pipe.parent_bone = P + "RightHand"
    # A bone's children sit at its tail; the cylinder runs along Z and the bone along Y.
    length = armature.data.bones[P + "RightHand"].length
    pipe.matrix_parent_inverse.identity()
    pipe.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    pipe.location = (0.0, -length + (0.09 + 0.28) / scale, 0.03 / scale)
    return pipe


def render_clip(name, armature, first, last, loop, args):
    frames = int(args["frames"])
    target, distance, height, size = VIEWS.get(name, STANDING)
    camera = bpy.data.objects["StudioCamera"]
    scratch = os.path.join(args["out"], "Clip_%s_shot.png" % name)
    scene = bpy.context.scene
    picks = [first + int(round((last - first) * (i / frames if loop else i / (frames - 1)))) for i in range(frames)]
    rows = []
    for angle in (35, 90):
        tiles = []
        for frame in picks:
            scene.frame_set(frame)
            bpy.context.view_layer.update()
            tiles.append(charsheet._shot(camera, angle, scratch, size, target=target, distance=distance, height=height))
        rows.append(np.concatenate(tiles, axis=1))
    path = os.path.join(args["out"], "Clip_%s.png" % name)
    png.write(path, np.concatenate(rows, axis=0))
    print("SHEET %s  frames %s of %d..%d" % (path, picks, first, last))


def main():
    args = parse_args()
    wanted = [n for n in args["only"].split(",") if n] or list(ALL_CLIPS)
    os.makedirs(args["out"], exist_ok=True)
    for character in ("Patient", "Orderly", "Protagonist"):
        names = [n for n in wanted if character_of(n) == character]
        if not names:
            continue
        armature, mesh = load_character(character, args)
        checker = Checker(mesh, armature)
        bpy.context.view_layer.update()
        checker.baseline, _ = checker.measure()
        print("CHECK %s at rest: %s (taken off what follows)" % (character, checker.baseline))
        if not args["no-render"]:
            charsheet.studio(int(args["samples"]))
        for name in names:
            seconds, loop, _pose = ALL_CLIPS[name]
            _action, first, last = load_clip(armature, os.path.join(args["anims"], name + ".fbx"))
            if last - first != int(round(seconds * 30)):
                print("CHECK %-14s WARNING: %d frames in the file, %d expected" % (name, last - first, int(round(seconds * 30))))
            check_clip(name, checker, first, last, loop)
            if name.endswith("Walk"):
                speed = {"Patient": enemy_clips.PATIENT_WALK_SPEED, "Orderly": enemy_clips.ORDERLY_WALK_SPEED,
                         "Protagonist": player_clips.PLAYER_WALK_SPEED}[character]
                check_walk(name, checker, first, last, speed)
            if not args["no-render"]:
                pipe = add_pipe(armature) if name == "MeleeSwing" else None
                render_clip(name, armature, first, last, loop, args)
                if pipe is not None:
                    bpy.data.objects.remove(pipe, do_unlink=True)


main()
