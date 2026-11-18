"""
LeonHorror - author animation clips for the mannequin with Blender, without the UI.

Run by Blender, not by the system Python:

    blender -b --factory-startup --python Projects/LeonHorror/Scripts/make_animations.py -- \
        --mode roundtrip --source Projects/LeonHorror/Raw/Anim/Idle.fbx --out <dir>
    blender -b --factory-startup --python Projects/LeonHorror/Scripts/make_animations.py -- \
        --mode author --rig Projects/LeonHorror/Raw/YBot.fbx --out Projects/LeonHorror/Raw/Anim

Use Projects/LeonHorror/Scripts/import_animations.py, which wraps both steps and the import into .lanim.

How the engine reads the result (Engine/Source/Runtime/Assets/Private/FSkeletalImporter.cpp):
  * one clip per FBX, named after the file, when the file has a single animation stack;
  * only bone-local transforms are sampled, at 30 Hz, and matched to the skeleton by bone name;
  * an FBX that also contains a mesh is treated as a new character, so only the armature is exported.
"""

import math
import os
import sys

import bpy
from mathutils import Euler, Quaternion, Vector

FPS = 30


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = {"mode": "author", "source": "", "rig": "", "out": "", "only": ""}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip("-")
        if key in args and i + 1 < len(argv):
            args[key] = argv[i + 1]
            i += 2
        else:
            i += 1
    return args


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS
    bpy.context.scene.render.fps_base = 1.0


def import_fbx(path):
    # The legacy importer pairs with the exporter below: bone axes survive the round trip.
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(path), automatic_bone_orientation=False)
    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if not armatures:
        raise RuntimeError("no armature in " + path)
    return armatures[0]


def export_armature(armature, path, frame_start, frame_end):
    scene = bpy.context.scene
    scene.frame_start = frame_start
    scene.frame_end = frame_end

    # The exporter writes each bone's reference transform from the pose at the current frame, and the
    # engine importer scales animated translation by rest length / that reference length. Exporting
    # from an animated frame therefore skews translation (the hips of a crouched first frame came out
    # 6 cm too high). Park the scene on a frame keyed at the rest pose, just before the exported range.
    rest_frame = frame_start - 1
    for pose_bone in armature.pose.bones:
        pose_bone.location = (0.0, 0.0, 0.0)
        pose_bone.scale = (1.0, 1.0, 1.0)
        pose_bone.keyframe_insert(data_path="location", frame=rest_frame)
        pose_bone.keyframe_insert(data_path="scale", frame=rest_frame)
        if pose_bone.rotation_mode == "QUATERNION":
            pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=rest_frame)
        else:
            pose_bone.rotation_euler = (0.0, 0.0, 0.0)
            pose_bone.keyframe_insert(data_path="rotation_euler", frame=rest_frame)
    scene.frame_set(rest_frame)

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.export_scene.fbx(
        filepath=os.path.abspath(path),
        use_selection=True,
        object_types={"ARMATURE"},
        add_leaf_bones=False,
        use_armature_deform_only=False,
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
        axis_forward="-Z",
        axis_up="Y",
        apply_scale_options="FBX_SCALE_NONE",
        bake_space_transform=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
    )
    print("EXPORTED", path, "frames", frame_start, frame_end)


def action_frame_range(armature):
    action = armature.animation_data.action if armature.animation_data else None
    if not action:
        return 1, 2
    start, end = action.frame_range
    return int(round(start)), int(round(end))


# --------------------------------------------------------------------------------------
# Round trip: re-export an existing clip untouched, to prove scale and axes survive.
# --------------------------------------------------------------------------------------
def run_roundtrip(source, out_dir):
    reset_scene()
    armature = import_fbx(source)
    start, end = action_frame_range(armature)
    stem = os.path.splitext(os.path.basename(source))[0].replace(" ", "")
    export_armature(armature, os.path.join(out_dir, stem + "RoundTrip.fbx"), start, end)


# --------------------------------------------------------------------------------------
# Authoring
# --------------------------------------------------------------------------------------
B = "mixamorig:"


