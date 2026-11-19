"""
LeonHorror - the player's clips, made for the protagonist's own body (jacket, backpack, scarf).

Imported by make_animations.py; regenerate and import with

    python Games/LeonHorror/Scripts/import_animations.py --player

and look at them on SK_Protagonist with review_enemy_clips.py. The poses are written and solved the
way enemy_clips.py explains (plain terms, IK for arms and legs, every bone keyed on every frame).

What the player carries decides the arms
    * the flashlight is always in the LEFT hand: the game hangs its mesh on mixamorig:LeftHand and
      turns it to the view by itself, so the left arm only has to hold it out in front, between
      waist and chest, in every clip that owns the upper body;
    * the RIGHT hand is empty or holds the pipe or the handgun, which the game hangs along the hand
      bone: the pipe and the muzzle point where the forearm and hand point.
  The fingers are closed round those things. (Tests/AnimationPipelineTests.cpp still expects
  LeftHandIndex1 at its rest rotation in the older clips, from when no clip posed a finger.)

How the game plays them (ULeonHorrorAnimInstance.cpp, UAnimInstance.cpp)
    * IdleRelaxed is the blend space's sample at speed 0, looped;
    * PistolAimIdle is layered on the spine and everything above it, looped, while aiming;
    * MeleeSwing, PistolFire and Interact are one-shots on the spine and above (0.08 s in, 0.18 s
      out) over whatever the legs are doing; HitReact is a one-shot over the whole body;
    * an upper-body clip therefore never moves the hips or the legs, and its arms are placed
      relative to the chest so that they mean the same thing over any legs.
  The clips the game does not load yet are described where they are defined.
"""

import math

from enemy_clips import (ANKLE, FPS, THIGH, Curve, Seq, add, bump, cyc, gait, lerp, mix, plant, ring, smooth,  # noqa: F401
                         torso)

# In-game height / height of the cooked mesh.
PLAYER_SCALE = 1.78 / 1.812
# The speed of the walk sample in the player's blend space (FLeonHorrorPlayerTuning::WalkSpeed).
PLAYER_WALK_SPEED = 2.60 / PLAYER_SCALE
PLAYER_WALK_PERIOD = 20.0 / FPS

_FEET = {**plant("l", 0.01, 0.0, yaw=8.0), "l_knee": (0.15, 1.0, 0.0), "l_leg_fk": 0.0, "l_leg": (0.0, 0.0, 0.0, 0.0),
         **plant("r", 0.01, 0.0, yaw=8.0), "r_knee": (0.15, 1.0, 0.0), "r_leg_fk": 0.0, "r_leg": (0.0, 0.0, 0.0, 0.0)}

# Standing, the flashlight held out in front of the left hip, the right arm at the side.
BASE = {
    "hips_pos": (0.0, 0.0, -0.015), "hips_rot": (1.0, 0.0, 0.0),
    **torso(4.0),
    "neck": (2.0, 0.0, 0.0), "head": (-2.0, 0.0, 0.0),
    "l_clav": (2.0, 3.0), "r_clav": (0.0, 2.0),
    "l_hand": (0.05, 0.30, -0.25), "l_arm_follow": 1.0, "l_elbow": (0.5, -1.0, -0.4), "l_hand_z": (0.0, 0.0),
    "l_wrist": (0.0, 0.0, 0.0), "l_fingers": 0.9,
    "r_hand": (0.14, 0.08, -0.50), "r_arm_follow": 1.0, "r_elbow": (0.4, -1.0, 0.0), "r_hand_z": (0.0, 0.0),
    "r_wrist": (5.0, 0.0, 0.0), "r_fingers": 0.5,
    **_FEET,
}

_CHAIN = {"spine1": 0.01, "spine2": 0.02, "neck": 0.035, "head": 0.05}


def idle_relaxed(t):
    period = 4.0
    w = 2.0 * math.pi * t / period

    def breath(delay=0.0):
        return math.sin(2.0 * math.pi * (t - delay) / period)

    p = dict(BASE)
    p["hips_pos"] = (0.006 * math.sin(w + 1.0), 0.0, -0.015 - 0.003 * breath())
    p["hips_rot"] = (1.0, 0.8 * math.sin(w + 1.6), -0.4 * math.sin(w + 1.0))
    p.update(torso(4.0 - 1.5 * breath(0.05), 1.0 * math.sin(w + 2.0), 0.8 * math.sin(w + 0.7)))
    p["neck"] = (2.0 + 0.4 * breath(0.2), 1.0 * math.sin(w - 0.8), 0.0)
    p["head"] = (-2.0 + 0.8 * breath(0.3), 3.0 * math.sin(w - 1.1), 0.0)
    p["l_clav"] = (2.0 + 1.5 * breath(0.15), 3.0)
    p["r_clav"] = (0.0 + 1.5 * breath(0.15), 2.0)
    p["l_hand"] = (0.05, 0.30 + 0.004 * math.sin(w + 0.5), -0.25 + 0.006 * breath(0.2))
    p["r_hand"] = (0.14, 0.08 + 0.006 * math.sin(w - 0.8), -0.50)
    p["r_fingers"] = 0.5 + 0.05 * math.sin(w + 2.0)
    return p


