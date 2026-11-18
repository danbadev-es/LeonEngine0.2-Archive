"""
LeonHorror - author animation clips for the mannequin with Blender, without the UI.

Run by Blender, not by the system Python:

    blender -b --factory-startup --python Games/LeonHorror/Scripts/make_animations.py -- \
        --mode roundtrip --source Games/LeonHorror/Raw/Anim/Idle.fbx --out <dir>
    blender -b --factory-startup --python Games/LeonHorror/Scripts/make_animations.py -- \
        --mode author --rig Games/LeonHorror/Raw/YBot.fbx --out Games/LeonHorror/Raw/Anim

Use Games/LeonHorror/Scripts/import_animations.py, which wraps both steps and the import into .lanim.

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
    args = {"mode": "author", "source": "", "rig": "", "out": "", "only": "", "preview": ""}
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


# Pose vocabulary.
#
# Rotations are offsets from the T-pose rest, in degrees, applied X then Y then Z about the rest axes.
# What each axis does was measured on this rig (rotate one axis, see where the bone tip goes):
#   spine / head : +X bends forward, +Y turns left, +Z leans right
#   upper arm    : +X lowers the arm (90 = hanging at the side). Once lowered, Y swings it forward or
#                  back and Z swings it sideways across the body; both signs flip between left and right
#   forearm      : Z flexes the elbow (+ on the left arm, - on the right)
#   thigh        : +X swings the leg forward
#   shin         : -X bends the knee
# The helpers below hide the per-side signs so a clip reads in plain terms.

def arm(side, down, forward=0.0, inward=0.0):
    """Upper arm: `down` from horizontal, then swung `forward` and `inward` (toward the body centre)."""
    if side == "L":
        return (down, -forward, inward)
    return (down, forward, -inward)


def elbow(side, bend):
    return (0.0, 0.0, bend if side == "L" else -bend)


def thigh(forward):
    return (forward, 0.0, 0.0)


def knee(bend):
    return (-bend, 0.0, 0.0)


def torso(forward=0.0, turn_left=0.0, lean_right=0.0):
    return (forward, turn_left, lean_right)


RELAXED = {
    "LeftArm": arm("L", 76), "RightArm": arm("R", 76),
    "LeftForeArm": elbow("L", 12), "RightForeArm": elbow("R", 12),
    "Spine": torso(3), "Head": torso(-2),
}


def clip_poses():
    """name -> (list of (time seconds, pose), loop flag)"""
    clips = {}
    steps = 8

    # Standing still, arms at the sides, breathing. The Mixamo Idle is a rifle-ready stance.
    breathe = merge(RELAXED, {"Spine": torso(5), "Spine1": torso(2), "Head": torso(-4),
                              "LeftArm": arm("L", 78), "RightArm": arm("R", 78)})
    clips["IdleRelaxed"] = ([(0.0, RELAXED), (1.6, breathe), (3.2, RELAXED)], True)

    # Enemy at rest: hunched and lopsided, like its walk, swaying slightly.
    slump = merge(RELAXED, {"Spine": torso(24, 0, 8), "Spine1": torso(10, 0, 5), "Neck": torso(8),
                            "Head": torso(18, -12, 12), "LeftArm": arm("L", 82), "LeftForeArm": elbow("L", 8),
                            "RightArm": arm("R", 86, 34, 22), "RightForeArm": elbow("R", 62),
                            "RightUpLeg": thigh(3), "RightLeg": knee(10), "Hips": torso(0, 0, 4)})
    sway = merge(slump, {"Spine": torso(27, 5, 11), "Head": torso(20, -6, 14), "Hips": torso(0, 0, 6)})
    clips["EnemyIdle"] = ([(0.0, slump), (1.4, sway), (2.8, slump)], True)

    # Slow, guarded walk: short steps, arms barely swinging.
    def walk(phase):
        swing = math.sin(phase)
        left_lift = max(0.0, math.cos(phase))
        right_lift = max(0.0, -math.cos(phase))
        return merge(RELAXED, {
            "LeftUpLeg": thigh(20 * swing + 8 * left_lift), "RightUpLeg": thigh(-20 * swing + 8 * right_lift),
            "LeftLeg": knee(6 + 36 * left_lift), "RightLeg": knee(6 + 36 * right_lift),
            "LeftArm": arm("L", 76, -12 * swing), "RightArm": arm("R", 76, 12 * swing),
            "Spine": torso(6, 4 * swing), "Hips": torso(0, -3 * swing),
        })
    clips["WalkForward"] = ([(1.1 * i / steps, walk(2 * math.pi * i / steps)) for i in range(steps + 1)], True)

    # Two-handed overhead swing with a blunt weapon.
    ready = merge(RELAXED, {"RightArm": arm("R", 88, 40, 10), "RightForeArm": elbow("R", 85),
                            "LeftArm": arm("L", 88, 30, 20), "LeftForeArm": elbow("L", 75),
                            "Spine": torso(-3, -10)})
    wind = merge(RELAXED, {"RightArm": arm("R", 90, 160, 5), "RightForeArm": elbow("R", 75),
                           "LeftArm": arm("L", 90, 140, 20), "LeftForeArm": elbow("L", 85),
                           "Spine": torso(-14, -16), "Spine1": torso(-6, -6), "Head": torso(6)})
    hit = merge(RELAXED, {"RightArm": arm("R", 90, 42, 12), "RightForeArm": elbow("R", 14),
                          "LeftArm": arm("L", 90, 38, 22), "LeftForeArm": elbow("L", 22),
                          "Spine": torso(24, 14), "Spine1": torso(8, 6), "Head": torso(-10),
                          "LeftUpLeg": thigh(14), "LeftLeg": knee(18)})
    clips["MeleeSwing"] = ([(0.0, ready), (0.22, wind), (0.36, hit), (0.5, hit), (0.8, ready)], False)

    # Pistol held out in the right hand, left hand supporting. Layered on the upper body at runtime.
    aim = merge(RELAXED, {"RightArm": arm("R", 90, 86, 8), "RightForeArm": elbow("R", 5),
                          "LeftArm": arm("L", 90, 74, 26), "LeftForeArm": elbow("L", 38),
                          "Spine": torso(2, -8), "Head": torso(0, 6)})
    aim_breathe = merge(aim, {"Spine": torso(4, -8), "RightArm": arm("R", 90, 84, 8)})
    clips["PistolAimIdle"] = ([(0.0, aim), (1.0, aim_breathe), (2.0, aim)], True)
    recoil = merge(aim, {"RightArm": arm("R", 90, 100, 8), "RightForeArm": elbow("R", 24),
                         "LeftArm": arm("L", 90, 86, 26), "Spine": torso(-3, -8)})
    clips["PistolFire"] = ([(0.0, aim), (0.05, recoil), (0.3, aim)], False)

    # Staggered backwards by a blow.
    flinch = merge(RELAXED, {"Spine": torso(-20, 8, 6), "Spine1": torso(-10, 4), "Head": torso(-16, -10),
                             "LeftArm": arm("L", 55, -25), "RightArm": arm("R", 50, -30),
                             "LeftForeArm": elbow("L", 40), "RightForeArm": elbow("R", 40),
                             "RightUpLeg": thigh(-12), "RightLeg": knee(10)})
    clips["HitReact"] = ([(0.0, RELAXED), (0.1, flinch), (0.5, RELAXED)], False)

    # Reaching forward and down to pick something up or work a handle.
    reach = merge(RELAXED, {"RightArm": arm("R", 90, 62, 8), "RightForeArm": elbow("R", 22),
                            "Spine": torso(18, -6), "Head": torso(10)})
    clips["Interact"] = ([(0.0, RELAXED), (0.3, reach), (0.55, reach), (0.9, RELAXED)], False)

    # Enemy: a dragging, lopsided shamble; one arm hangs, the other is drawn up against the chest.
    def shamble(phase):
        swing = math.sin(phase)
        lift = max(0.0, math.cos(phase))
        return merge(RELAXED, {
            "Spine": torso(26, 8 * swing, 9), "Spine1": torso(10, 0, 5), "Neck": torso(8),
            "Head": torso(18, -14, 12),
            "LeftUpLeg": thigh(18 * swing + 6 * lift), "LeftLeg": knee(8 + 26 * lift),
            "RightUpLeg": thigh(-9 * swing + 3), "RightLeg": knee(10),
            "LeftArm": arm("L", 82, 6 * swing), "LeftForeArm": elbow("L", 8),
            "RightArm": arm("R", 86, 34, 22), "RightForeArm": elbow("R", 62),
            "Hips": torso(0, -5 * swing, 4),
        })
    clips["EnemyShamble"] = ([(1.5 * i / steps, shamble(2 * math.pi * i / steps)) for i in range(steps + 1)], True)

    # Enemy: rears back, then throws both arms forward in a lunge.
    rear = merge(RELAXED, {"Spine": torso(-10), "Head": torso(-8),
                           "LeftArm": arm("L", 70, -30), "RightArm": arm("R", 70, -30),
                           "LeftForeArm": elbow("L", 50), "RightForeArm": elbow("R", 50)})
    lunge = merge(RELAXED, {"Spine": torso(32), "Spine1": torso(12), "Head": torso(-22),
                            "LeftArm": arm("L", 90, 96, 10), "RightArm": arm("R", 90, 96, 10),
                            "LeftForeArm": elbow("L", 12), "RightForeArm": elbow("R", 12),
                            "LeftUpLeg": thigh(22), "LeftLeg": knee(24)})
    clips["EnemyAttack"] = ([(0.0, RELAXED), (0.3, rear), (0.45, lunge), (0.65, lunge), (1.0, RELAXED)], False)
    return clips


def render_preview(name, frames, preview_dir):
    """Workbench stills of the posed mannequin, for checking a clip by eye. Best effort."""
    scene = bpy.context.scene
    try:
        scene.render.engine = "BLENDER_WORKBENCH"
        scene.render.resolution_x = 300
        scene.render.resolution_y = 420
        scene.render.film_transparent = False
        camera_data = bpy.data.cameras.new("PreviewCam")
        camera = bpy.data.objects.new("PreviewCam", camera_data)
        scene.collection.objects.link(camera)
        # The character stands at the origin, about 1.8 m tall, facing -Y.
        camera.location = Vector((2.4, -3.4, 1.35))
        direction = Vector((0.0, 0.0, 0.95)) - camera.location
        camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        camera_data.lens = 38
        scene.camera = camera
        os.makedirs(preview_dir, exist_ok=True)
        for index, frame in enumerate(frames):
            scene.frame_set(frame)
            scene.render.filepath = os.path.join(os.path.abspath(preview_dir), f"{name}_{index}.png")
            bpy.ops.render.render(write_still=True)
            print("PREVIEW", scene.render.filepath)
        bpy.data.objects.remove(camera)
    except Exception as error:  # rendering is optional: never fail the export because of it
        print("PREVIEW FAILED", name, error)


def run_author(rig_path, out_dir, only, preview_dir):
    clips = clip_poses()
    for name, (keys, loop) in clips.items():
        if only and name != only:
            continue
        reset_scene()
        armature = import_fbx(rig_path)
        # The rig file carries the mesh too; it is not exported, but it is what the previews show.
        rig = Rig(armature)
        rig.new_action(name)
        last_frame = 1
        key_frames = []
        for time, pose in keys:
            frame = seconds(time)
            rig.key(frame, pose)
            key_frames.append(frame)
            last_frame = max(last_frame, frame)
        rig.finish(linear=loop)
        if preview_dir:
            render_preview(name, key_frames[:4], preview_dir)
        export_armature(armature, os.path.join(out_dir, name + ".fbx"), 1, last_frame)


def main():
    args = parse_args()
    if not args["out"]:
        raise SystemExit("--out is required")
    if args["mode"] == "roundtrip":
        run_roundtrip(args["source"], args["out"])
    else:
        run_author(args["rig"], args["out"], args["only"], args["preview"])


main()