class Rig:
    """Keys pose-bone rotations as offsets from the rest pose, in degrees."""

    def __init__(self, armature):
        self.armature = armature
        self.bones = armature.pose.bones
        for pose_bone in self.bones:
            pose_bone.rotation_mode = "XYZ"  # bones import as quaternions; euler keys would be ignored
        # Drop whatever action came with the file so the export has exactly one clip.
        if armature.animation_data:
            armature.animation_data.action = None
        for action in list(bpy.data.actions):
            bpy.data.actions.remove(action)
        armature.animation_data_create()

    def new_action(self, name):
        action = bpy.data.actions.new(name)
        self.armature.animation_data.action = action
        for pose_bone in self.bones:
            pose_bone.rotation_euler = (0.0, 0.0, 0.0)
            pose_bone.location = (0.0, 0.0, 0.0)
        return action

    def key(self, frame, pose):
        """pose: {bone short name: (x, y, z) degrees}. Unlisted bones are keyed at rest."""
        for pose_bone in self.bones:
            short = pose_bone.name[len(B):] if pose_bone.name.startswith(B) else pose_bone.name
            degrees = pose.get(short, (0.0, 0.0, 0.0))
            pose_bone.rotation_euler = Euler([math.radians(a) for a in degrees], "XYZ")
            pose_bone.keyframe_insert(data_path="rotation_euler", frame=frame)

    def finish(self, linear=False):
        action = self.armature.animation_data.action
        if not linear:
            return
        for fcurve in iter_fcurves(action):
            for point in fcurve.keyframe_points:
                point.interpolation = "LINEAR"


def iter_fcurves(action):
    """Blender 4.4+ stores curves in layers/strips/channelbags; older versions on the action."""
    if hasattr(action, "fcurves") and action.fcurves is not None:
        try:
            for fcurve in action.fcurves:
                yield fcurve
            return
        except Exception:
            pass
    for layer in getattr(action, "layers", []):
        for strip in getattr(layer, "strips", []):
            for bag in getattr(strip, "channelbags", []):
                for fcurve in bag.fcurves:
                    yield fcurve


def merge(*poses):
    out = {}
    for pose in poses:
        out.update(pose)
    return out


def seconds(value):
    return 1 + int(round(value * FPS))


# Pose fragments. Angles are offsets from the T-pose rest, found by rendering and adjusting; they are
# kept in one place so a clip reads as a sequence of named poses.
# Mixamo bone axes: Y runs along the bone; for the arms X bends forward/back and Z raises/lowers.
ARMS_DOWN = {
    "LeftArm": (0, 0, -68), "RightArm": (0, 0, 68),
    "LeftForeArm": (0, 0, -8), "RightForeArm": (0, 0, 8),
}
RELAXED = merge(ARMS_DOWN, {"Spine": (3, 0, 0), "Head": (-2, 0, 0)})