# ---- the pipe: wound up over the right shoulder, brought down across to the left -----------------
_MELEE = Seq(BASE, [
    (0.0, {}),
    (0.15, {**torso(-4.0, -26.0, 0.0), "neck": (2.0, 10.0, 0.0), "head": (0.0, 14.0, 0.0),
            "r_clav": (14.0, -8.0), "r_hand": (0.10, 0.04, 0.24), "r_elbow": (0.6, 1.0, -0.3), "r_fingers": 1.0,
            "l_hand": (0.09, 0.22, -0.28), "l_clav": (2.0, 8.0)}),
    # The blow (MeleeWindup, 0.28 s): the arm is out and the pipe is still coming down.
    (0.28, {**torso(14.0, 16.0, 0.0), "neck": (0.0, -4.0, 0.0), "head": (-4.0, -8.0, 0.0),
            "r_clav": (4.0, 20.0), "r_hand": (-0.06, 0.50, 0.0), "r_elbow": (0.6, -0.3, -0.8), "r_fingers": 1.0,
            "l_hand": (0.10, 0.12, -0.30), "l_clav": (2.0, -4.0)}, "lin"),
    (0.38, {**torso(18.0, 30.0, 0.0), "neck": (0.0, -8.0, 0.0), "head": (-4.0, -14.0, 0.0),
            "r_clav": (0.0, 24.0), "r_hand": (-0.20, 0.38, -0.30), "r_elbow": (0.5, -0.5, -0.5), "r_fingers": 1.0,
            "l_hand": (0.10, 0.10, -0.30), "l_clav": (2.0, -6.0)}),
    (0.52, {**torso(16.0, 25.0, 0.0), "neck": (0.0, -6.0, 0.0), "head": (-2.0, -10.0, 0.0),
            "r_clav": (0.0, 20.0), "r_hand": (-0.16, 0.40, -0.30), "r_elbow": (0.5, -0.6, -0.4), "r_fingers": 1.0,
            "l_hand": (0.09, 0.14, -0.29), "l_clav": (2.0, -4.0)}),
    (0.68, {**torso(8.0, 8.0, 0.0), "r_hand": (0.12, 0.30, -0.38), "r_fingers": 0.8, "l_hand": (0.06, 0.26, -0.26)}, "lin"),
    (0.8, {}),
])


def melee_swing(t):
    """Upper body. Winds up by 0.15 s, the blow at 0.28 s, through and across by 0.38 s, back by 0.8 s."""
    return _MELEE.at(t, {**_CHAIN, "l_hand": 0.03})


# ---- the handgun -------------------------------------------------------------------------------
# The right arm out in front of the right eye; the flashlight hand under that wrist, steadying it.
AIM = {**BASE, **torso(5.0, 6.0, 0.0), "neck": (0.0, -2.0, 0.0), "head": (-3.0, -4.0, 0.0),
       "r_clav": (6.0, 14.0), "r_hand": (-0.10, 0.50, 0.08), "r_elbow": (0.5, -0.2, -1.0), "r_wrist": (0.0, 0.0, 0.0), "r_fingers": 0.9,
       "l_clav": (4.0, 20.0), "l_hand": (-0.23, 0.42, -0.08), "l_elbow": (0.9, 0.0, -1.0)}


def pistol_aim_idle(t):
    w = 2.0 * math.pi * t / 2.0
    p = dict(AIM)
    drift = 0.004 * math.sin(w)
    p.update(torso(5.0 + 0.6 * math.sin(w - 0.3), 6.0, 0.0))
    p["r_hand"] = (-0.10 + 0.002 * math.sin(w + 1.2), 0.50, 0.08 + drift)
    p["l_hand"] = (-0.23 + 0.002 * math.sin(w + 1.2), 0.42, -0.08 + drift)
    return p


