"""
LeonHorror - the clips of the two enemy kinds, the Patient and the Orderly.

Imported by make_animations.py (Blender runs that file, not this one); regenerate and import with

    python Games/LeonHorror/Scripts/import_animations.py --enemies

and look at the result on the characters' own meshes with review_enemy_clips.py.

How a clip is made
    A clip is a function of time that returns a pose in plain terms: where the hips are, how the
    spine bends, where each hand is relative to its shoulder, where each ankle is on the floor.
    `solve` turns that into bone transforms: the spine, the shoulders, the hands and the fingers by
    forward kinematics, the arms and the legs by two-bone inverse kinematics, so a planted foot
    stays where it was put whatever the hips do. `author` then keys every bone on every frame, so
    what is exported is plain FK and the engine needs to know nothing about any of it.

Space
    Metres, in Blender's world: the character stands on z = 0 at the origin, faces -Y, and its left
    is +X. Poses are written as (left, forward, up), or (out, forward, up) for a limb, `out` being
    away from the middle of the body on that limb's side. Angles are degrees: (pitch forward, yaw
    to the left, lean to the left).

What the game does with the clips (read in ULeonHorrorAnimInstance.cpp, ALeonHorrorEnemy.cpp and
the engine's FAnimRuntime.cpp, not guessed)
    * Idle and Walk are the samples of a blend space at 0 and at `WalkSpeedCm`; both are sampled
      with the same clock, looped over their own length, at their authored rate whatever the speed.
      The body is moved by the character movement, never by the clip, so the walks are in place and
      a planted foot travels backwards at WalkSpeedCm / (in-game height / mesh height).
    * Attack and Hit are one-shots over the whole body: 0.08 s in, 0.18 s out, played once from
      frame 0 the moment the windup starts; the blow is dealt `AttackWindup` seconds later.
    * Death is a state that does not loop: the last frame is held for as long as the body lies there.
    * Only the hips keep their animated translation; every other bone keeps its rest length.
"""

import math

from mathutils import Matrix, Quaternion, Vector

FPS = 30
B = "mixamorig:"

# In-game height / height of the cooked mesh (FLeonHorrorArchetypes.cpp, inspect_character.py).
PATIENT_SCALE = 1.72 / 1.802
ORDERLY_SCALE = 1.95 / 1.808


# --------------------------------------------------------------------------------------
# Small maths
# --------------------------------------------------------------------------------------
def V(left, forward, up):
    return Vector((left, -forward, up))


def Rx(deg):
    return Matrix.Rotation(math.radians(deg), 4, "X")


def Ry(deg):
    return Matrix.Rotation(math.radians(deg), 4, "Y")


def Rz(deg):
    return Matrix.Rotation(math.radians(deg), 4, "Z")


def R(pitch=0.0, yaw=0.0, lean=0.0):
    """Pitch forward, yaw to the left, lean to the left; the lean is applied first."""
    return Rz(yaw) @ Rx(pitch) @ Ry(lean)


def about(point, rotation):
    return Matrix.Translation(point) @ rotation @ Matrix.Translation(-point)