def clip_poses():
    """name -> (list of (time seconds, pose), loop flag)"""
    clips = {}

    # Slow, guarded walk: small steps, shoulders tight.
    def walk(phase):
        swing = math.sin(phase)
        lift = max(0.0, math.cos(phase))
        return merge(RELAXED, {
            "LeftUpLeg": (-22 * swing, 0, 0), "RightUpLeg": (22 * swing, 0, 0),
            "LeftLeg": (34 * max(0.0, -math.cos(phase)), 0, 0), "RightLeg": (34 * lift, 0, 0),
            "LeftArm": (14 * swing, 0, -66), "RightArm": (-14 * swing, 0, 66),
            "Spine": (5, 4 * swing, 0), "Hips": (0, -3 * swing, 0),
        })
    steps = 8
    clips["WalkForward"] = ([(1.0 * i / steps, walk(2 * math.pi * i / steps)) for i in range(steps + 1)], True)

    # Two-handed overhead swing with a blunt weapon.
    ready = merge(RELAXED, {"RightArm": (-40, 0, 20), "RightForeArm": (0, 0, 70), "LeftArm": (-25, 0, -30),
                            "LeftForeArm": (0, 0, -60), "Spine": (-6, -12, 0)})
    wind = merge(RELAXED, {"RightArm": (-150, 0, 10), "RightForeArm": (0, 0, 95), "LeftArm": (-120, 0, -15),
                           "LeftForeArm": (0, 0, -85), "Spine": (-14, -24, 0), "Spine1": (-6, -10, 0)})
    hit = merge(RELAXED, {"RightArm": (-35, 0, 30), "RightForeArm": (0, 0, 20), "LeftArm": (-30, 0, -35),
                          "LeftForeArm": (0, 0, -25), "Spine": (22, 20, 0), "Spine1": (8, 10, 0)})
    clips["MeleeSwing"] = ([(0.0, ready), (0.22, wind), (0.36, hit), (0.5, hit), (0.8, ready)], False)

    # Pistol held out in the right hand, left hand supporting. Upper body only at runtime.
    aim = merge(RELAXED, {"RightArm": (-78, 0, 12), "RightForeArm": (0, 0, 6), "LeftArm": (-70, 0, 28),
                          "LeftForeArm": (0, 0, -34), "Spine": (2, -8, 0), "Head": (0, 6, 0)})
    aim_breathe = merge(aim, {"Spine": (3, -8, 0), "RightArm": (-77, 0, 12)})
    clips["PistolAimIdle"] = ([(0.0, aim), (1.0, aim_breathe), (2.0, aim)], True)
    recoil = merge(aim, {"RightArm": (-92, 0, 12), "RightForeArm": (0, 0, 22), "LeftArm": (-82, 0, 28),
                         "Spine": (-3, -8, 0)})
    clips["PistolFire"] = ([(0.0, aim), (0.05, recoil), (0.3, aim)], False)

    # Staggered backwards by a blow.
    flinch = merge(RELAXED, {"Spine": (-20, 10, 6), "Spine1": (-10, 4, 0), "Head": (-16, -10, 0),
                             "LeftArm": (20, 0, -40), "RightArm": (25, 0, 45), "Hips": (-6, 0, 0)})
    clips["HitReact"] = ([(0.0, RELAXED), (0.1, flinch), (0.5, RELAXED)], False)

    # Reaching forward and down to pick something up or work a handle.
    reach = merge(RELAXED, {"RightArm": (-62, 0, 20), "RightForeArm": (0, 0, 30), "Spine": (18, -6, 0),
                            "Head": (10, 0, 0)})
    clips["Interact"] = ([(0.0, RELAXED), (0.3, reach), (0.55, reach), (0.9, RELAXED)], False)

    # Enemy: a dragging, lopsided shamble.
    def shamble(phase):
        swing = math.sin(phase)
        return merge(RELAXED, {
            "Spine": (26, 8 * swing, 10), "Spine1": (10, 0, 6), "Head": (22, -14, 12), "Neck": (8, 0, 0),
            "LeftUpLeg": (-16 * swing, 0, 0), "RightUpLeg": (10 * swing - 6, 0, 8),
            "LeftLeg": (26 * max(0.0, -math.cos(phase)), 0, 0), "RightLeg": (8, 0, 0),
            "LeftArm": (10 + 8 * swing, 0, -74), "RightArm": (-34, 0, 52), "RightForeArm": (0, 0, 48),
            "Hips": (0, -5 * swing, 4),
        })
    clips["EnemyShamble"] = ([(1.4 * i / steps, shamble(2 * math.pi * i / steps)) for i in range(steps + 1)], True)

    # Enemy: both arms thrown forward in a lunge.
    rear = merge(RELAXED, {"Spine": (-8, 0, 0), "LeftArm": (30, 0, -50), "RightArm": (30, 0, 50), "Head": (-10, 0, 0)})
    lunge = merge(RELAXED, {"Spine": (34, 0, 0), "Spine1": (12, 0, 0), "LeftArm": (-95, 0, -18),
                            "RightArm": (-95, 0, 18), "LeftForeArm": (0, 0, -12), "RightForeArm": (0, 0, 12),
                            "Head": (-24, 0, 0)})
    clips["EnemyAttack"] = ([(0.0, RELAXED), (0.3, rear), (0.45, lunge), (0.65, lunge), (1.0, RELAXED)], False)
    return clips


def run_author(rig_path, out_dir, only):
    clips = clip_poses()
    for name, (keys, loop) in clips.items():
        if only and name != only:
            continue
        reset_scene()
        armature = import_fbx(rig_path)
        # The rig file carries the mesh too; it is not exported, but keep it for preview renders.
        rig = Rig(armature)
        rig.new_action(name)
        last_frame = 1
        for time, pose in keys:
            frame = seconds(time)
            rig.key(frame, pose)
            last_frame = max(last_frame, frame)
        rig.finish(linear=loop)
        export_armature(armature, os.path.join(out_dir, name + ".fbx"), 1, last_frame)


def main():
    args = parse_args()
    if not args["out"]:
        raise SystemExit("--out is required")
    if args["mode"] == "roundtrip":
        run_roundtrip(args["source"], args["out"])
    else:
        run_author(args["rig"], args["out"], args["only"])


main()