_FIRE = Seq(AIM, [
    (0.0, {}, "go"),
    (0.05, {**torso(2.5, 6.0, 0.0), "head": (-4.5, -4.0, 0.0), "r_clav": (10.0, 10.0),
            "r_hand": (-0.10, 0.44, 0.15), "l_hand": (-0.23, 0.36, -0.01)}),
    (0.15, {**torso(5.5, 6.0, 0.0), "r_hand": (-0.10, 0.505, 0.072), "l_hand": (-0.23, 0.425, -0.088)}),
    (0.3, {}),
])


def pistol_fire(t):
    """Upper body, from the aim: the muzzle kicks up and back by 0.05 s (the shot is at 0) and settles."""
    return _FIRE.at(t, {"head": 0.02})


_HIT = Seq(BASE, [
    (0.0, {}, "go"),
    (0.08, {"hips_pos": (0.0, -0.05, -0.05), "hips_rot": (-3.0, 5.0, 0.0), **torso(-12.0, 10.0, 3.0),
            "neck": (-6.0, 4.0, 2.0), "head": (-14.0, 8.0, 4.0), "l_clav": (10.0, 0.0), "r_clav": (10.0, 0.0),
            "l_hand": (0.12, 0.30, -0.16), "r_hand": (0.18, 0.10, -0.46), "r_fingers": 0.2}),
    (0.24, {"hips_pos": (0.0, -0.03, -0.04), "hips_rot": (2.0, 1.0, 0.0), **torso(10.0, 2.0, 0.0),
            "neck": (4.0, 0.0, 0.0), "head": (6.0, 0.0, 0.0),
            "l_hand": (0.05, 0.28, -0.26), "r_hand": (0.15, 0.06, -0.50)}),
    (0.5, {}),
])


def hit_react(t):
    """Whole body: knocked back at the chest, the head after it, and gathered again in half a second."""
    return _HIT.at(t, {**_CHAIN, "l_hand": 0.03, "r_hand": 0.04, "r_fingers": 0.05})


_REACH = {**torso(10.0, 10.0, 0.0), "neck": (2.0, -3.0, 0.0), "head": (4.0, -6.0, 0.0),
          "r_clav": (2.0, 18.0), "r_hand": (-0.04, 0.50, -0.12), "r_elbow": (0.5, -0.3, -0.8)}
_INTERACT = Seq(BASE, [
    (0.0, {}),
    (0.30, {**_REACH, "r_fingers": 0.15}),
    (0.55, {**_REACH, "r_hand": (-0.04, 0.51, -0.13), "r_fingers": 0.7}),
    (0.76, {**torso(6.0, 5.0, 0.0), "r_hand": (0.12, 0.32, -0.36)}, "lin"),
    (0.9, {}),
])


def interact(t):
    """Upper body: the right hand out to a handle or a switch at 0.3 s, on it until 0.55 s."""
    return _INTERACT.at(t, {**_CHAIN, "r_fingers": 0.03})


# --------------------------------------------------------------------------------------
# Clips the game does not load yet
# --------------------------------------------------------------------------------------
_TABLE = {"hips_pos": (0.0, -0.06, -0.06), "hips_rot": (18.0, 8.0, 0.0), **torso(22.0, 10.0, 0.0),
          "neck": (2.0, -4.0, 0.0), "head": (4.0, -6.0, 0.0), "r_clav": (0.0, 18.0),
          "r_hand": (-0.04, 0.34, -0.40), "r_arm_follow": 0.0, "r_elbow": (0.5, -0.4, -0.3),
          "l_hand": (0.06, 0.26, -0.18), "l_arm_follow": 0.0}
_PICKUP = Seq(BASE, [
    (0.0, {}),
    (0.14, {**mix(BASE, {}, _TABLE, 0.3), "r_hand": (0.12, 0.26, -0.42), "r_arm_follow": 0.5}, "lin"),
    (0.36, {**_TABLE, "r_fingers": 0.1}),
    (0.47, {**_TABLE, "r_hand": (-0.04, 0.345, -0.41), "r_fingers": 0.9}),
    (0.66, {**mix(BASE, {}, _TABLE, 0.35), "r_hand": (0.12, 0.28, -0.40), "r_arm_follow": 0.5, "r_fingers": 0.9}, "lin"),
    (0.8, {}),
])


def player_pickup(t):
    """
    Whole body, once, 0.8 s, standing still: bends and takes something off a table with the right
    hand. The hand is there at 0.36 s (frame 11) and has closed on it by 0.47 s (frame 14): the
    thing should leave the table at about 0.43 s.
    """
    return _PICKUP.at(t, {**_CHAIN, "r_fingers": 0.02})