def smooth(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


def lerp(a, b, w):
    return a + (b - a) * w


def ring(t, start, freq, decay):
    """A struck spring: nothing before `start`, then a sine that dies away. Starts at speed."""
    dt = t - start
    if dt <= 0.0:
        return 0.0
    return math.exp(-decay * dt) * math.sin(2.0 * math.pi * freq * dt)


def bump(t, start, rise, fall):
    """0 -> 1 over `rise` seconds from `start`, then back to 0 over `fall`. Eased both ways."""
    if t <= start:
        return 0.0
    if t < start + rise:
        return smooth((t - start) / rise)
    return 1.0 - smooth((t - start - rise) / fall)


def hermite(v0, v1, m0, m1, s):
    s2, s3 = s * s, s * s * s
    return (2 * s3 - 3 * s2 + 1) * v0 + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * v1 + (s3 - s2) * m1


class Curve:
    """
    A value through keys, eased: a key that is a peak or a valley, or the first or the last, is come
    to and left at rest; a key on the way somewhere is passed through without stopping.

    modes[i] changes one key: "hit" arrives at speed and stops dead (a body meeting the floor),
    "go" leaves at speed (a body being struck), "lin" passes straight through.
    period makes the curve cyclic (the keys are then times within one period).
    """

    def __init__(self, times, values, modes=None, period=None):
        self.t = list(times)
        self.v = list(values)
        self.period = period
        n = len(self.t)
        modes = modes or [None] * n
        if period is not None:
            # One more key, a period later, so every segment is an ordinary one.
            self.t.append(self.t[0] + period)
            self.v.append(self.v[0])
            modes = list(modes) + [modes[0]]
        count = len(self.t)
        slope = [(self.v[i + 1] - self.v[i]) / max(self.t[i + 1] - self.t[i], 1e-9) for i in range(count - 1)]
        self.m_in = [0.0] * count
        self.m_out = [0.0] * count
        for i in range(count):
            if period is not None:
                before = slope[(i - 1) % (count - 1)]
                after = slope[i % (count - 1)]
                span = (self.t[i % (count - 1) + 1] - self.t[i % (count - 1)]) + \
                       (self.t[(i - 1) % (count - 1) + 1] - self.t[(i - 1) % (count - 1)])
                through = (after * (self.t[i % (count - 1) + 1] - self.t[i % (count - 1)]) +
                           before * (self.t[(i - 1) % (count - 1) + 1] - self.t[(i - 1) % (count - 1)])) / span
            elif i == 0 or i == count - 1:
                before = slope[i - 1] if i > 0 else 0.0
                after = slope[i] if i < count - 1 else 0.0
                through = 0.0
            else:
                before, after = slope[i - 1], slope[i]
                through = (self.v[i + 1] - self.v[i - 1]) / (self.t[i + 1] - self.t[i - 1])
            if before * after <= 0.0:
                through = 0.0
            else:
                limit = 3.0 * min(abs(before), abs(after))
                through = max(-limit, min(limit, through))
            mode = modes[i]
            if mode == "hit":
                self.m_in[i], self.m_out[i] = 2.0 * before, 0.0
            elif mode == "go":
                self.m_in[i], self.m_out[i] = 0.0, 2.0 * after
            elif mode == "lin":
                self.m_in[i], self.m_out[i] = before, after
            else:
                self.m_in[i] = self.m_out[i] = through

    def __call__(self, t):
        if self.period is not None:
            t = self.t[0] + (t - self.t[0]) % self.period
        if t <= self.t[0]:
            return self.v[0]
        if t >= self.t[-1]:
            return self.v[-1]
        for i in range(len(self.t) - 1):
            if t <= self.t[i + 1]:
                span = self.t[i + 1] - self.t[i]
                s = (t - self.t[i]) / span
                return hermite(self.v[i], self.v[i + 1], self.m_out[i] * span, self.m_in[i + 1] * span, s)
        return self.v[-1]


def cyc(period, *keys):
    """A cyclic curve from (time, value) pairs."""
    return Curve([k[0] for k in keys], [k[1] for k in keys], period=period)


class Seq:
    """
    A pose through key poses. Each key is (time, overrides) or (time, overrides, mode): the base
    pose with some of its entries replaced. Every number is run through its own Curve.
    """

    def __init__(self, base, keys):
        self.base = base
        times = [k[0] for k in keys]
        modes = [k[2] if len(k) > 2 else None for k in keys]
        self.end = times[-1]
        self.curves = {}
        for name, rest in base.items():
            values = [k[1].get(name, rest) for k in keys]
            if isinstance(rest, tuple):
                self.curves[name] = [Curve(times, [v[c] for v in values], modes) for c in range(len(rest))]
            else:
                self.curves[name] = Curve(times, values, modes)

    def at(self, t, lags=None):
        """lags: {name: seconds} - those entries are read that much earlier, so they trail."""
        out = {}
        for name, curve in self.curves.items():
            when = max(0.0, t - lags[name]) if lags and name in lags else t
            if isinstance(curve, list):
                out[name] = tuple(c(when) for c in curve)
            else:
                out[name] = curve(when)
        return out


def mix(base, a, b, w):
    """A key pose between two others (each the base with some entries replaced)."""
    out = {}
    for name, rest in base.items():
        va, vb = a.get(name, rest), b.get(name, rest)
        out[name] = tuple(lerp(x, y, w) for x, y in zip(va, vb)) if isinstance(rest, tuple) else lerp(va, vb, w)
    return out


def add(pose, name, *delta):
    value = pose[name]
    if isinstance(value, tuple):
        pose[name] = tuple(a + b for a, b in zip(value, delta))
    else:
        pose[name] = value + delta[0]


# --------------------------------------------------------------------------------------
# The skeleton and the solver
# --------------------------------------------------------------------------------------
FINGERS = ("Index", "Middle", "Ring", "Pinky")
FINGER_CURL = (42.0, 52.0, 38.0)

# Rest positions the pose helpers need before a skeleton is at hand (measured on Raw/YBot.fbx).
ANKLE = Vector((0.091, 0.026, 0.105))
BALL_FROM_ANKLE = Vector((0.004, -0.139, -0.072))
HEEL_FROM_ANKLE = Vector((0.0, 0.055, -0.105))
THIGH, SHIN = 0.406, 0.420
HIPS_ABOVE_JOINTS = 0.067  # the hips bone sits this far above the line through the two hip joints


class Skeleton:
    def __init__(self, armature):
        world = armature.matrix_world
        self.names, self.parent, self.rest, self.rest_inv, self.head = [], {}, {}, {}, {}
        for bone in armature.data.bones:  # parents come before their children
            short = bone.name[len(B):] if bone.name.startswith(B) else bone.name
            self.names.append(short)
            self.parent[short] = (bone.parent.name[len(B):] if bone.parent.name.startswith(B) else bone.parent.name) if bone.parent else None
            self.rest[short] = world @ bone.matrix_local
            self.rest_inv[short] = self.rest[short].inverted()
            self.head[short] = world @ bone.head_local


def two_bone(root, target, pole, head_upper, head_lower, head_end, rest_pole):
    """
    Two-bone IK. Returns the world transforms (relative to rest) of the upper and the lower bone,
    where the end really is (the target, pulled in if it was out of reach) and how stretched the
    limb is (1 = straight).
    """
    upper = (head_lower - head_upper).length
    lower = (head_end - head_lower).length
    reach = target - root
    wanted = reach.length
    distance = min(max(wanted, abs(upper - lower) + 1e-4), (upper + lower) * 0.9995)
    along = reach.normalized() if wanted > 1e-6 else Vector((0.0, 0.0, -1.0))
    side = pole - along * pole.dot(along)
    if side.length < 1e-5:
        side = rest_pole - along * rest_pole.dot(along)
    side.normalize()
    cos_a = (upper * upper + distance * distance - lower * lower) / (2.0 * upper * distance)
    sin_a = math.sqrt(max(0.0, 1.0 - cos_a * cos_a))
    joint = root + (along * cos_a + side * sin_a) * upper
    end = root + along * distance
    normal = along.cross(side).normalized()

    def frame(direction, plane_normal):
        x = direction.normalized()
        z = x.cross(plane_normal).normalized()
        y = z.cross(x)
        return Matrix((x, y, z)).transposed()

    def turn(rest_direction, direction):
        rest_normal = rest_direction.cross(rest_pole)
        return (frame(direction, normal) @ frame(rest_direction, rest_normal).transposed()).to_4x4()

    d_upper = Matrix.Translation(root) @ turn(head_lower - head_upper, joint - root) @ Matrix.Translation(-head_upper)
    d_lower = Matrix.Translation(joint) @ turn(head_end - head_lower, end - joint) @ Matrix.Translation(-head_lower)
    return d_upper, d_lower, end, wanted / (upper + lower)


def _blend_local(parent, d_ik, d_fk_local_parent, d_fk, joint, weight):
    """Between two answers for one bone, by the rotation each makes relative to its own parent."""
    q_ik = (parent.inverted() @ d_ik).to_quaternion()
    q_fk = (d_fk_local_parent.inverted() @ d_fk).to_quaternion()
    return q_ik.slerp(q_fk, weight).to_matrix().to_4x4(), joint


def solve(sk, p, stats=None):
    """The world transform of every bone, relative to its rest, for the pose `p`."""
    D = {}
    h = sk.head
    hp = p["hips_pos"]
    D["Hips"] = Matrix.Translation(h["Hips"] + V(hp[0], hp[1], hp[2])) @ R(*p["hips_rot"]) @ Matrix.Translation(-h["Hips"])
    parent = "Hips"
    for name in ("Spine", "Spine1", "Spine2", "Neck", "Head"):
        D[name] = D[parent] @ about(h[name], R(*p[name.lower()]))
        parent = name
    chest = D["Spine2"].to_quaternion()

    for side, s, pre in (("Left", 1.0, "l_"), ("Right", -1.0, "r_")):
        # ---- arm
        lift, forward = p[pre + "clav"]
        clav = side + "Shoulder"
        D[clav] = D["Spine2"] @ about(h[clav], Rz(-s * forward) @ Ry(-s * lift))
        root = D[clav] @ h[side + "Arm"]
        follow = Quaternion().slerp(chest, min(1.0, max(0.0, p[pre + "arm_follow"])))
        out, fwd, up = p[pre + "hand"]
        target = root + follow @ V(s * out, fwd, up)
        weight, floor = p[pre + "hand_z"]
        target.z += (floor - target.z) * weight
        out, fwd, up = p[pre + "elbow"]
        pole = follow @ V(s * out, fwd, up)
        d_upper, d_lower, _end, stretch = two_bone(root, target, pole, h[side + "Arm"], h[side + "ForeArm"], h[side + "Hand"],
                                                   Vector((0.0, 1.0, 0.0)))
        D[side + "Arm"], D[side + "ForeArm"] = d_upper, d_lower
        if stats is not None:
            stats["arm"] = max(stats.get("arm", 0.0), stretch)
        flex, sideways, twist = p[pre + "wrist"]
        hand = side + "Hand"
        D[hand] = d_lower @ about(h[hand], Ry(s * flex) @ Rz(s * sideways) @ Rx(twist))
        curl = p[pre + "fingers"]
        for finger in FINGERS:
            above = hand
            for index, angle in enumerate(FINGER_CURL):
                bone = "%sHand%s%d" % (side, finger, index + 1)
                D[bone] = D[above] @ about(h[bone], Ry(s * curl * angle))
                above = bone
        # The thumb folds down towards the palm at its root, then closes over the fingers.
        above = hand
        for index, turn in enumerate((Rx(curl * 38.0) @ Rz(s * curl * 12.0), Rz(s * curl * 26.0), Rz(s * curl * 30.0))):
            bone = "%sHandThumb%d" % (side, index + 1)
            D[bone] = D[above] @ about(h[bone], turn)
            above = bone

        # ---- leg
        up_leg, leg, foot, toe = side + "UpLeg", side + "Leg", side + "Foot", side + "ToeBase"
        root = D["Hips"] @ h[up_leg]
        out, fwd, up = p[pre + "ankle"]
        target = h[foot] + V(s * out, fwd, up)
        pitch, yaw, roll = p[pre + "foot"]
        out, fwd, up = p[pre + "knee"]
        d_upper, d_lower, end, stretch = two_bone(root, target, V(s * out, fwd, up), h[up_leg], h[leg], h[foot],
                                                  Vector((0.0, -1.0, 0.0)))
        d_foot = Matrix.Translation(end) @ Rz(s * yaw) @ Rx(pitch) @ Ry(s * roll) @ Matrix.Translation(-h[foot])
        free = p[pre + "leg_fk"]
        if stats is not None and free < 0.5:
            stats["leg"] = max(stats.get("leg", 0.0), stretch)
        if free > 0.0:
            flex, abduct, knee, foot_pitch = p[pre + "leg"]
            f_upper = D["Hips"] @ about(h[up_leg], Ry(-s * abduct) @ Rx(-flex))
            f_lower = f_upper @ about(h[leg], Rx(knee))
            f_foot = f_lower @ about(h[foot], Rx(foot_pitch))
            q, _ = _blend_local(D["Hips"], d_upper, D["Hips"], f_upper, h[up_leg], free)
            n_upper = D["Hips"] @ about(h[up_leg], q)
            q, _ = _blend_local(d_upper, d_lower, f_upper, f_lower, h[leg], free)
            n_lower = n_upper @ about(h[leg], q)
            q, _ = _blend_local(d_lower, d_foot, f_lower, f_foot, h[foot], free)
            d_foot = n_lower @ about(h[foot], q)
            d_upper, d_lower = n_upper, n_lower
        D[up_leg], D[leg], D[foot] = d_upper, d_lower, d_foot
        D[toe] = d_foot @ about(h[toe], Rx(-p[pre + "toe"]))

    for name in sk.names:
        if name not in D:
            D[name] = D[sk.parent[name]]
    return D


def plant(side, out=0.0, forward=0.0, lift=0.0, pitch=0.0, yaw=0.0):
    """
    The ankle and the foot for a foot on the floor. (out, forward) is where the ankle would be with
    the foot flat, from its rest place. pitch > 0 raises the heel about the ball of the foot,
    pitch < 0 raises the toes about the heel; yaw turns the toes out; lift raises the lot.
    """
    s = 1.0 if side == "l" else -1.0
    turn = Rz(s * yaw).to_3x3()
    pivot_offset = Vector((s * BALL_FROM_ANKLE.x, BALL_FROM_ANKLE.y, BALL_FROM_ANKLE.z)) if pitch >= 0.0 else HEEL_FROM_ANKLE
    rest = Vector((s * ANKLE.x, ANKLE.y, ANKLE.z))
    spot = rest + Vector((s * out, -forward, 0.0))
    pivot = spot + turn @ pivot_offset
    ankle = pivot + turn @ (Rx(pitch).to_3x3() @ (-pivot_offset))
    return {
        side + "_ankle": (s * (ankle.x - rest.x), -(ankle.y - rest.y), ankle.z - rest.z + lift),
        side + "_foot": (pitch, yaw, 0.0),
        side + "_toe": max(pitch, 0.0) * 0.9,
    }


def torso(pitch, yaw=0.0, lean=0.0):
    """A bend of the whole back, shared out between its three bones."""
    return {"spine": (pitch * 0.42, yaw * 0.30, lean * 0.40),
            "spine1": (pitch * 0.33, yaw * 0.35, lean * 0.35),
            "spine2": (pitch * 0.25, yaw * 0.35, lean * 0.25)}


def hips_over_knees(knee_forward, knee_up, thigh_angle, hips_pitch):
    """
    hips_pos for a kneeling body: the knees on the floor at (knee_forward, knee_up) in the world,
    the thighs `thigh_angle` degrees forward of upright, the pelvis pitched `hips_pitch`.
    """
    a, b = math.radians(thigh_angle), math.radians(hips_pitch)
    forward = knee_forward + THIGH * math.sin(a) + HIPS_ABOVE_JOINTS * math.sin(b)
    up = knee_up + THIGH * math.cos(a) + HIPS_ABOVE_JOINTS * math.cos(b)
    return forward, up - 0.998


def gait(u, stance, front, back, lift=0.06, toe_up=12.0, heel_up=30.0, drag=0.0):
    """
    One foot of a walk in place, at phase u (0 = the foot lands). While it bears weight (u < stance)
    it travels from `front` to `back` at a steady speed, which is what keeps it from sliding when
    the game moves the body at that speed. Returns (forward, lift, pitch).
    drag > 0: the foot is not lifted in the swing but pulled along on its toes, pitched `drag`.
    """
    if u < stance:
        s = u / stance
        forward = lerp(front, back, s)
        if s < 0.16:
            pitch = -toe_up * (1.0 - smooth(s / 0.16))
        else:
            pitch = heel_up * smooth((s - 0.55) / 0.45) ** 1.3
        return forward, 0.0, pitch
    w = (u - stance) / (1.0 - stance)
    speed = (back - front) / stance * (1.0 - stance)  # the stance speed, per unit of w
    forward = hermite(back, front, speed, speed, w)
    if drag > 0.0:
        pitch = lerp(heel_up, drag, smooth(w / 0.25)) if w < 0.25 else lerp(drag, -toe_up, smooth((w - 0.72) / 0.28))
        return forward, 0.0, pitch
    pitch = lerp(heel_up, -toe_up, smooth(w / 0.85))
    return forward, lift * math.sin(math.pi * w ** 0.85) ** 2, pitch


# --------------------------------------------------------------------------------------
# The Patient: thin, quick, broken. Its left arm is the bandaged one; its right leg drags.
# --------------------------------------------------------------------------------------
P_BASE = {
    "hips_pos": (0.025, 0.0, -0.035), "hips_rot": (4.0, -4.0, -3.0),
    **torso(24.0, -6.0, 5.0),
    "neck": (10.0, 0.0, -6.0), "head": (6.0, 8.0, -14.0),
    "l_clav": (8.0, 5.0), "r_clav": (-4.0, 4.0),
    # The bandaged arm: held up and out to the side, well clear of the gown.
    "l_hand": (0.20, 0.29, -0.24), "l_arm_follow": 1.0, "l_elbow": (1.0, -0.3, -0.6), "l_hand_z": (0.0, 0.0),
    "l_wrist": (35.0, 0.0, 0.0), "l_fingers": 0.7,
    # The other one hangs.
    "r_hand": (0.13, 0.08, -0.51), "r_arm_follow": 0.0, "r_elbow": (0.3, -1.0, 0.0), "r_hand_z": (0.0, 0.0),
    "r_wrist": (8.0, 0.0, 0.0), "r_fingers": 0.3,
    **plant("l", 0.0, 0.0, yaw=6.0), "l_knee": (0.12, 1.0, 0.0), "l_leg_fk": 0.0, "l_leg": (0.0, 0.0, 0.0, 0.0),
    **plant("r", 0.03, -0.06, yaw=22.0), "r_knee": (0.4, 1.0, 0.0), "r_leg_fk": 0.0, "r_leg": (0.0, 0.0, 0.0, 0.0),
}


def patient_idle(t):
    period = 6.0
    w = 2.0 * math.pi * t / period
    p = dict(P_BASE)
    sway = 0.018 * math.sin(w) + 0.006 * math.sin(2.0 * w + 1.0)
    breath = math.sin(4.0 * w)
    twitch = ring(t, 1.7, 8.0, 9.0)       # the head jerks
    spasm = ring(t, 3.9, 5.5, 5.0)        # the back snaps straight and shudders
    spasm_arm = ring(t, 3.95, 5.5, 4.5)   # the bandaged arm follows it
    spasm_head = ring(t, 4.0, 5.5, 4.5)   # and the head after that
    tic = ring(t, 5.0, 7.0, 8.0)
    p["hips_pos"] = (0.025 + sway, 0.004 * math.sin(2.0 * w), -0.035 - 0.006 * math.cos(2.0 * w) + 0.012 * spasm)
    p["hips_rot"] = (4.0, -4.0 + 1.5 * math.sin(w + 0.6), -3.0 - sway * 70.0)
    p.update(torso(24.0 + 1.2 * breath - 11.0 * spasm, -6.0 + 2.0 * math.sin(w + 2.0) + 2.0 * twitch, 5.0 + sway * 120.0))
    loll = math.sin(w - 0.6)
    p["neck"] = (10.0 + 3.0 * math.sin(2.0 * w + 0.5) - 6.0 * spasm_head, 4.0 * twitch, -6.0 + 4.0 * loll)
    p["head"] = (6.0 + 5.0 * math.sin(2.0 * w + 0.2) - 13.0 * spasm_head, 8.0 + 14.0 * twitch + 6.0 * math.sin(w + 1.2),
                 -14.0 + 9.0 * math.sin(w - 0.9) + 7.0 * tic)
    p["l_clav"] = (8.0 + 1.5 * breath + 7.0 * twitch + 6.0 * spasm, 5.0)
    p["r_clav"] = (-4.0 + 1.5 * breath, 4.0)
    p["l_hand"] = (0.20 + 0.03 * spasm_arm, 0.29 - 0.02 * spasm_arm, -0.24 + 0.008 * math.sin(4.0 * w - 0.6) + 0.07 * spasm_arm)
    p["l_fingers"] = 0.7 + 0.12 * math.sin(3.0 * w) - 0.6 * bump(t, 3.9, 0.07, 0.5)
    p["l_wrist"] = (35.0 - 25.0 * spasm_arm, 0.0, 0.0)
    p["r_hand"] = (0.13, 0.08 + 0.012 * math.sin(2.0 * w - 1.0) + 0.035 * ring(t, 4.02, 3.0, 4.0), -0.51)
    p["r_fingers"] = 0.3 + 0.4 * abs(tic) + 0.3 * bump(t, 3.9, 0.1, 0.6)
    return p


PATIENT_WALK_PERIOD = 0.8
PATIENT_WALK_SPEED = 1.30 / PATIENT_SCALE  # metres a second on the uncooked rig
_P_UP = cyc(1.0, (0.0, -0.082), (0.08, -0.090), (0.32, -0.036), (0.55, -0.052), (0.78, -0.042))


def patient_walk(t):
    """A dragging limp. The left leg steps; the right is pulled after it on its toes, turned out."""
    period, speed = PATIENT_WALK_PERIOD, PATIENT_WALK_SPEED
    u = (t / period) % 1.0
    w = 2.0 * math.pi * u
    p = dict(P_BASE)

    left_stance, right_stance = 0.60, 0.50
    left_front = 0.335
    right_front = 0.20
    fwd, lift, pitch = gait(u, left_stance, left_front, left_front - speed * left_stance * period, lift=0.075, toe_up=12.0, heel_up=32.0)
    p.update(plant("l", 0.0, fwd, lift, pitch, yaw=6.0))
    fwd, lift, pitch = gait((u - 0.55) % 1.0, right_stance, right_front, right_front - speed * right_stance * period,
                            toe_up=0.0, heel_up=26.0, drag=30.0)
    p.update(plant("r", 0.04, fwd, lift, pitch, yaw=24.0))

    # The body drops onto the good leg as it lands and is hauled up over it.
    p["hips_pos"] = (0.032 * math.sin(w - 0.31), 0.02 + 0.012 * math.sin(w - 0.6), _P_UP(u))
    p["hips_rot"] = (6.0, -6.0 * math.cos(w), -3.0 * math.sin(w - 0.31))
    lurch = math.cos(w - 0.65)
    p.update(torso(25.0 + 5.0 * lurch, -6.0 + 5.0 * math.cos(w - 0.2), 4.0 + 4.0 * math.sin(w - 0.5)))
    add(p, "spine2", 2.0 * math.cos(w - 1.0), 0.0, 0.0)
    p["neck"] = (10.0 + 3.0 * math.cos(w - 1.2), 0.0, -6.0 + 2.0 * math.sin(w - 1.4))
    p["head"] = (7.0 + 6.0 * math.cos(w - 1.6), 7.0 + 3.0 * math.cos(w - 0.6), -14.0 + 6.0 * math.sin(w - 1.9))
    # The hanging arm swings late, like a rope; the bandaged one is carried, and bounces a little.
    p["r_hand"] = (0.14, 0.09 + 0.09 * math.cos(w - 0.9), -0.50 + 0.012 * math.cos(2.0 * w - 1.8))
    p["r_wrist"] = (8.0 + 10.0 * math.cos(w - 1.6), 0.0, 0.0)
    p["r_clav"] = (-4.0 + 2.0 * math.cos(w - 0.5), 4.0)
    p["l_hand"] = (0.20 + 0.012 * math.sin(w - 1.2), 0.29 + 0.02 * math.cos(w - 1.3), -0.24 + 0.022 * math.cos(w - 1.25))
    p["l_wrist"] = (35.0 + 9.0 * math.cos(w - 1.9), 0.0, 0.0)
    p["l_clav"] = (9.0 + 2.5 * math.cos(w - 0.9), 5.0)
    p["l_fingers"] = 0.7 + 0.08 * math.cos(w - 2.2)
    return p


_P_ATTACK = Seq(P_BASE, [
    (0.0, {}),
    # Coils to its right, the free hand drawn up and back.
    (0.13, {"hips_pos": (0.0, -0.05, -0.02), "hips_rot": (0.0, -22.0, -3.0), **torso(12.0, -30.0, 2.0),
            "neck": (4.0, 10.0, -6.0), "head": (0.0, 22.0, -20.0),
            "r_hand": (0.30, -0.10, 0.25), "r_arm_follow": 1.0, "r_elbow": (1.0, -0.3, 0.3), "r_fingers": 0.2, "r_wrist": (-20.0, 0.0, 0.0),
            "r_clav": (12.0, -10.0),
            "l_hand": (0.20, 0.25, -0.20),
            **plant("r", 0.03, -0.06, yaw=22.0, pitch=6.0)}),
    (0.24, {"hips_pos": (0.0, 0.08, -0.07), "hips_rot": (8.0, -6.0, 0.0), **torso(26.0, -8.0, 0.0),
            "neck": (0.0, 0.0, -4.0), "head": (-12.0, 4.0, -8.0),
            "r_hand": (0.26, 0.26, 0.34), "r_arm_follow": 1.0, "r_elbow": (1.0, -0.2, 0.5), "r_fingers": 0.45, "r_wrist": (-25.0, 0.0, 0.0),
            "r_clav": (10.0, 6.0),
            "l_hand": (0.26, 0.10, -0.32),
            **plant("l", 0.0, 0.17, lift=0.06, pitch=-6.0, yaw=6.0), **plant("r", 0.03, -0.06, yaw=22.0, pitch=18.0)}),
    # The blow: the hand is as far forward as it gets, still travelling across.
    (0.35, {"hips_pos": (0.0, 0.20, -0.13), "hips_rot": (14.0, 16.0, 0.0), **torso(40.0, 20.0, -2.0),
            "neck": (-6.0, -6.0, 0.0), "head": (-20.0, -10.0, 4.0),
            "r_hand": (0.0, 0.46, 0.25), "r_arm_follow": 1.0, "r_elbow": (0.8, -0.2, 0.6), "r_fingers": 0.55, "r_wrist": (-15.0, 0.0, 0.0),
            "r_clav": (4.0, 22.0),
            "l_hand": (0.30, 0.0, -0.40), "l_fingers": 0.3,
            **plant("l", 0.0, 0.30, yaw=6.0), **plant("r", 0.03, -0.06, yaw=22.0, pitch=30.0)}, "lin"),
    (0.44, {"hips_pos": (0.0, 0.24, -0.15), "hips_rot": (16.0, 24.0, 0.0), **torso(46.0, 34.0, -4.0),
            "neck": (-4.0, -8.0, 0.0), "head": (-14.0, -16.0, 6.0),
            "r_hand": (-0.28, 0.40, 0.0), "r_arm_follow": 1.0, "r_elbow": (0.6, -0.4, 0.5), "r_fingers": 0.7, "r_wrist": (10.0, 0.0, 0.0),
            "r_clav": (0.0, 26.0),
            "l_hand": (0.32, -0.04, -0.38), "l_fingers": 0.3,
            **plant("l", 0.0, 0.30, yaw=6.0), **plant("r", 0.03, -0.06, yaw=22.0, pitch=32.0)}),
    (0.58, {"hips_pos": (0.0, 0.19, -0.12), "hips_rot": (12.0, 16.0, -2.0), **torso(42.0, 22.0, 0.0),
            "neck": (2.0, -4.0, -3.0), "head": (-4.0, -6.0, -6.0),
            "r_hand": (-0.04, 0.46, -0.16), "r_arm_follow": 1.0, "r_elbow": (0.6, -0.5, 0.3), "r_fingers": 0.6,
            "r_clav": (-2.0, 16.0),
            "l_hand": (0.26, 0.02, -0.24),
            **plant("l", 0.0, 0.30, yaw=6.0), **plant("r", 0.03, -0.06, yaw=22.0, pitch=24.0)}),
    (0.74, {"hips_pos": (0.015, 0.07, -0.06), "hips_rot": (6.0, 2.0, -3.0), **torso(30.0, 2.0, 4.0),
            "r_hand": (0.10, 0.22, -0.43), "r_arm_follow": 0.5,
            **plant("l", 0.0, 0.14, lift=0.045, yaw=6.0), **plant("r", 0.03, -0.06, yaw=22.0, pitch=8.0)}),
    (0.9, {}),
])


def patient_attack(t):
    """Coils, then lunges onto the left foot and rakes the free hand across. The blow is at 0.35 s."""
    return _P_ATTACK.at(t, {"spine1": 0.012, "spine2": 0.024, "neck": 0.04, "head": 0.055,
                            "l_hand": 0.04, "l_fingers": 0.06, "r_wrist": 0.02, "r_fingers": 0.03})


_P_HIT = Seq(P_BASE, [
    (0.0, {}, "go"),
    (0.09, {"hips_pos": (0.01, -0.09, -0.055), "hips_rot": (-4.0, 6.0, -2.0), **torso(-14.0, 18.0, 0.0),
            "neck": (-10.0, 6.0, 4.0), "head": (-26.0, 14.0, 10.0),
            "l_clav": (16.0, 0.0), "r_clav": (8.0, -6.0),
            "r_hand": (0.20, 0.25, -0.38), "r_fingers": 0.0,
            "l_hand": (0.28, 0.30, -0.12), "l_fingers": 0.1,
            **plant("r", 0.03, -0.14, lift=0.03, yaw=22.0)}),
    (0.22, {"hips_pos": (0.0, -0.13, -0.07), "hips_rot": (2.0, 4.0, -3.0), **torso(4.0, 10.0, 2.0),
            "neck": (4.0, 0.0, -4.0), "head": (6.0, 2.0, -6.0),
            "r_hand": (0.16, 0.12, -0.46), "r_fingers": 0.15,
            "l_hand": (0.24, 0.26, -0.20),
            **plant("r", 0.03, -0.30, yaw=22.0)}),
    (0.42, {"hips_pos": (0.0, -0.10, -0.06), "hips_rot": (5.0, -2.0, -3.0), **torso(31.0, -2.0, 6.0),
            "neck": (12.0, 0.0, -8.0), "head": (14.0, 6.0, -19.0),
            "r_hand": (0.08, -0.02, -0.53),
            **plant("r", 0.03, -0.30, yaw=22.0, pitch=6.0)}),
    (0.6, {"hips_pos": (0.015, -0.04, -0.04), **torso(26.0, -5.0, 5.0),
           **plant("r", 0.03, -0.17, lift=0.035, yaw=22.0)}),
    (0.8, {}),
])


def patient_hit(t):
    """Thrown back from the chest: the head whips after it, a foot catches the fall, it folds back down."""
    return _P_HIT.at(t, {"spine1": 0.012, "spine2": 0.024, "neck": 0.04, "head": 0.06,
                         "r_hand": 0.05, "l_hand": 0.035, "r_fingers": 0.07, "l_fingers": 0.06})


# Kneeling, for the death: both feet pulled back onto their toes, the knees on the floor.
_P_KNEEL_FOOT = -0.22
_P_KNEE_UP = 0.06


def _patient_kneel(thigh_angle, hips_pitch):
    ankle = plant("l", 0.0, _P_KNEEL_FOOT, pitch=60.0)["l_ankle"]
    knee_forward = -ANKLE.y + ankle[1] + math.sqrt(SHIN ** 2 - (ANKLE.z + ankle[2] - _P_KNEE_UP) ** 2)
    forward, up = hips_over_knees(knee_forward, _P_KNEE_UP, thigh_angle, hips_pitch)
    return forward, up


_PK = _patient_kneel(-15.0, 5.0)
# On its right side, half turned onto its face, the legs folded in front of it. The body lies along
# X with the head towards -X, so for the feet `out` is along the body and `forward` across it.
_P_LYING = {
    "hips_pos": (-0.34, 0.20, -0.998 + 0.215), "hips_rot": (24.0, 8.0, -90.0), **torso(26.0, 0.0, 6.0),
    "neck": (10.0, 0.0, -2.0), "head": (10.0, 0.0, -4.0),
    "r_hand": (-0.22, 0.46, 0.0), "r_hand_z": (1.0, 0.11), "r_elbow": (1.0, 0.3, 0.2), "r_fingers": 0.25, "r_wrist": (0.0, 0.0, 0.0),
    "l_hand": (0.36, 0.28, 0.0), "l_hand_z": (1.0, 0.21), "l_arm_follow": 0.0, "l_elbow": (0.2, -0.2, 1.0),
    "l_wrist": (0.0, 0.0, 0.0), "l_fingers": 0.5,
    "l_clav": (0.0, 14.0), "r_clav": (6.0, 8.0),
    "r_ankle": (-0.39, 0.076, -0.045), "r_foot": (0.0, 0.0, 90.0), "r_toe": 0.0, "r_knee": (0.0, 1.0, 0.0),
    "l_ankle": (0.06, 0.446, -0.03), "l_foot": (0.0, 0.0, -90.0), "l_toe": 0.0, "l_knee": (0.0, 1.0, -0.3),
}
_P_SWAY = {"hips_pos": (-0.05, _PK[0] - 0.02, _PK[1] - 0.01), "hips_rot": (6.0, 0.0, -14.0), **torso(38.0, -6.0, -14.0),
           "neck": (18.0, 0.0, -10.0), "head": (26.0, 0.0, -18.0),
           "r_hand": (0.14, 0.10, -0.50), "l_hand": (0.10, 0.16, -0.50), "l_arm_follow": 0.0, "l_wrist": (10.0, 0.0, 0.0),
           "l_clav": (-4.0, 6.0), "r_clav": (-6.0, 6.0),
           **plant("l", 0.0, _P_KNEEL_FOOT, pitch=60.0, yaw=4.0), **plant("r", 0.03, _P_KNEEL_FOOT, pitch=60.0, yaw=14.0),
           "l_knee": (0.1, 1.0, -0.6), "r_knee": (0.2, 1.0, -0.6)}
_P_TIPPING = {**mix(P_BASE, _P_SWAY, _P_LYING, 0.36), "l_knee": (0.0, 1.0, 0.6), "r_knee": (0.0, 1.0, 0.8),
              "r_hand_z": (1.0, 0.22), "l_hand_z": (1.0, 0.45)}
_P_DEATH = Seq(P_BASE, [
    (0.0, {}, "go"),
    # Struck: the back arches, the arms fly.
    (0.14, {"hips_pos": (0.02, -0.04, -0.03), "hips_rot": (-2.0, 2.0, -2.0), **torso(2.0, 8.0, 0.0),
            "neck": (-10.0, 0.0, 6.0), "head": (-24.0, 10.0, 12.0),
            "r_hand": (0.18, 0.16, -0.42), "r_fingers": 0.0, "l_hand": (0.30, 0.22, -0.10), "l_fingers": 0.0,
            "l_clav": (16.0, 0.0), "r_clav": (10.0, 0.0)}),
    # The legs give: it drops onto its knees, the feet dragged back under it.
    (0.52, {"hips_pos": (0.0, _PK[0], _PK[1]), "hips_rot": (5.0, 0.0, 0.0), **torso(34.0, -4.0, -6.0),
            "neck": (16.0, 0.0, -8.0), "head": (24.0, 4.0, -16.0),
            "r_hand": (0.10, 0.10, -0.53), "l_hand": (0.16, 0.14, -0.50), "l_arm_follow": 0.0, "l_wrist": (10.0, 0.0, 0.0),
            "l_clav": (-4.0, 6.0), "r_clav": (-6.0, 6.0),
            **plant("l", 0.0, _P_KNEEL_FOOT, pitch=60.0, yaw=4.0), **plant("r", 0.03, _P_KNEEL_FOOT, pitch=60.0, yaw=14.0),
            "l_knee": (0.1, 1.0, -0.6), "r_knee": (0.2, 1.0, -0.6)}, "hit"),
    # Sways there, then tips over to its right; half way down the knees are kept off the floor.
    (0.80, _P_SWAY),
    (1.03, _P_TIPPING, "lin"),
    # On its side, half turned onto its face, the legs folded.
    (1.22, _P_LYING, "hit"),
    (1.9, _P_LYING),
])


def patient_death(t):
    """Arches, drops to its knees, tips onto its right side and lies there."""
    p = _P_DEATH.at(t, {"spine1": 0.012, "spine2": 0.024, "neck": 0.045, "head": 0.07,
                        "l_hand": 0.06, "r_hand": 0.04, "l_hand_z": 0.06, "r_hand_z": 0.04,
                        "l_ankle": 0.04, "l_foot": 0.04, "l_fingers": 0.1, "r_fingers": 0.1})
    # What the floor gives back: the knees, then the shoulder.
    knees = ring(t, 0.52, 4.0, 9.0)
    floor = ring(t, 1.22, 4.5, 8.0)
    add(p, "spine", 5.0 * knees + 2.0 * floor, 0.0, 0.0)
    add(p, "head", 9.0 * ring(t, 0.57, 4.0, 8.0), 0.0, -6.0 * ring(t, 1.27, 4.5, 7.0))
    add(p, "neck", 4.0 * ring(t, 0.55, 4.0, 8.0), 0.0, -3.0 * ring(t, 1.25, 4.5, 7.0))
    return p


# --------------------------------------------------------------------------------------
# The Orderly: big and slow, a thick trunk under a heavy apron. Everything is held clear of it.
# --------------------------------------------------------------------------------------
_O_FOOT = dict(out=0.08, yaw=14.0)
O_BASE = {
    "hips_pos": (0.0, 0.0, -0.04), "hips_rot": (2.0, 0.0, 0.0),
    **torso(16.0, 0.0, 0.0),
    "neck": (6.0, 0.0, 0.0), "head": (6.0, 0.0, 0.0),
    "l_clav": (3.0, 4.0), "r_clav": (3.0, 4.0),
    "l_hand": (0.21, 0.06, -0.47), "l_arm_follow": 1.0, "l_elbow": (0.8, -0.6, 0.0), "l_hand_z": (0.0, 0.0),
    "l_wrist": (10.0, 0.0, 0.0), "l_fingers": 0.55,
    "r_hand": (0.21, 0.06, -0.47), "r_arm_follow": 1.0, "r_elbow": (0.8, -0.6, 0.0), "r_hand_z": (0.0, 0.0),
    "r_wrist": (10.0, 0.0, 0.0), "r_fingers": 0.55,
    **plant("l", 0.08, 0.0, yaw=14.0), "l_knee": (0.25, 1.0, 0.0), "l_leg_fk": 0.0, "l_leg": (0.0, 0.0, 0.0, 0.0),
    **plant("r", 0.08, 0.0, yaw=14.0), "r_knee": (0.25, 1.0, 0.0), "r_leg_fk": 0.0, "r_leg": (0.0, 0.0, 0.0, 0.0),
}


def orderly_idle(t):
    period = 4.0
    w = 2.0 * math.pi * t / period

    def breath(delay=0.0):
        return math.sin(2.0 * math.pi * (t - delay) / 2.0)

    sway = math.sin(w)
    p = dict(O_BASE)
    p["hips_pos"] = (0.02 * sway, 0.004 * math.sin(w + 1.3), -0.04 - 0.004 * breath())
    p["hips_rot"] = (2.0, 1.5 * math.sin(w + 1.0), -1.2 * sway)
    p.update(torso(16.0 - 2.5 * breath(0.05), 2.0 * math.sin(w + 0.5), 1.6 * math.sin(w - 0.3)))
    add(p, "spine2", -1.6 * breath(0.12), 0.0, 0.0)
    p["neck"] = (6.0 + 1.2 * breath(0.25), 2.0 * math.sin(w - 0.8), -0.8 * math.sin(w - 0.6))
    p["head"] = (6.0 + 2.2 * breath(0.35), 5.0 * math.sin(w - 1.1), -1.5 * math.sin(w - 0.9))
    for pre, phase in (("l_", 0.0), ("r_", 1.7)):
        p[pre + "clav"] = (3.0 + 3.5 * breath(0.18), 4.0 - 1.0 * breath(0.18))
        p[pre + "hand"] = (0.21 + 0.012 * breath(0.22), 0.06 + 0.008 * math.sin(w + phase), -0.47 + 0.006 * breath(0.3))
        p[pre + "fingers"] = 0.55 + 0.12 * math.sin(w + 2.0 + phase)
        p[pre + "wrist"] = (10.0 + 4.0 * math.sin(w + 1.0 + phase), 0.0, 0.0)
    return p


ORDERLY_WALK_PERIOD = 1.1
ORDERLY_WALK_SPEED = 0.95 / ORDERLY_SCALE


def orderly_walk(t):
    """Heavy, wide steps: the trunk rolls over each foot in turn and sinks as it takes the weight."""
    period, speed = ORDERLY_WALK_PERIOD, ORDERLY_WALK_SPEED
    u = (t / period) % 1.0
    w = 2.0 * math.pi * u
    p = dict(O_BASE)
    stance = 0.64
    front = 0.30
    back = front - speed * stance * period
    for side, phase in (("l", 0.0), ("r", 0.5)):
        v = (u - phase) % 1.0
        fwd, lift, pitch = gait(v, stance, front, back, lift=0.085, toe_up=10.0, heel_up=20.0)
        swing = math.sin(math.pi * (v - stance) / (1.0 - stance)) if v > stance else 0.0
        p.update(plant(side, 0.08 + 0.035 * swing, fwd, lift, pitch, yaw=14.0))

    p["hips_pos"] = (0.05 * math.sin(w - 0.35), 0.0, -0.062 - 0.028 * math.cos(2.0 * (w - 0.38)))
    p["hips_rot"] = (3.0, -5.0 * math.cos(w), -3.0 * math.sin(w - 0.35))
    p.update(torso(16.0 + 2.5 * math.cos(2.0 * (w - 0.75)), 4.0 * math.cos(w - 0.2), 5.0 * math.sin(w - 0.6)))
    p["neck"] = (6.0 + 1.5 * math.cos(2.0 * (w - 1.1)), 0.0, -1.5 * math.sin(w - 0.8))
    p["head"] = (6.0 + 2.5 * math.cos(2.0 * (w - 1.3)), -1.5 * math.cos(w - 0.4), -2.0 * math.sin(w - 1.0))
    # Arms carried wide of the trunk; they swing against the legs, a little late.
    for pre, s in (("l_", -1.0), ("r_", 1.0)):
        swing = s * math.cos(w - 0.5)
        p[pre + "hand"] = (0.22 + 0.01 * math.cos(2.0 * (w - 0.9)), 0.07 + 0.10 * swing, -0.46 + 0.012 * math.cos(2.0 * (w - 1.0)))
        p[pre + "wrist"] = (10.0 + 6.0 * s * math.cos(w - 1.1), 0.0, 0.0)
        p[pre + "clav"] = (3.0 + 2.0 * math.cos(2.0 * (w - 1.0)), 4.0 + 3.0 * swing)
    return p


_O_FEET_UP = {**plant("l", 0.08, 0.0, pitch=12.0, yaw=14.0), **plant("r", 0.08, 0.0, pitch=12.0, yaw=14.0)}
_O_FISTS = {"l_fingers": 1.0, "r_fingers": 1.0}


def _both(hand, elbow, clav, wrist=(0.0, 0.0, 0.0)):
    return {"l_hand": hand, "r_hand": hand, "l_elbow": elbow, "r_elbow": elbow, "l_clav": clav, "r_clav": clav,
            "l_wrist": wrist, "r_wrist": wrist, **_O_FISTS}


_O_ATTACK = Seq(O_BASE, [
    (0.0, {}),
    # Sinks and gathers the hands in front of the apron, round the belly and not through it.
    (0.09, {"hips_pos": (0.0, 0.0, -0.065), "hips_rot": (3.0, 0.0, 0.0), **torso(20.0),
            "l_hand": (0.21, 0.36, -0.32), "r_hand": (0.21, 0.36, -0.32), "l_fingers": 0.8, "r_fingers": 0.8,
            "l_clav": (2.0, 8.0), "r_clav": (2.0, 8.0)}, "lin"),
    (0.18, {"hips_pos": (0.0, 0.0, -0.085), "hips_rot": (4.0, 0.0, 0.0), **torso(23.0),
            **_both((-0.05, 0.47, -0.19), (1.0, -0.4, -0.2), (2.0, 10.0))}),
    (0.34, {"hips_pos": (0.0, -0.01, -0.05), "hips_rot": (2.0, 0.0, 0.0), **torso(8.0),
            "neck": (8.0, 0.0, 0.0), "head": (8.0, 0.0, 0.0),
            **_both((-0.05, 0.40, 0.04), (1.0, -0.1, -0.3), (8.0, 14.0))}),
    # Up past the face, well in front of it, and over the head.
    (0.44, {"hips_pos": (0.0, -0.02, -0.025), "hips_rot": (-1.0, 0.0, 0.0), **torso(-4.0),
            "neck": (9.0, 0.0, 0.0), "head": (9.0, 0.0, 0.0),
            **_both((-0.05, 0.36, 0.33), (1.0, 0.2, -0.1), (18.0, 10.0)), **_O_FEET_UP}),
    (0.53, {"hips_pos": (0.0, -0.03, -0.01), "hips_rot": (-3.0, 0.0, 0.0), **torso(-13.0),
            "neck": (10.0, 0.0, 0.0), "head": (10.0, 0.0, 0.0),
            **_both((-0.05, 0.08, 0.50), (1.0, 0.5, 0.0), (26.0, 0.0)), **_O_FEET_UP}),
    (0.60, {"hips_pos": (0.0, -0.03, -0.012), "hips_rot": (-3.0, 0.0, 0.0), **torso(-16.0),
            "neck": (10.0, 0.0, 0.0), "head": (10.0, 0.0, 0.0),
            **_both((-0.05, 0.02, 0.51), (1.0, 0.5, 0.0), (27.0, -2.0)), **_O_FEET_UP}),
    (0.655, {"hips_pos": (0.0, 0.02, -0.05), "hips_rot": (2.0, 0.0, 0.0), **torso(2.0),
             "neck": (4.0, 0.0, 0.0), "head": (0.0, 0.0, 0.0),
             **_both((-0.05, 0.38, 0.32), (1.0, 0.2, -0.2), (20.0, 12.0), (-5.0, 0.0, 0.0))}, "lin"),
    # The blow: the fists come through where a head would be, at full speed.
    (0.70, {"hips_pos": (0.0, 0.06, -0.10), "hips_rot": (6.0, 0.0, 0.0), **torso(22.0),
            "neck": (-2.0, 0.0, 0.0), "head": (-8.0, 0.0, 0.0),
            **_both((-0.05, 0.50, 0.02), (1.0, -0.1, -0.3), (8.0, 20.0), (10.0, 0.0, 0.0))}, "lin"),
    (0.78, {"hips_pos": (0.0, 0.08, -0.17), "hips_rot": (10.0, 0.0, 0.0), **torso(40.0),
            "neck": (-4.0, 0.0, 0.0), "head": (-12.0, 0.0, 0.0),
            **_both((-0.05, 0.52, -0.10), (1.0, 0.0, 0.0), (0.0, 22.0), (20.0, 0.0, 0.0))}, "hit"),
    (0.98, {"hips_pos": (0.0, 0.07, -0.15), "hips_rot": (9.0, 0.0, 0.0), **torso(36.0),
            "neck": (0.0, 0.0, 0.0), "head": (-4.0, 0.0, 0.0),
            **_both((-0.05, 0.50, -0.08), (1.0, 0.0, 0.0), (2.0, 20.0), (14.0, 0.0, 0.0))}),
    (1.13, {"hips_pos": (0.0, 0.05, -0.115), "hips_rot": (6.5, 0.0, 0.0), **torso(30.0),
            "l_hand": (0.17, 0.42, -0.24), "r_hand": (0.17, 0.42, -0.24), "l_fingers": 0.9, "r_fingers": 0.9,
            "l_clav": (2.0, 14.0), "r_clav": (2.0, 14.0)}, "lin"),
    (1.28, {"hips_pos": (0.0, 0.03, -0.08), "hips_rot": (4.0, 0.0, 0.0), **torso(24.0),
            "l_hand": (0.25, 0.20, -0.40), "r_hand": (0.25, 0.20, -0.40), "l_fingers": 0.8, "r_fingers": 0.8,
            "l_clav": (2.0, 10.0), "r_clav": (2.0, 10.0)}),
    (1.6, {}),
])


def orderly_attack(t):
    """Gathers, hauls both fists up over its head and brings them down. The blow is at 0.7 s."""
    p = _O_ATTACK.at(t, {"spine1": 0.012, "spine2": 0.024, "neck": 0.04, "head": 0.055})
    jolt = ring(t, 0.78, 5.0, 9.0)
    add(p, "hips_pos", 0.0, 0.0, -0.012 * jolt)
    add(p, "head", 5.0 * ring(t, 0.82, 5.0, 8.0), 0.0, 0.0)
    return p


_O_HIT = Seq(O_BASE, [
    (0.0, {}, "go"),
    (0.08, {"hips_pos": (0.0, -0.03, -0.045), "hips_rot": (0.0, 3.0, 0.0), **torso(8.0, 9.0, 0.0),
            "neck": (2.0, 2.0, 0.0), "head": (-5.0, 4.0, 2.0),
            "l_clav": (8.0, 2.0), "r_clav": (6.0, 0.0),
            "l_hand": (0.25, 0.10, -0.44), "r_hand": (0.24, 0.02, -0.45), "l_fingers": 0.25, "r_fingers": 0.3}),
    (0.22, {"hips_pos": (0.0, -0.012, -0.05), "hips_rot": (3.0, 1.0, 0.0), **torso(19.0, 2.0, 0.0),
            "neck": (8.0, 0.0, 0.0), "head": (9.0, -1.0, 0.0),
            "l_hand": (0.22, 0.05, -0.47), "r_hand": (0.22, 0.07, -0.47), "l_fingers": 0.8, "r_fingers": 0.8}),
    (14.0 / FPS, {}),
])


def orderly_hit(t):
    """It gives a hand's breadth at the chest, the head nods after, and it is back."""
    return _O_HIT.at(t, {"spine1": 0.012, "spine2": 0.024, "neck": 0.04, "head": 0.06,
                         "l_hand": 0.03, "r_hand": 0.03, "l_fingers": 0.05, "r_fingers": 0.05})


# The death: the feet slide back and roll onto their tops as the knees come down, and stay there.
_O_KNEE_FWD, _O_KNEE_UP = 0.25, 0.10
_O_ANKLE_UP = 0.15
_O_ANKLE_FWD = _O_KNEE_FWD - math.sqrt(SHIN ** 2 - (_O_ANKLE_UP - _O_KNEE_UP) ** 2)
_O_DOWN = {"l_ankle": (0.08, _O_ANKLE_FWD + ANKLE.y, _O_ANKLE_UP - ANKLE.z), "r_ankle": (0.08, _O_ANKLE_FWD + ANKLE.y, _O_ANKLE_UP - ANKLE.z),
           "l_foot": (150.0, 22.0, 0.0), "r_foot": (150.0, 22.0, 0.0), "l_toe": 0.0, "r_toe": 0.0,
           "l_knee": (0.15, 0.6, -1.0), "r_knee": (0.15, 0.6, -1.0)}
_O_KNEEL_T, _O_FALL_T, _O_FLOOR_T = 0.80, 1.30, 1.85
_O_PRONE = math.degrees(math.acos((0.22 - _O_KNEE_UP) / THIGH))  # the thigh angle that lays the pelvis on the belly
_O_THIGH = Curve([_O_KNEEL_T, _O_FALL_T, _O_FLOOR_T, 2.4], [-8.0, 4.0, _O_PRONE, _O_PRONE], [None, None, "hit", None])
_O_PELVIS = Curve([_O_KNEEL_T, _O_FALL_T, _O_FLOOR_T, 2.4], [10.0, 14.0, 90.0, 90.0], [None, None, "hit", None])
_OK = hips_over_knees(_O_KNEE_FWD, _O_KNEE_UP, -8.0, 10.0)
_O_PRONE_POSE = {
    **torso(-2.0, 0.0, 0.0), "neck": (4.0, 30.0, 0.0), "head": (6.0, 38.0, 0.0),
    "l_hand": (0.24, 0.24, 0.0), "l_hand_z": (1.0, 0.14), "l_arm_follow": 0.0, "l_elbow": (1.0, -0.2, 0.0), "l_fingers": 0.15,
    "l_wrist": (0.0, 0.0, 0.0), "l_clav": (10.0, 10.0),
    "r_hand": (0.30, -0.20, 0.0), "r_hand_z": (1.0, 0.14), "r_arm_follow": 0.0, "r_elbow": (1.0, 0.0, 0.0), "r_fingers": 0.15,
    "r_wrist": (0.0, 0.0, 0.0), "r_clav": (4.0, 6.0),
    **_O_DOWN,
}
_O_DEATH = Seq(O_BASE, [
    (0.0, {}),
    # Stops, straightens, the head goes back.
    (0.26, {"hips_pos": (0.0, -0.04, -0.03), "hips_rot": (0.0, 0.0, 0.0), **torso(-2.0),
            "neck": (-4.0, 0.0, 2.0), "head": (-14.0, 4.0, 4.0),
            "l_hand": (0.25, 0.02, -0.45), "r_hand": (0.25, 0.02, -0.45), "l_fingers": 0.2, "r_fingers": 0.2}),
    (0.56, {"hips_pos": (0.0, 0.06, -0.26), "hips_rot": (6.0, 0.0, 0.0), **torso(12.0),
            "neck": (4.0, 0.0, 0.0), "head": (2.0, 0.0, 0.0),
            "l_hand": (0.22, 0.06, -0.48), "r_hand": (0.22, 0.06, -0.48), "l_arm_follow": 0.3, "r_arm_follow": 0.3,
            "l_ankle": (0.08, -0.13, 0.15), "r_ankle": (0.08, -0.13, 0.15), "l_foot": (84.0, 18.0, 0.0), "r_foot": (84.0, 18.0, 0.0),
            "l_toe": 0.0, "r_toe": 0.0, "l_knee": (0.2, 1.0, -0.5), "r_knee": (0.2, 1.0, -0.5)}, "lin"),
    # The knees take the weight; the trunk folds over them.
    (_O_KNEEL_T, {"hips_pos": (0.0, _OK[0], _OK[1]), "hips_rot": (10.0, 0.0, 0.0), **torso(34.0),
                  "neck": (14.0, 0.0, 0.0), "head": (22.0, 0.0, 0.0),
                  "l_hand": (0.20, 0.10, -0.50), "r_hand": (0.20, 0.10, -0.50), "l_arm_follow": 0.0, "r_arm_follow": 0.0,
                  "l_fingers": 0.35, "r_fingers": 0.35, **_O_DOWN}, "hit"),
    # Hangs there a moment, swaying.
    (_O_FALL_T, {"hips_rot": (14.0, 0.0, 0.0), **torso(20.0, 0.0, 4.0),
                 "neck": (16.0, 0.0, 4.0), "head": (24.0, 0.0, 8.0),
                 "l_hand": (0.18, 0.14, -0.50), "r_hand": (0.20, 0.12, -0.50), "l_arm_follow": 0.0, "r_arm_follow": 0.0,
                 "l_fingers": 0.35, "r_fingers": 0.35, **_O_DOWN}),
    # And goes down on its front.
    (_O_FLOOR_T, {"hips_rot": (90.0, 0.0, 0.0), **_O_PRONE_POSE}, "hit"),
    (2.4, {"hips_rot": (90.0, 0.0, 0.0), **_O_PRONE_POSE}),
])


def orderly_death(t):
    """Stops, drops onto its knees, hangs there, and falls forward onto the apron."""
    p = _O_DEATH.at(t, {"spine1": 0.015, "spine2": 0.03, "neck": 0.05, "head": 0.08,
                        "l_hand": 0.07, "r_hand": 0.05, "l_hand_z": 0.07, "r_hand_z": 0.05})
    knees = ring(t, _O_KNEEL_T, 3.5, 7.0)
    floor = ring(t, _O_FLOOR_T, 4.0, 8.0)
    thigh, pelvis = _O_THIGH(t), _O_PELVIS(t)
    if t >= _O_KNEEL_T:
        # From here the knees do not move: the hips go round them.
        thigh += 2.5 * knees
        forward, up = hips_over_knees(_O_KNEE_FWD, _O_KNEE_UP, thigh, pelvis)
        p["hips_pos"] = (0.012 * math.sin(2.2 * (t - _O_KNEEL_T)) * (1.0 - smooth((t - _O_FALL_T) / 0.3)), forward, up)
        p["hips_rot"] = (pelvis, 0.0, 0.0)
    add(p, "spine", 6.0 * knees - 3.0 * floor, 0.0, 0.0)
    add(p, "spine2", 4.0 * ring(t, _O_KNEEL_T + 0.04, 3.5, 7.0), 0.0, 0.0)
    add(p, "head", 10.0 * ring(t, _O_KNEEL_T + 0.09, 3.5, 6.0) - 7.0 * ring(t, _O_FLOOR_T + 0.05, 4.5, 8.0), 0.0, 0.0)
    return p


# name -> (seconds, loops, pose function)
CLIPS = {
    "PatientIdle": (6.0, True, patient_idle),
    "PatientWalk": (PATIENT_WALK_PERIOD, True, patient_walk),
    "PatientAttack": (0.9, False, patient_attack),
    "PatientHit": (0.8, False, patient_hit),
    "PatientDeath": (1.9, False, patient_death),
    "OrderlyIdle": (4.0, True, orderly_idle),
    "OrderlyWalk": (ORDERLY_WALK_PERIOD, True, orderly_walk),
    "OrderlyAttack": (1.6, False, orderly_attack),
    "OrderlyHit": (14.0 / FPS, False, orderly_hit),
    "OrderlyDeath": (2.4, False, orderly_death),
}


def author(armature, name, clips=None):
    """
    Keys the clip `name` on the armature, one key a bone a frame, from frame 1. Returns the last
    frame. The armature is the one of Raw/YBot.fbx, as imported by make_animations.import_fbx.
    `clips` is the table the clip is in (this module's, or player_clips.CLIPS).
    """
    import bpy

    seconds, loop, pose = (clips or CLIPS)[name]
    count = int(round(seconds * FPS))
    if armature.animation_data:
        armature.animation_data.action = None
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    armature.animation_data_create()
    armature.animation_data.action = bpy.data.actions.new(name)

    sk = Skeleton(armature)
    bones = {}
    for pose_bone in armature.pose.bones:
        pose_bone.rotation_mode = "QUATERNION"
        short = pose_bone.name[len(B):] if pose_bone.name.startswith(B) else pose_bone.name
        bones[short] = pose_bone
    stats = {}
    previous = {}
    for index in range(count + 1):
        # A loop's last frame is its first again, to the bit: the engine wraps at the clip's length.
        t = (index % count) / FPS if loop else index / FPS
        D = solve(sk, pose(t), stats)
        frame = index + 1
        for short in sk.names:
            above = sk.parent[short]
            local = D[short] if above is None else D[above].inverted() @ D[short]
            location, rotation, _scale = (sk.rest_inv[short] @ local @ sk.rest[short]).decompose()
            if short in previous and previous[short].dot(rotation) < 0.0:
                rotation.negate()
            previous[short] = rotation.copy()
            pose_bone = bones[short]
            pose_bone.rotation_quaternion = rotation
            pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=frame)
            if above is None:
                pose_bone.location = location
                pose_bone.keyframe_insert(data_path="location", frame=frame)
    print("AUTHORED %s: %d frames, %.3f s, %s; straightest leg %.3f, straightest arm %.3f (1 = out of reach)" % (
        name, count + 1, seconds, "loop" if loop else "once", stats.get("leg", 0.0), stats.get("arm", 0.0)))
    return count + 1