_FLOOR = {"hips_pos": (0.0, -0.10, -0.50), "hips_rot": (35.0, 10.0, 0.0), **torso(40.0, 28.0, 0.0),
          "neck": (-6.0, -8.0, 0.0), "head": (-10.0, -12.0, 0.0), "r_clav": (0.0, 16.0),
          "r_hand": (-0.12, 0.16, -0.47), "r_arm_follow": 0.0, "r_elbow": (0.3, -0.5, 0.4),
          "l_hand": (0.16, 0.34, -0.20), "l_arm_follow": 0.0, "l_elbow": (0.6, -0.6, -0.4),
          **plant("l", 0.03, 0.04, yaw=12.0), **plant("r", 0.03, -0.10, pitch=38.0, yaw=10.0),
          "l_knee": (0.8, 1.0, 0.0), "r_knee": (0.9, 1.0, 0.0)}
_PICKUP_LOW = Seq(BASE, [
    (0.0, {}),
    (0.24, {**mix(BASE, {}, _FLOOR, 0.4), "r_hand": (0.14, 0.26, -0.40), "r_arm_follow": 0.5, "r_fingers": 0.2}, "lin"),
    (0.50, {**_FLOOR, "r_fingers": 0.1}),
    (0.62, {**_FLOOR, "r_hand": (-0.12, 0.16, -0.48), "r_fingers": 0.9}),
    (0.94, {**mix(BASE, {}, _FLOOR, 0.4), "r_hand": (0.16, 0.24, -0.38), "r_arm_follow": 0.5, "r_fingers": 0.9}, "lin"),
    (1.2, {}),
])


def player_pickup_low(t):
    """
    Whole body, once, 1.2 s, standing still: squats and takes something off the floor with the
    right hand. The hand is down at 0.50 s (frame 15) and closed by 0.62 s (frame 19): the thing
    should leave the floor at about 0.58 s.
    """
    return _PICKUP_LOW.at(t, {**_CHAIN, "r_fingers": 0.02})


_DOOR = Seq(BASE, [
    (0.0, {}),
    (0.07, {"r_hand": (0.12, 0.24, -0.34), "r_elbow": (0.8, -0.6, 0.0), "r_fingers": 0.7}, "lin"),
    (0.14, {**torso(2.0, -8.0, 0.0), "head": (-2.0, 6.0, 0.0), "r_clav": (10.0, 6.0),
            "r_hand": (-0.14, 0.31, -0.06), "r_elbow": (1.0, 0.1, 0.1), "r_fingers": 0.8}),
    (0.30, {**torso(8.0, 10.0, 0.0), "head": (0.0, -6.0, 0.0), "r_clav": (8.0, 20.0),
            "r_hand": (-0.14, 0.40, -0.04), "r_elbow": (1.0, 0.1, 0.1), "r_fingers": 0.8}, "lin"),
    (0.42, {**torso(12.0, 16.0, 0.0), "head": (2.0, -8.0, 0.0), "r_clav": (6.0, 24.0),
            "r_hand": (-0.10, 0.47, -0.04), "r_elbow": (1.0, 0.1, 0.1), "r_fingers": 0.8}),
    (0.58, {**torso(6.0, 6.0, 0.0), "r_hand": (0.10, 0.32, -0.30), "r_elbow": (0.8, -0.6, 0.0)}, "lin"),
    (0.7, {}),
])


def player_open_door(t):
    """
    Upper body, once, 0.7 s: the right forearm comes up across the chest and shoves a door. It
    meets the door at 0.30 s (frame 9) and pushes through until 0.42 s (frame 13).
    """
    return _DOOR.at(t, {**_CHAIN, "l_hand": 0.02})


_LOOK_DOWN = {"neck": (8.0, -3.0, 0.0), "head": (14.0, -6.0, 0.0)}
_GUN_UP = {"r_hand": (-0.12, 0.34, -0.10), "r_elbow": (0.6, -0.6, -0.6), "r_wrist": (0.0, 0.0, 35.0), "r_fingers": 0.9,
           "r_clav": (4.0, 10.0), **torso(8.0, 4.0, 0.0), **_LOOK_DOWN}
_RELOAD = Seq(BASE, [
    (0.0, {}),
    # The gun comes up in front of the chest and is tipped; the empty magazine drops.
    (0.24, {**_GUN_UP, "l_hand": (0.06, 0.22, -0.34)}),
    # The left hand, flashlight and all, goes to the belt for the full one...
    (0.46, {**_GUN_UP, "r_hand": (-0.12, 0.34, -0.12), "l_hand": (0.17, 0.14, -0.42), "l_elbow": (0.6, -1.0, 0.0), "l_clav": (-2.0, 0.0)}),
    # ...brings it up under the grip...
    (0.80, {**_GUN_UP, "l_hand": (-0.22, 0.31, -0.26), "l_elbow": (0.4, -0.4, -1.0), "l_clav": (2.0, 14.0)}, "lin"),
    # ...and drives it home.
    (0.88, {**_GUN_UP, "r_hand": (-0.12, 0.34, -0.085), "l_hand": (-0.22, 0.32, -0.20), "l_elbow": (0.4, -0.4, -1.0), "l_clav": (2.0, 14.0)}, "hit"),
    # Out from under the grip, over the top of the slide, back with it, and let it go.
    (0.99, {**_GUN_UP, "l_hand": (-0.06, 0.36, -0.14), "l_elbow": (0.5, -0.5, -0.8), "l_clav": (4.0, 14.0)}, "lin"),
    (1.12, {**_GUN_UP, "r_wrist": (0.0, 0.0, 10.0), "l_hand": (-0.22, 0.36, 0.02), "l_elbow": (0.5, -0.5, -0.6), "l_clav": (6.0, 14.0)}),
    (1.24, {**_GUN_UP, "r_wrist": (0.0, 0.0, 10.0), "r_hand": (-0.12, 0.37, -0.10), "l_hand": (-0.22, 0.28, 0.02),
            "l_elbow": (0.5, -0.5, -0.6), "l_clav": (6.0, 8.0)}, "hit"),
    (1.36, {**_GUN_UP, "r_wrist": (0.0, 0.0, 0.0), "l_hand": (0.0, 0.32, -0.14), "l_clav": (4.0, 10.0),
            "neck": (4.0, -2.0, 0.0), "head": (4.0, -3.0, 0.0)}),
    (1.56, {**torso(5.0, 2.0, 0.0), "r_hand": (0.10, 0.28, -0.34), "r_fingers": 0.9, "l_hand": (0.04, 0.30, -0.22)}, "lin"),
    (1.7, {}),
])


def player_reload(t):
    """
    Upper body, once, 1.7 s (PistolReloadSeconds): the gun is brought up and tipped by 0.24 s, the
    left hand fetches a magazine from the belt (0.46 s), seats it at 0.88 s (frame 26) and racks the
    slide at 1.24 s (frame 37). The round count can change at either; the clip ends as the reload does.
    """
    return _RELOAD.at(t, {"spine1": 0.01, "spine2": 0.02, "head": 0.04})


_PRESS = {**torso(14.0, 6.0, 0.0), "neck": (8.0, 4.0, 0.0), "head": (14.0, 8.0, 0.0),
          "r_clav": (2.0, 16.0), "r_hand": (-0.20, 0.315, -0.32), "r_elbow": (0.7, -0.3, -0.5), "r_fingers": 0.3,
          "l_hand": (0.14, 0.30, -0.22)}
_MEDKIT = Seq(BASE, [
    (0.0, {}),
    (0.18, {**torso(8.0, 3.0, 0.0), "r_hand": (0.04, 0.36, -0.40), "r_elbow": (0.6, -0.5, -0.3), "l_hand": (0.10, 0.30, -0.24)}, "lin"),
    (0.38, _PRESS),
    # It goes in: the shoulders come up round it.
    (0.70, {**_PRESS, **torso(19.0, 6.0, 2.0), "neck": (10.0, 4.0, 2.0), "head": (18.0, 8.0, 4.0),
            "r_hand": (-0.20, 0.30, -0.32), "r_clav": (10.0, 18.0), "l_clav": (9.0, 6.0), "r_fingers": 0.5}, "hit"),
    (0.98, {**_PRESS, **torso(16.0, 6.0, 1.0), "r_hand": (-0.20, 0.305, -0.32), "r_clav": (5.0, 16.0), "l_clav": (4.0, 4.0), "r_fingers": 0.5}),
    (1.18, {**_PRESS, **torso(10.0, 4.0, 0.0), "neck": (4.0, 2.0, 0.0), "head": (6.0, 4.0, 0.0), "r_hand": (-0.06, 0.36, -0.36)}, "lin"),
    (1.34, {**torso(6.0, 2.0, 0.0), "r_hand": (0.11, 0.30, -0.40)}, "lin"),
    (1.5, {}),
])


def player_use_medkit(t):
    """
    Upper body, once, 1.5 s: the right hand goes to the left of the belly (0.38 s), presses the
    dressing or the needle in at 0.70 s (frame 21) - the moment to give the health - holds, and lets go.
    """
    p = _MEDKIT.at(t, _CHAIN)
    wince = ring(t, 0.70, 6.0, 9.0)
    add(p, "head", 3.0 * wince, 0.0, 0.0)
    add(p, "spine2", 1.5 * wince, 0.0, 0.0)
    return p


# Hurt: bent over the right arm, which is held across the belly.
HURT = {**BASE,
        "hips_pos": (0.015, 0.0, -0.045), "hips_rot": (4.0, 0.0, -2.0), **torso(20.0, 4.0, 5.0),
        "neck": (8.0, 0.0, -2.0), "head": (8.0, 0.0, -4.0), "l_clav": (6.0, 8.0), "r_clav": (5.0, 14.0),
        "r_hand": (-0.20, 0.335, -0.32), "r_elbow": (0.7, -0.3, -0.5), "r_fingers": 0.4,
        "l_hand": (0.13, 0.30, -0.25), "l_elbow": (0.5, -1.0, -0.2),
        **plant("l", 0.02, 0.01, yaw=8.0), **plant("r", 0.02, -0.03, yaw=12.0)}


def player_hurt_idle(t):
    """Whole body, loop, 3.0 s: the idle for low health. Two heavy breaths and a wince to a loop."""
    period = 3.0
    w = 2.0 * math.pi * t / period

    def breath(delay=0.0):
        return math.sin(2.0 * math.pi * (t - delay) / 1.5)

    wince = ring(t, 1.9, 5.0, 7.0)
    p = dict(HURT)
    p["hips_pos"] = (0.015 + 0.01 * math.sin(w), 0.0, -0.045 - 0.005 * breath())
    p["hips_rot"] = (4.0, 1.0 * math.sin(w + 1.0), -2.0 - 0.6 * math.sin(w))
    p.update(torso(20.0 - 2.5 * breath(0.05) + 3.0 * wince, 4.0 + 1.5 * math.sin(w + 0.4), 5.0 + 1.2 * math.sin(w - 0.4)))
    p["neck"] = (8.0 + 1.5 * breath(0.2), 0.0, -2.0)
    p["head"] = (8.0 + 2.5 * breath(0.3) + 5.0 * ring(t, 1.95, 5.0, 7.0), 3.0 * math.sin(w - 1.0), -4.0 + 1.5 * math.sin(w - 0.7))
    p["l_clav"] = (6.0 + 3.0 * breath(0.15) + 3.0 * wince, 8.0)
    p["r_clav"] = (5.0 + 3.0 * breath(0.15) + 3.0 * wince, 14.0)
    p["l_hand"] = (0.13, 0.30 + 0.005 * math.sin(w + 0.5), -0.25 + 0.008 * breath(0.25))
    p["r_fingers"] = 0.4 + 0.2 * abs(wince)
    return p


def _stride(p, u, stance, left_front, right_front, lift, speed, period, toe_up=10.0, heel_up=26.0):
    for side, phase, front in (("l", 0.0, left_front), ("r", 0.5, right_front)):
        fwd, up, pitch = gait((u - phase) % 1.0, stance, front, front - speed * stance * period, lift=lift, toe_up=toe_up, heel_up=heel_up)
        p.update(plant(side, 0.0 if side == "l" else 0.0, fwd, up, pitch, yaw=6.0))


def player_hurt_walk(t):
    """
    Whole body, loop, 20 frames, in place at the blend space's walk speed (260 cm/s): the walk for
    low health, hunched over the arm across the belly. At that speed a body is off the ground between
    steps, so this is a hurried, hunched trot more than a walk; the planted feet go back at 260 cm/s.
    """
    period, speed = PLAYER_WALK_PERIOD, PLAYER_WALK_SPEED
    u = (t / period) % 1.0
    w = 2.0 * math.pi * u
    p = dict(HURT)
    _stride(p, u, 0.40, 0.33, 0.31, 0.07, speed, period)
    p["hips_pos"] = (0.02 * math.sin(w - 0.4), 0.02, -0.085 - 0.022 * math.cos(2.0 * (w - 0.45)) - 0.012 * math.cos(w - 0.5))
    p["hips_rot"] = (6.0, -5.0 * math.cos(w), -2.0 - 2.0 * math.sin(w - 0.4))
    p.update(torso(22.0 + 3.0 * math.cos(2.0 * (w - 0.8)), 4.0 + 4.0 * math.cos(w - 0.2), 5.0 + 3.0 * math.sin(w - 0.6)))
    p["neck"] = (8.0 + 2.0 * math.cos(2.0 * (w - 1.1)), 0.0, -2.0)
    p["head"] = (8.0 + 3.0 * math.cos(2.0 * (w - 1.3)), 0.0, -4.0 + 2.0 * math.sin(w - 1.0))
    p["l_hand"] = (0.13, 0.30, -0.25 + 0.012 * math.cos(2.0 * (w - 1.0)))
    p["l_clav"] = (6.0 + 2.0 * math.cos(2.0 * (w - 0.9)), 8.0)
    p["r_clav"] = (5.0 + 2.0 * math.cos(2.0 * (w - 0.9)), 14.0)
    return p


def player_walk(t):
    """
    Whole body, loop, 20 frames, in place at 260 cm/s: a brisk walk with the flashlight carried
    steady in front and only the right arm swinging. Not asked for: it is an alternative to
    WalkForward (whose left arm hangs and whose feet slide at this speed) for the blend space's
    sample at (260, 0), should it be wanted.
    """
    period, speed = PLAYER_WALK_PERIOD, PLAYER_WALK_SPEED
    u = (t / period) % 1.0
    w = 2.0 * math.pi * u
    p = dict(BASE)
    _stride(p, u, 0.40, 0.33, 0.33, 0.08, speed, period, toe_up=12.0, heel_up=30.0)
    p["hips_pos"] = (0.015 * math.sin(w - 0.4), 0.02, -0.075 - 0.022 * math.cos(2.0 * (w - 0.45)))
    p["hips_rot"] = (4.0, -7.0 * math.cos(w), -2.0 * math.sin(w - 0.4))
    p.update(torso(8.0 + 2.0 * math.cos(2.0 * (w - 0.8)), 7.0 * math.cos(w - 0.15), 1.5 * math.sin(w - 0.6)))
    p["neck"] = (0.0, -2.0 * math.cos(w - 0.3), 0.0)
    p["head"] = (-5.0 + 1.5 * math.cos(2.0 * (w - 1.2)), -3.0 * math.cos(w - 0.4), 0.0)
    swing = math.cos(w - 0.35)  # the right arm goes forward with the left leg
    p["r_hand"] = (0.15, 0.10 + 0.16 * swing, -0.45 + 0.03 * swing)
    p["r_elbow"] = (0.4, -1.0, 0.0)
    p["r_clav"] = (0.0, 2.0 + 4.0 * swing)
    p["l_hand"] = (0.05, 0.30 + 0.01 * math.cos(w - 0.6), -0.25 + 0.012 * math.cos(2.0 * (w - 1.0)))
    return p


# ---- falling backwards ---------------------------------------------------------------------------
_SUPINE_HIPS = (0.0, -0.66, -0.998 + 0.165)
_SUPINE = {
    "hips_pos": _SUPINE_HIPS, "hips_rot": (-90.0, 0.0, 0.0), **torso(33.0, 0.0, 0.0),
    "neck": (-12.0, 8.0, 0.0), "head": (-16.0, 16.0, 0.0), "l_clav": (4.0, -6.0), "r_clav": (4.0, -6.0),
    "r_hand": (0.40, 0.14, 0.0), "r_hand_z": (1.0, 0.17), "r_arm_follow": 0.0, "r_elbow": (1.0, 0.0, 0.1), "r_fingers": 0.3,
    "l_hand": (0.36, -0.16, 0.0), "l_hand_z": (1.0, 0.17), "l_arm_follow": 0.0, "l_elbow": (1.0, 0.0, 0.1), "l_fingers": 0.3,
    **plant("l", 0.03, 0.10, pitch=-50.0, yaw=30.0), "l_knee": (0.3, 0.0, 1.0),
    **plant("r", 0.07, 0.17, pitch=-60.0, yaw=40.0), "r_knee": (0.5, 0.0, 1.0),
}
_DEATH_BACK = Seq(BASE, [
    (0.0, {}, "go"),
    (0.16, {"hips_pos": (0.0, -0.06, -0.02), "hips_rot": (-4.0, 0.0, 0.0), **torso(-14.0, 6.0, 0.0),
            "neck": (-8.0, 2.0, 0.0), "head": (-18.0, 4.0, 0.0),
            "r_hand": (0.22, 0.20, -0.36), "r_fingers": 0.1, "l_hand": (0.16, 0.34, -0.10), "l_fingers": 0.2}),
    # A step back that does not hold.
    (0.42, {"hips_pos": (0.0, -0.24, -0.12), "hips_rot": (-8.0, 0.0, 0.0), **torso(-4.0, 0.0, 0.0),
            "neck": (4.0, 0.0, 0.0), "head": (6.0, 0.0, 0.0),
            "r_hand": (0.22, 0.30, -0.20), "r_fingers": 0.1, "l_hand": (0.20, 0.34, -0.10), "l_fingers": 0.2,
            **plant("r", 0.03, -0.32, yaw=10.0), **plant("l", 0.01, 0.0, pitch=-8.0, yaw=8.0)}, "lin"),
    # Sits down hard, the hands thrown back too late.
    (0.80, {"hips_pos": (0.0, -0.62, -0.998 + 0.17), "hips_rot": (-28.0, 0.0, 0.0), **torso(22.0, 0.0, 0.0),
            "neck": (6.0, 0.0, 0.0), "head": (10.0, 0.0, 0.0),
            "r_hand": (0.16, -0.22, 0.0), "r_hand_z": (1.0, 0.20), "r_arm_follow": 0.0, "r_elbow": (0.6, -1.0, 0.2), "r_fingers": 0.2,
            "l_hand": (0.16, -0.18, 0.0), "l_hand_z": (1.0, 0.20), "l_arm_follow": 0.0, "l_elbow": (0.6, -1.0, 0.2), "l_fingers": 0.3,
            **plant("l", 0.02, 0.02, pitch=-18.0, yaw=14.0), "l_knee": (0.2, 0.2, 1.0),
            **plant("r", 0.04, -0.06, pitch=-12.0, yaw=18.0), "r_knee": (0.3, 0.2, 1.0)}, "hit"),
    # The back and the head follow; the legs slide out.
    (1.22, _SUPINE, "hit"),
    (1.62, {**_SUPINE, "head": (-16.0, 26.0, 0.0), "l_knee": (1.0, 0.0, 0.35), **plant("l", 0.10, 0.12, pitch=-50.0, yaw=48.0)}),
    (2.0, {**_SUPINE, "head": (-16.0, 26.0, 0.0), "l_knee": (1.0, 0.0, 0.35), **plant("l", 0.10, 0.12, pitch=-50.0, yaw=48.0)}),
])


def player_death_backward(t):
    """
    Whole body, once, 2.0 s, the last frame held: struck, a step back that gives, sits down hard
    (0.80 s, frame 24), goes over onto the backpack (1.22 s, frame 37) and lies with one knee up
    that falls outwards. The body ends about 0.7 m behind where it stood.
    """
    p = _DEATH_BACK.at(t, {**_CHAIN, "head": 0.07, "neck": 0.05, "l_hand": 0.05, "r_hand": 0.04, "l_hand_z": 0.05, "r_hand_z": 0.04})
    seat = ring(t, 0.80, 4.0, 8.0)
    back = ring(t, 1.22, 4.5, 8.0)
    add(p, "spine", 5.0 * seat - 2.5 * back, 0.0, 0.0)
    add(p, "head", 8.0 * ring(t, 0.86, 4.0, 7.0) + 6.0 * ring(t, 1.29, 4.5, 7.0), 0.0, 0.0)
    return p


# name -> (seconds, loops, pose function). The first six replace the mannequin's clips of the same name.
CLIPS = {
    "IdleRelaxed": (4.0, True, idle_relaxed),
    "MeleeSwing": (0.8, False, melee_swing),
    "PistolAimIdle": (2.0, True, pistol_aim_idle),
    "PistolFire": (0.3, False, pistol_fire),
    "HitReact": (0.5, False, hit_react),
    "Interact": (0.9, False, interact),
    "PlayerPickup": (0.8, False, player_pickup),
    "PlayerPickupLow": (1.2, False, player_pickup_low),
    "PlayerOpenDoor": (0.7, False, player_open_door),
    "PlayerReload": (1.7, False, player_reload),
    "PlayerUseMedkit": (1.5, False, player_use_medkit),
    "PlayerHurtIdle": (3.0, True, player_hurt_idle),
    "PlayerHurtWalk": (PLAYER_WALK_PERIOD, True, player_hurt_walk),
    "PlayerWalk": (PLAYER_WALK_PERIOD, True, player_walk),
    "PlayerDeathBackward": (2.0, False, player_death_backward),
}
