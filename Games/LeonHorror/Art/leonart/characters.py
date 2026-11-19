"""
The three characters of LeonHorror, on the mannequin's skeleton.

    Protagonist   an ordinary adult: short jacket over a shirt, trousers, boots, a beanie and a
                  scarf, a small pack on the back. Seen from behind most of the time.
    Patient       thin, in a hospital gown tied at the back, barefoot, head wrapped in bandages.
    Orderly       big and broad, in scrubs, a rubber apron, gloves and clogs, cap, mask, goggles.

All three are built at the skeleton's own size (the bones cannot change), so their heights come
from a scale the game applies: see SCALE. Their widths come from the mesh.
"""

import math

import numpy as np

from . import charpaint as cp
from .charbody import Builder, Table, bump, smooth01, superellipse, unit
from .charparts import (DENY_HEAD, DENY_TORSO, TWO_PI, UpperGarment, arm, arm_line, deny_limb, foot, hand, head, leg_line,
                        mirror, rigid, shell, side_name, sole, spine_line, sweep, toward, trousers)

# Height each character should stand in the game, in metres. The build prints the scale that
# gives it (target height / height of the mesh as built).
HEIGHT = {"Protagonist": 1.78, "Patient": 1.72, "Orderly": 1.90}


class Character:
    def __init__(self, name, builder, sizes, materials):
        self.name = name
        self.builder = builder
        self.sizes = sizes          # {sheet: texture size}
        self.materials = materials  # material name per sheet, in sheet order


def ring_of(b, indices):
    """Centre, and outward directions, of a ring of vertices."""
    p = b.positions(indices)
    centre = p.mean(axis=0)
    return p, centre, unit(p - centre)


# --------------------------------------------------------------------------------------
# Shared shapes
# --------------------------------------------------------------------------------------
def head_slices(width=1.0, jaw=1.0, neck=1.0):
    """A plain adult head: slices from the base of the neck to the crown."""
    return Table([
        # z, centre y, half width, front depth, back depth, exponent
        (1.440, 0.040, 0.062 * neck, 0.060 * neck, 0.058 * neck, 2.0),
        (1.500, 0.032, 0.056 * neck, 0.056 * neck, 0.056 * neck, 2.0),
        (1.540, 0.022, 0.054 * neck, 0.058 * neck, 0.058 * neck, 2.0),
        (1.566, 0.010, 0.057 * jaw, 0.074 * jaw, 0.068, 2.1),
        (1.586, 0.004, 0.064 * jaw, 0.094 * jaw, 0.082, 2.2),
        (1.615, 0.002, 0.070 * jaw, 0.103, 0.094, 2.25),
        (1.655, 0.002, 0.0745 * width, 0.104, 0.101, 2.25),
        (1.700, 0.002, 0.0765 * width, 0.100, 0.105, 2.2),
        (1.740, 0.003, 0.0755 * width, 0.098, 0.104, 2.2),
        (1.772, 0.004, 0.066 * width, 0.084, 0.092, 2.1),
        (1.792, 0.005, 0.047 * width, 0.058, 0.066, 2.0),
        (1.802, 0.006, 0.022 * width, 0.028, 0.032, 2.0),
    ])


def face_relief(nose=1.0, brow=1.0):
    """The little a covered face needs: a nose, brows, eye sockets, a hint of cheekbones."""
    def relief(theta, z):
        a = (theta + math.pi) % TWO_PI - math.pi   # 0 at the front
        out = 0.020 * nose * bump(a, 0.0, 0.16) * bump(z, 1.648, 0.022) * (z < 1.70)
        out = out + 0.011 * nose * bump(a, 0.0, 0.12) * bump(z, 1.680, 0.028)
        out = out + 0.005 * brow * bump(np.abs(a), 0.30, 0.34) * bump(z, 1.716, 0.012)
        out = out - 0.007 * bump(np.abs(a), 0.36, 0.17) * bump(z, 1.694, 0.011)
        out = out + 0.003 * bump(np.abs(a), 0.62, 0.25) * bump(z, 1.665, 0.02)
        return out
    return relief


def ears(b, rig, point, paint=None, size=1.0):
    """Small flattened shapes on the sides of the head."""
    b.part("Ears", None, DENY_HEAD, rigid("Head"))
    for side in (1, -1):
        base = point(np.array(math.pi * 0.5 + 0.12), np.array(1.672))
        centres = [base + np.array([-0.008, 0.004, -0.024 * size]), base + np.array([0.004, 0.008, -0.010 * size]),
                   base + np.array([0.007, 0.012, 0.008 * size]), base + np.array([0.002, 0.010, 0.022 * size])]
        radii = [0.008 * size, 0.013 * size, 0.015 * size, 0.011 * size]
        sweep(b, mirror(centres, side), radii, 8, "Ear_" + side_name(side), paint, rows_per_segment=1, round_start=True,
              up=(1.0 * side, 0.35, 0.0), squash=0.42)


# --------------------------------------------------------------------------------------
# Protagonist
# --------------------------------------------------------------------------------------
def protagonist(rig):
    b = Builder(rig)
    P = cp.PROTAGONIST

    # ---- trousers
    knee_z = rig.j("LeftLeg")[2]

    def trouser_folds(side, phi, z):
        # Creases behind the knee and in front of the hip, and the cloth stacking on the boot.
        back = np.clip(-np.sin(phi), 0.0, 1.0)
        front = np.clip(np.sin(phi), 0.0, 1.0)
        knee = 0.0045 * back * np.sin((z - knee_z) * 150.0) * bump(z, knee_z + 0.005, 0.05)
        knee_front = 0.006 * front * bump(z, knee_z - 0.01, 0.045)
        stack = 0.0055 * np.sin((z - 0.14) * 185.0 + 0.8 * np.sin(phi * 2.0 + side)) * bump(z, 0.17, 0.065)
        hip = 0.004 * front * np.sin((z - 0.74) * 130.0 + phi * 1.5) * bump(z, 0.74, 0.05)
        drape = 0.002 * np.sin(phi * 3.0 + z * 9.0 + side) * smooth01((0.75 - z) / 0.2)
        return knee + knee_front + stack + hip + drape

    trouser_hems = trousers(b, rig, {
        "M": 40, "K": 8, "Rp": 8, "Rl": 36, "waist": 1.01, "crotch": 0.795, "hem": 0.118, "dip": 0.03,
        "pelvis": Table([
            (1.02, 0.006, 0.160, 0.128, 0.128, 2.2),
            (0.95, 0.008, 0.181, 0.136, 0.150, 2.3),
            (0.88, 0.008, 0.193, 0.132, 0.152, 2.35),
            (0.795, 0.006, 0.196, 0.118, 0.130, 2.3),
        ]),
        "leg": Table([
            # z, centre x, centre y, half width, front depth, back depth
            (0.795, 0.100, 0.004, 0.102, 0.118, 0.126),
            (0.76, 0.099, 0.004, 0.099, 0.112, 0.118),
            (0.70, 0.099, 0.004, 0.093, 0.105, 0.107),
            (0.60, 0.099, 0.006, 0.084, 0.092, 0.092),
            (0.525, 0.097, 0.008, 0.078, 0.088, 0.086),
            (0.40, 0.094, 0.022, 0.072, 0.074, 0.088),
            (0.30, 0.092, 0.033, 0.067, 0.062, 0.078),
            (0.20, 0.091, 0.036, 0.064, 0.059, 0.068),
            (0.118, 0.091, 0.030, 0.063, 0.062, 0.064),
        ]),
        "relief": trouser_folds,
    }, "Trousers", cp.painter(P, "trousers"))
    for side in (1, -1):
        b.part("TrouserHem_" + side_name(side), leg_line(rig, side), deny_limb(side, "leg"))
        p, centre, _out = ring_of(b, trouser_hems[side])
        b.close_opening(trouser_hems[side], centre, (0, 0, 1), 0.006, 0.03, "TrouserHemIn_" + side_name(side), cp.painter(P, "inside"), plug=True)

    # ---- boots
    ankle = rig.j("LeftFoot")
    toe = rig.j("LeftToeBase")

    def boot_width(u):
        return float(np.interp(u, [0.0, 0.25, 0.55, 0.8, 0.93, 1.0], [0.046, 0.050, 0.055, 0.050, 0.038, 0.020]))

    def boot_top(u):
        return float(np.interp(u, [0.0, 0.2, 0.5, 0.75, 0.9, 1.0], [0.118, 0.106, 0.092, 0.082, 0.074, 0.056]))

    for side in (1, -1):
        foot(b, rig, side, {
            "top": 0.30, "rows_leg": 7, "cols": 20, "rows_bend": 6, "rows_foot": 10,
            "leg": Table([
                (0.30, 0.092, 0.036, 0.052, 0.048, 0.058),
                (0.20, 0.091, 0.034, 0.050, 0.047, 0.052),
                (0.138, 0.091, 0.030, 0.049, 0.048, 0.050),
            ]),
            "instep": 0.138, "rho": 0.020, "floor": 0.022, "heel": 0.022, "length": 0.205,
            "half_width": boot_width, "top_height": boot_top, "tip": 0.010,
        }, "Boot_" + side_name(side), cp.painter(P, "boot"))
        sole(b, rig, side, {
            "back": ankle[1] + 0.082, "front": ankle[1] - 0.250, "cols": 24, "thickness": 0.030,
            "cx": lambda u: 0.091 + (toe[0] - ankle[0]) * u,
            "half_width": lambda u: float(np.interp(u, [0.0, 0.2, 0.45, 0.7, 0.9, 1.0], [0.044, 0.050, 0.052, 0.060, 0.054, 0.040])),
        }, "Sole_" + side_name(side), cp.painter(P, "sole"))

    # ---- jacket
    elbow_x = rig.j("LeftForeArm")[0]

    def jacket_folds(theta, z):
        a = (theta + math.pi) % TWO_PI - math.pi
        sides = np.abs(np.sin(theta))
        waist = 0.005 * sides * np.sin((z - 1.0) * 95.0 + 1.3 * np.cos(theta)) * bump(z, 1.03, 0.09)
        blouse = 0.010 * bump(z, 0.97, 0.045) * (0.75 + 0.25 * np.cos(theta * 9.0))
        back = 0.003 * np.cos(a * 2.0) * (np.cos(a) < 0) * np.sin((z - 1.1) * 30.0)
        return waist + blouse + back

    def sleeve_folds(psi, x):
        inner = np.clip(np.cos(psi - math.pi * 0.5), 0.0, 1.0)   # the front of the arm, where it creases
        elbow = 0.0055 * np.sin((x - elbow_x) * 165.0 + 1.2 * np.sin(psi)) * bump(x, elbow_x, 0.055) * (0.45 + 0.55 * inner)
        elbow_point = 0.007 * np.clip(np.cos(psi - math.pi * 1.5), 0.0, 1.0) ** 2 * bump(x, elbow_x + 0.005, 0.04)
        stack = 0.005 * np.sin((x - 0.70) * 200.0 + psi * 2.0) * bump(x, 0.675, 0.04)
        upper = 0.003 * np.sin((x - 0.3) * 80.0 + psi * 2.0) * bump(x, 0.33, 0.06)
        return elbow + elbow_point + stack + upper

    def jacket_hem(points, weights, rig_):
        # The hem hangs from the hips instead of following a thigh.
        amount = 0.6 * smooth01((1.0 - points[:, 2]) / 0.1)[:, None]
        out = weights * (1.0 - amount)
        out[:, rig_.index["Hips"]] += amount[:, 0]
        return out

    arm_y, arm_z = rig.j("LeftArm")[1], rig.j("LeftArm")[2]
    jacket = UpperGarment(b, rig, {
        "M": 48, "S": 6, "R1": 20, "R2": 8, "Hn": 3, "seam": 0.0, "hem": 0.872, "armpit": 1.338, "weights": jacket_hem,
        "body": Table([
            # z, centre y, half width, front depth, back depth, exponent
            (0.872, 0.006, 0.194, 0.138, 0.150, 2.3),
            (0.93, 0.006, 0.200, 0.142, 0.154, 2.3),
            (1.02, 0.004, 0.193, 0.142, 0.148, 2.3),
            (1.10, 0.000, 0.184, 0.146, 0.134, 2.3),
            (1.20, -0.006, 0.186, 0.152, 0.132, 2.3),
            (1.28, -0.008, 0.192, 0.156, 0.136, 2.3),
            (1.338, -0.008, 0.194, 0.154, 0.138, 2.3),
        ]),
        "neck": (0.074, 0.036, 1.548), "shoulder": (0.212, 0.058, 1.512),
        "neck_front": (0.078, 0.040), "neck_back": (0.062, 0.006),
        "chest": 0.72, "spread": 1.3, "shoulder_sag": 0.004,
        "relief": jacket_folds,
        "sleeve": {
            "rows": 26, "start": 0.250, "end": 0.708,
            "table": Table([
                # x, centre y, centre z, half depth, half height
                (0.250, arm_y - 0.002, arm_z + 0.003, 0.070, 0.068),
                (0.33, arm_y, arm_z, 0.066, 0.064),
                (elbow_x, arm_y, arm_z, 0.060, 0.058),
                (0.56, arm_y, arm_z, 0.056, 0.054),
                (0.66, arm_y, arm_z, 0.051, 0.049),
                (0.708, arm_y, arm_z, 0.045, 0.043),
            ]),
            "relief": sleeve_folds,
        },
    }, "Jacket", cp.painter(P, "jacket"), cp.painter(P, "sleeve"))

    # Ribbed band round the hem, and the inside of the jacket seen from below.
    b.part("JacketHem", spine_line(rig), DENY_TORSO, jacket_hem)
    theta = TWO_PI * np.arange(48) / 48
    hem_path = jacket.point(theta, np.full(48, 0.03))
    hem_centre = np.array([0.0, 0.006, 0.884])
    hem_out = unit((hem_path - hem_centre) * np.array([1.0, 1.0, 0.0]))
    b.band(hem_path + hem_out * 0.003, hem_out, (0, 0, 1), 0.012, 0.036, "JacketHemBand", cp.painter(P, "rib"), section=8, exponent=2.6)
    b.close_opening(jacket.idx[0], hem_centre, (0, 0, 1), 0.012, 0.06, "JacketInside", cp.painter(P, "inside"))

    for side in (1, -1):
        b.part("Cuff_" + side_name(side), arm_line(rig, side), deny_limb(side, "arm"))
        p, centre, out = ring_of(b, jacket.sleeve_end[side])
        b.band(p - out * 0.006 + np.array([0.012 * side, 0, 0]), out, (side, 0, 0), 0.008, 0.022, "Cuff_" + side_name(side),
               cp.painter(P, "rib"), section=8, exponent=2.6)
        b.close_opening(jacket.sleeve_end[side], centre + np.array([0.02 * side, 0, 0]), (-side, 0, 0), 0.010, 0.03,
                        "CuffIn_" + side_name(side), cp.painter(P, "inside"))

    # Collar: a ribbed band standing round the neck.
    b.part("Collar", spine_line(rig), DENY_TORSO)
    omega = TWO_PI * np.arange(40) / 40
    collar_path = jacket.neck_path(omega, lift=0.022, grow=0.004)
    collar_out = unit((collar_path - np.array([0.0, 0.03, 0.0])) * np.array([1.0, 1.0, 0.0]))
    b.band(collar_path, collar_out, unit(np.array([0, 0, 1.0]) + collar_out * 0.25), 0.009, 0.034, "Collar", cp.painter(P, "rib"),
           section=8, exponent=2.5)

    # ---- hands
    for side in (1, -1):
        hand(b, rig, side, {"thick": 1.14, "wrist": 0.035}, "Hand_" + side_name(side), cp.painter(P, "skin"))

    # ---- head, beanie, scarf
    point = head(b, rig, {"slices": head_slices(), "bottom": 1.47, "top": 1.800, "rows": 24, "cols": 32,
                          "relief": face_relief(), "marks": {"eyes": 1.694, "brow": 1.716, "nape": 1.60}},
                 "Head", cp.painter(P, "face", detail=1.7))

    b.part("Beanie", None, DENY_HEAD, rigid("Head"))

    def beanie_lift(theta, u):
        cuff = 0.006 * smooth01((0.34 - u) / 0.06)
        return 0.008 + cuff + 0.010 * smooth01((u - 0.55) / 0.45)

    shell(b, point, "Beanie", 1.708, 1.800, 14, 32, beanie_lift, cp.painter(P, "beanie"), crown=1.812,
          tilt=lambda th: -0.040 * (0.5 - 0.5 * np.cos(th)) ** 1.5)

    b.part("Scarf", spine_line(rig), ("Arm", "Hand", "Leg", "Foot", "Toe", "Hips"), toward("Neck", 0.35))
    omega = TWO_PI * np.arange(36) / 36
    for index, (z, rx, ryf, ryb, cy, th, hh, wob) in enumerate(((1.556, 0.082, 0.098, 0.082, 0.020, 0.030, 0.040, 0.0),
                                                              (1.606, 0.074, 0.100, 0.074, 0.012, 0.024, 0.034, 1.7))):
        c, s = np.cos(omega), np.sin(omega)
        ry = np.where(c > 0, ryf, ryb)
        path = np.stack([rx * s, cy - ry * c, z + 0.012 * np.sin(omega * 2.0 + wob) + 0.014 * c], axis=-1)
        out = unit(np.stack([s / rx, -c / ry, np.zeros_like(s)], axis=-1))
        b.band(path, out, (0, 0, 1), th * (1.0 + 0.12 * np.sin(omega * 3 + wob)), hh, "Scarf%d" % index, cp.painter(P, "scarf"),
               section=10, exponent=2.3)

    # ---- pack and straps
    b.part("Pack", spine_line(rig), DENY_TORSO, toward("Spine2", 0.5))
    zs = np.array([1.105, 1.12, 1.16, 1.25, 1.36, 1.41, 1.435, 1.445])
    half_w = np.array([0.070, 0.105, 0.120, 0.124, 0.118, 0.108, 0.085, 0.050])
    half_d = np.array([0.020, 0.045, 0.058, 0.062, 0.056, 0.048, 0.034, 0.016])
    phi = TWO_PI * np.arange(28) / 28
    c, s = superellipse(phi, 3.4)
    back_y = np.array([jacket.point(np.array(math.pi), np.array((z - 0.872) / (1.338 - 0.872)))[1] for z in np.clip(zs, 0.9, 1.33)])
    rings = np.stack([half_w[:, None] * s[None, :], (back_y + half_d * 0.82)[:, None] - half_d[:, None] * c[None, :],
                      np.broadcast_to(zs[:, None], (len(zs), 28))], axis=-1)
    b.tube(rings, "Pack", cp.painter(P, "pack"), cap_start=np.array([0, back_y[0] + 0.016, zs[0] - 0.004]),
           cap_end=np.array([0, back_y[-1] + 0.012, zs[-1] + 0.004]))

    b.part("Straps", spine_line(rig), DENY_TORSO)
    for side in (1, -1):
        k = 6.3
        d = jacket.d
        front_h = np.linspace(1.08, 2.0, 16)
        back_h = np.linspace(2.0, 1.2, 12)[1:]
        front_k = k + (2.0 - front_h) ** 2 * 2.6
        back_k = k - (2.0 - back_h) * 2.2          # the strap runs in under the top of the pack
        theta_path = np.concatenate([front_k * d, math.pi - back_k * d]) * side
        h_path = np.concatenate([front_h, back_h])
        across = np.array([-1.05, -0.8, 0.0, 0.8, 1.05]) * d * side
        th = theta_path[:, None] + across[None, :]
        # On the back panel a larger angle is nearer the centre, so the columns run the other way.
        th[len(front_h):] = theta_path[len(front_h):, None] - across[None, :]
        hh = np.broadcast_to(h_path[:, None], th.shape)
        b.patch(jacket.point(th, hh), jacket.normal(th, hh), 0.007, "Strap_" + side_name(side), cp.painter(P, "strap"))

    return Character("Protagonist", b, {0: 2048}, ["M_Protagonist"])


# --------------------------------------------------------------------------------------
# Patient
# --------------------------------------------------------------------------------------
def patient(rig):
    b = Builder(rig)
    P = cp.PATIENT
    ax, ay, az = rig.j("LeftArm")
    elbow_x = rig.j("LeftForeArm")[0]
    knee_z = rig.j("LeftLeg")[2]

    # ---- gown: open down the back, held by two ties
    def gown_folds(theta, z):
        hang = smooth01((1.28 - z) / 0.7)
        return (0.017 * hang * np.cos(theta * 7.0 + 1.3 * np.sin(theta * 2.0) + z * 2.0) +
                0.006 * hang * np.cos(theta * 13.0 + z * 5.0))

    def gap(z):
        upper = 0.030 * math.sin(min(max((z - 1.13) / 0.37, 0.0), 1.0) * math.pi)
        lower = 0.034 * float(smooth01((1.10 - z) / 0.45))
        return 0.003 + upper + lower

    def skirt(points, weights, rig_):
        # The skirt hangs from the hips more than it follows either thigh.
        amount = 0.3 * smooth01((0.92 - points[:, 2]) / 0.25)[:, None]
        out = weights * (1.0 - amount)
        out[:, rig_.index["Hips"]] += amount[:, 0]
        return out

    gown = UpperGarment(b, rig, {
        "M": 48, "S": 6, "R1": 30, "R2": 7, "Hn": 3, "seam": math.pi, "open": True, "gap": gap, "gap_reach": 5,
        "hem": 0.50, "armpit": 1.33, "weights": skirt, "lining": cp.painter(P, "gown_inside"),
        "body": Table([
            (0.50, 0.012, 0.188, 0.152, 0.150, 2.2),
            (0.75, 0.010, 0.172, 0.136, 0.145, 2.2),
            (0.95, 0.008, 0.160, 0.126, 0.140, 2.2),
            (1.10, 0.002, 0.148, 0.125, 0.118, 2.2),
            (1.25, -0.004, 0.152, 0.132, 0.116, 2.2),
            (1.33, -0.006, 0.158, 0.132, 0.118, 2.2),
        ]),
        "neck": (0.066, 0.036, 1.532), "shoulder": (0.188, 0.058, 1.492),
        "neck_front": (0.076, 0.052), "neck_back": (0.060, 0.012), "chest": 0.75, "spread": 1.3,
        "relief": gown_folds, "relax": 0.13,
        "sleeve": {"rows": 9, "start": 0.228, "end": 0.40,
                   "table": Table([(0.228, ay, az + 0.002, 0.060, 0.058), (0.31, ay, az, 0.060, 0.058), (0.40, ay, az - 0.004, 0.065, 0.062)]),
                   "relief": lambda psi, x: 0.004 * np.cos(psi * 5.0 + x * 20.0) * smooth01((x - 0.26) / 0.1)},
    }, "Gown", cp.painter(P, "gown"), cp.painter(P, "gown_sleeve"))
    for side in (1, -1):
        b.part("GownCuff_" + side_name(side), arm_line(rig, side), deny_limb(side, "arm"))
        p, centre, out = ring_of(b, gown.sleeve_end[side])
        b.close_opening(gown.sleeve_end[side], centre, (-side, 0, 0), 0.009, 0.07, "GownCuffIn_" + side_name(side), cp.painter(P, "gown_inside"))

    # Ties at the neck and the waist: a knot and two tails each.
    b.part("Ties", spine_line(rig), DENY_TORSO)
    for z, h in ((1.498, 1.86), (1.13, 0.759)):
        y = gown.point(np.array(math.pi), np.array(h))[1] + 0.008
        for side in (1, -1):
            sweep(b, [(0.05 * side, y - 0.004, z + 0.004), (0.02 * side, y + 0.002, z), (0.0, y + 0.006, z - 0.004),
                      (0.014 * side, y + 0.010, z - 0.05), (0.026 * side, y + 0.008, z - 0.12 - 0.02 * (side > 0))],
                  [0.005, 0.006, 0.0085, 0.006, 0.0055], 6, "Tie_%d_%s" % (int(z * 100), side_name(side)), cp.painter(P, "tie"),
                  rows_per_segment=2, up=(0.0, 1.0, 0.0), squash=0.4, round_start=True)

    # ---- the body where it shows: the back in the opening, arms, legs, feet, head
    b.part("Back", spine_line(rig), DENY_TORSO)
    h = np.concatenate([np.linspace(0.12, 1.0, 22), 1.0 + np.linspace(0.1, 0.85, 7)])
    # It narrows toward the neck, so it cannot come out over the shoulders.
    half = 1.05 - 0.6 * smooth01((h - 1.0) / 0.85)
    th = math.pi + half[:, None] * np.linspace(-1.0, 1.0, 15)[None, :]
    hh = np.broadcast_to(h[:, None], th.shape)
    spine = 0.004 * bump(th, math.pi, 0.09) * (0.6 + 0.4 * np.cos(hh * 60.0))
    back_points = gown.point(th, hh) - gown.normal(th, hh) * (0.024 - spine)[..., None]
    b.grid(b.add(back_points), "Back", cp.painter(P, "back"), facing=gown.normal(th, hh))

    def arm_bones(psi, x):
        knob = 0.006 * np.clip(np.cos(psi - math.pi * 1.5), 0.0, 1.0) ** 2 * bump(x, elbow_x, 0.022)
        wrist = 0.003 * np.abs(np.sin(psi)) * bump(x, 0.725, 0.015)
        return knob + wrist

    for side in (1, -1):
        arm(b, rig, side, {"start": 0.25, "end": 0.746, "rows": 24, "cols": 14, "relief": arm_bones,
                           "table": Table([(0.25, ay, az, 0.040, 0.040), (0.35, ay, az, 0.035, 0.036), (elbow_x, ay, az, 0.031, 0.032),
                                           (0.52, ay, az, 0.032, 0.030), (0.66, ay, az, 0.025, 0.023), (0.746, ay, az, 0.024, 0.019)])},
            "Arm_" + side_name(side), cp.painter(P, "arm_bandaged" if side > 0 else "arm"))
        hand(b, rig, side, {"thick": 0.86, "long": 1.06, "wrist": 0.02, "wrist_radius": (0.024, 0.018)}, "Hand_" + side_name(side),
             cp.painter(P, "hand_bandaged" if side > 0 else "hand"))

    def leg_bones(omega, z):
        cap = 0.009 * np.clip(-np.cos(omega), 0.0, 1.0) ** 2 * bump(z, knee_z + 0.005, 0.03)
        shin = 0.003 * np.clip(-np.cos(omega), 0.0, 1.0) ** 4 * bump(z, 0.32, 0.12)
        ankle = 0.006 * np.abs(np.sin(omega)) ** 3 * bump(z, 0.105, 0.02)
        return cap + shin + ankle

    def bare_width(u):
        return float(np.interp(u, [0.0, 0.3, 0.6, 0.8, 0.93, 1.0], [0.029, 0.034, 0.043, 0.045, 0.041, 0.034]))

    def bare_top(u):
        return float(np.interp(u, [0.0, 0.25, 0.5, 0.75, 0.9, 1.0], [0.068, 0.056, 0.044, 0.033, 0.026, 0.021]))

    for side in (1, -1):
        foot(b, rig, side, {
            "top": 0.82, "rows_leg": 30, "cols": 16, "rows_bend": 5, "rows_foot": 9, "relief": leg_bones,
            "leg": Table([
                (0.82, 0.092, 0.004, 0.066, 0.068, 0.072),
                (0.66, 0.093, 0.005, 0.056, 0.060, 0.060),
                (knee_z, 0.094, 0.008, 0.047, 0.049, 0.046),
                (0.40, 0.093, 0.022, 0.045, 0.037, 0.060),
                (0.25, 0.092, 0.030, 0.034, 0.031, 0.040),
                (0.083, 0.091, 0.028, 0.029, 0.030, 0.034),
            ]),
            "instep": 0.083, "rho": 0.015, "floor": 0.0, "heel": 0.018, "length": 0.172, "heel_pinch": 0.06,
            "half_width": bare_width, "top_height": bare_top, "tip": 0.004,
        }, "Leg_" + side_name(side), cp.painter(P, "leg"))
        # Toes: the big one on the inside.
        b.part("Toes_" + side_name(side), None, deny_limb(side, "leg"), rigid(side_name(side) + "ToeBase"))
        tip_y = 0.028 - 0.030 - 0.015 - 0.172
        for k, (dx, r, reach) in enumerate(((-0.027, 0.0125, 0.034), (-0.008, 0.0095, 0.031), (0.007, 0.009, 0.026),
                                            (0.020, 0.0085, 0.020), (0.031, 0.008, 0.013))):
            x = 0.095 + dx
            sweep(b, mirror([(x, tip_y + 0.022, 0.016), (x, tip_y, 0.0135), (x, tip_y - reach + r, 0.0115)], side),
                  [r * 0.9, r, r * 0.92], 6, "Toe%d_%s" % (k, side_name(side)), cp.painter(P, "toe"), rows_per_segment=2, squash=0.82)

    point = head(b, rig, {"slices": head_slices(0.96, 0.9, 0.82), "bottom": 1.46, "top": 1.798, "rows": 24, "cols": 32,
                          "relief": face_relief(0.8, 1.2), "marks": {"eyes": 1.694, "brow": 1.716, "nape": 1.60, "mouth": 1.618}},
                 "Head", cp.painter(P, "head", detail=1.6))
    # A few turns of bandage standing proud of the rest, and a loose end.
    b.part("Wraps", None, DENY_HEAD, rigid("Head"))
    omega = TWO_PI * np.arange(32) / 32
    for index, (z0, tilt, phase, half) in enumerate(((1.735, 0.030, 0.6, 0.019), (1.660, 0.034, 2.4, 0.017), (1.612, 0.016, 4.4, 0.015))):
        z = z0 + tilt * np.cos(omega - phase)
        path = point(omega, z, 0.0045)
        out = unit(point(omega, z, 0.02) - path)
        b.band(path, out, (0, 0, 1), 0.0035, half, "Wrap%d" % index, cp.painter(P, "wrap"), section=6, exponent=3.0)
    start = point(np.array(2.6), np.array(1.60), 0.006)
    sweep(b, [start, start + np.array([0.012, 0.016, -0.05]), start + np.array([0.020, 0.022, -0.12]), start + np.array([0.016, 0.018, -0.19])],
          [0.012, 0.011, 0.010, 0.008], 6, "WrapTail", cp.painter(P, "wrap"), up=(0.6, 0.8, 0.0), squash=0.25, round_start=True)

    return Character("Patient", b, {0: 2048}, ["M_Patient"])


# --------------------------------------------------------------------------------------
# Orderly
# --------------------------------------------------------------------------------------
def orderly(rig):
    b = Builder(rig)
    P = cp.ORDERLY
    ax, ay, az = rig.j("LeftArm")
    elbow_x = rig.j("LeftForeArm")[0]
    knee_z = rig.j("LeftLeg")[2]
    ankle, toe = rig.j("LeftFoot"), rig.j("LeftToeBase")

    # ---- scrub trousers, wide in the leg
    def scrub_folds(side, phi, z):
        drape = 0.006 * np.cos(phi * 4.0 + z * 6.0 + side) * smooth01((0.72 - z) / 0.3)
        knee = 0.005 * np.clip(-np.sin(phi), 0.0, 1.0) * np.sin((z - knee_z) * 120.0) * bump(z, knee_z, 0.06)
        stack = 0.007 * np.sin((z - 0.1) * 150.0 + np.sin(phi * 2.0)) * bump(z, 0.15, 0.07)
        return drape + knee + stack

    hems = trousers(b, rig, {
        "M": 40, "K": 8, "Rp": 8, "Rl": 32, "waist": 1.0, "crotch": 0.775, "hem": 0.095, "dip": 0.035,
        "pelvis": Table([(1.0, 0.004, 0.200, 0.176, 0.156, 2.3), (0.92, 0.004, 0.222, 0.180, 0.170, 2.3),
                         (0.85, 0.005, 0.228, 0.166, 0.170, 2.3), (0.775, 0.006, 0.228, 0.142, 0.152, 2.3)]),
        "leg": Table([(0.775, 0.114, 0.004, 0.114, 0.136, 0.142), (0.70, 0.113, 0.004, 0.108, 0.126, 0.128),
                      (0.60, 0.110, 0.006, 0.099, 0.112, 0.112), (knee_z, 0.107, 0.008, 0.092, 0.104, 0.100),
                      (0.40, 0.103, 0.020, 0.087, 0.091, 0.101), (0.25, 0.099, 0.030, 0.082, 0.079, 0.090),
                      (0.095, 0.096, 0.030, 0.083, 0.082, 0.087)]),
        "relief": scrub_folds,
    }, "Trousers", cp.painter(P, "trousers"))
    for side in (1, -1):
        b.part("TrouserHem_" + side_name(side), leg_line(rig, side), deny_limb(side, "leg"))
        p, centre, _out = ring_of(b, hems[side])
        b.close_opening(hems[side], centre, (0, 0, 1), 0.006, 0.04, "TrouserHemIn_" + side_name(side), cp.painter(P, "inside"))

    # ---- clogs
    for side in (1, -1):
        foot(b, rig, side, {
            "top": 0.21, "rows_leg": 3, "cols": 20, "rows_bend": 6, "rows_foot": 10,
            "leg": Table([(0.21, 0.092, 0.034, 0.047, 0.045, 0.050), (0.128, 0.092, 0.030, 0.049, 0.050, 0.050)]),
            "instep": 0.128, "rho": 0.018, "floor": 0.028, "heel": 0.024, "length": 0.212, "heel_pinch": 0.0,
            "half_width": lambda u: float(np.interp(u, [0.0, 0.25, 0.55, 0.8, 0.93, 1.0], [0.050, 0.057, 0.064, 0.063, 0.054, 0.034])),
            "top_height": lambda u: float(np.interp(u, [0.0, 0.2, 0.5, 0.75, 0.9, 1.0], [0.110, 0.108, 0.104, 0.097, 0.084, 0.058])),
            "tip": 0.012,
        }, "Clog_" + side_name(side), cp.painter(P, "clog"), sheet=1)
        sole(b, rig, side, {
            "back": ankle[1] + 0.088, "front": ankle[1] - 0.262, "cols": 24, "thickness": 0.036,
            "cx": lambda u: 0.092 + (toe[0] - ankle[0]) * u,
            "half_width": lambda u: float(np.interp(u, [0.0, 0.2, 0.45, 0.7, 0.9, 1.0], [0.050, 0.057, 0.060, 0.068, 0.062, 0.046])),
        }, "ClogSole_" + side_name(side), cp.painter(P, "clog_sole"), sheet=1)

    # ---- tunic
    def tunic_folds(theta, z):
        belly = 0.004 * np.sin((z - 1.0) * 70.0 + 2.0 * np.cos(theta)) * bump(z, 1.16, 0.09) * np.abs(np.sin(theta))
        hang = 0.008 * np.cos(theta * 6.0 + 1.0) * smooth01((0.95 - z) / 0.15)
        return belly + hang

    def tunic_skirt(points, weights, rig_):
        amount = 0.25 * smooth01((0.93 - points[:, 2]) / 0.12)[:, None]
        out = weights * (1.0 - amount)
        out[:, rig_.index["Hips"]] += amount[:, 0]
        return out

    tunic = UpperGarment(b, rig, {
        "M": 48, "S": 6, "R1": 20, "R2": 8, "Hn": 3, "seam": math.pi, "hem": 0.78, "armpit": 1.315, "weights": tunic_skirt,
        "body": Table([
            (0.78, 0.004, 0.232, 0.176, 0.170, 2.5),
            (0.90, 0.002, 0.236, 0.200, 0.174, 2.5),
            (1.02, -0.010, 0.234, 0.232, 0.166, 2.5),
            (1.12, -0.012, 0.228, 0.232, 0.160, 2.5),
            (1.24, -0.010, 0.230, 0.208, 0.162, 2.5),
            (1.315, -0.008, 0.236, 0.192, 0.164, 2.5),
        ]),
        "neck": (0.098, 0.040, 1.570), "shoulder": (0.268, 0.060, 1.536),
        "neck_front": (0.088, 0.090), "neck_back": (0.072, 0.008), "chest": 0.68, "spread": 1.3, "relax": 0.17,
        "relief": tunic_folds,
        "sleeve": {"rows": 9, "start": 0.305, "end": 0.44,
                   "table": Table([(0.305, ay, az + 0.004, 0.098, 0.094), (0.37, ay, az, 0.092, 0.088), (0.44, ay, az - 0.003, 0.088, 0.083)]),
                   "relief": lambda psi, x: 0.004 * np.cos(psi * 4.0 + x * 30.0) * smooth01((x - 0.33) / 0.08)},
    }, "Tunic", cp.painter(P, "tunic"), cp.painter(P, "tunic_sleeve"))
    b.part("TunicHem", spine_line(rig), DENY_TORSO, tunic_skirt)
    b.close_opening(tunic.idx[0], np.array([0.0, 0.004, 0.78]), (0, 0, 1), 0.006, 0.07, "TunicInside", cp.painter(P, "inside"))
    for side in (1, -1):
        b.part("TunicCuff_" + side_name(side), arm_line(rig, side), deny_limb(side, "arm"))
        p, centre, out = ring_of(b, tunic.sleeve_end[side])
        b.close_opening(tunic.sleeve_end[side], centre, (-side, 0, 0), 0.006, 0.06, "TunicCuffIn_" + side_name(side), cp.painter(P, "inside"))

    # ---- rubber apron: a bib on the chest, then a sheet hanging to the shins
    def apron_weights(points, weights, rig_):
        amount = 0.2 * smooth01((0.80 - points[:, 2]) / 0.2)[:, None]
        out = weights * (1.0 - amount)
        out[:, rig_.index["Hips"]] += amount[:, 0]
        return out

    b.part("Apron", spine_line(rig), DENY_TORSO, apron_weights)
    cols = 21
    h_rows = np.linspace(1.50, 0.0, 26)
    reach = np.where(h_rows > 1.0, 0.62, 0.62 + 0.62 * smooth01((1.0 - h_rows) / 0.3))
    reach = reach * 0.86
    across = np.linspace(-1.0, 1.0, cols)
    th = reach[:, None] * across[None, :]
    hh = np.broadcast_to(h_rows[:, None], th.shape)
    normals = tunic.normal(th, hh)
    upper = tunic.point(th, hh) + normals * 0.016
    drops = np.linspace(0.0, 0.46, 13)[1:]
    fall = (drops / 0.46)[:, None]
    wave = 0.010 * np.cos(across * 7.0)[None, :] * fall
    lower = upper[-1][None] + np.stack([upper[-1][:, 0][None] * 0.04 * fall,
                                        -0.035 * fall * np.cos(across * 1.2)[None, :] - wave,
                                        -np.broadcast_to(drops[:, None], (len(drops), cols))], axis=-1)
    apron_points = np.concatenate([upper, lower], axis=0)
    apron_normals = np.concatenate([normals, np.broadcast_to(normals[-1][None], lower.shape)], axis=0)
    b.slab(apron_points, apron_normals, 0.006, "Apron", cp.painter(P, "apron"), sheet=1)

    b.part("ApronStraps", spine_line(rig), DENY_TORSO)
    d = tunic.d
    for side in (1, -1):
        front_h = np.linspace(1.47, 2.0, 9)
        back_h = np.linspace(2.0, 1.72, 6)[1:]
        k_front = np.full(len(front_h), 4.3)
        k_back = 4.3 - (2.0 - back_h) / 0.28 * 3.6
        theta_path = np.concatenate([k_front * d, math.pi - k_back * d]) * side
        h_path = np.concatenate([front_h, back_h])
        offsets = np.array([-0.55, -0.4, 0.0, 0.4, 0.55]) * d * side
        tt = theta_path[:, None] + offsets[None, :]
        tt[len(front_h):] = theta_path[len(front_h):, None] - offsets[None, :]
        hh2 = np.broadcast_to(h_path[:, None], tt.shape)
        b.patch(tunic.point(tt, hh2), tunic.normal(tt, hh2), 0.008, "ApronStrap_" + side_name(side), cp.painter(P, "apron_strap"), sheet=1)
    # The waist tie round the back, with a knot.
    tie_theta = np.linspace(0.62 + 0.55, TWO_PI - 0.62 - 0.55, 34)
    tie_h = np.array([0.585, 0.57, 0.545, 0.52, 0.505])
    tt, hh2 = np.meshgrid(tie_theta, tie_h, indexing="ij")
    b.patch(tunic.point(tt, hh2), tunic.normal(tt, hh2), 0.007, "ApronTie", cp.painter(P, "apron_strap"), sheet=1)
    knot = tunic.point(np.array(math.pi), np.array(0.545)) + np.array([0.0, 0.012, 0.0])
    for side in (1, -1):
        sweep(b, [knot, knot + np.array([0.03 * side, 0.004, -0.05]), knot + np.array([0.045 * side, 0.0, -0.13])],
              [0.012, 0.008, 0.007], 6, "ApronTieEnd_" + side_name(side), cp.painter(P, "apron_strap"), sheet=1, up=(0.0, 1.0, 0.0),
              squash=0.4, round_start=True)

    # ---- arms: bare above the gloves
    for side in (1, -1):
        arm(b, rig, side, {"start": 0.36, "end": 0.60, "rows": 12, "cols": 18,
                           "table": Table([(0.36, ay, az, 0.072, 0.070), (elbow_x, ay, az, 0.062, 0.060), (0.52, ay, az, 0.064, 0.058),
                                           (0.60, ay, az, 0.054, 0.049)])},
            "Arm_" + side_name(side), cp.painter(P, "arm"))
        first, _last = arm(b, rig, side, {"start": 0.565, "end": 0.744, "rows": 9, "cols": 18,
                                          "table": Table([(0.565, ay, az, 0.060, 0.055), (0.60, ay, az, 0.057, 0.052), (0.67, ay, az, 0.049, 0.043),
                                                          (0.744, ay, az, 0.041, 0.033)])},
                           "Glove_" + side_name(side), cp.painter(P, "glove"), sheet=1)
        p, centre, out = ring_of(b, first)
        b.band(p, out, (side, 0, 0), 0.006, 0.010, "GloveRoll_" + side_name(side), cp.painter(P, "glove"), sheet=1, section=6)
        hand(b, rig, side, {"thick": 1.3, "wrist": 0.02, "wrist_radius": (0.039, 0.031)}, "GloveHand_" + side_name(side),
             cp.painter(P, "glove_hand"), sheet=1)

    # ---- head: cap, goggles, mask
    slices = head_slices(1.07, 1.14, 1.34)
    slices.values[0, 1:4] = (0.105, 0.085, 0.080)   # the neck spreads into the shoulders
    point = head(b, rig, {"slices": slices, "bottom": 1.44, "top": 1.800, "rows": 24, "cols": 32, "relief": face_relief(1.1, 1.3),
                          "marks": {"eyes": 1.694, "brow": 1.716, "nape": 1.60}}, "Head", cp.painter(P, "face", detail=1.4))
    ears(b, rig, point, cp.painter(P, "skin"), 1.1)

    b.part("Cap", None, DENY_HEAD, rigid("Head"))
    shell(b, point, "Cap", 1.716, 1.800, 12, 32, lambda th, u: 0.006 + 0.012 * np.sin(u * math.pi) ** 0.7, cp.painter(P, "cap"),
          crown=1.808, tilt=lambda th: -0.060 * (0.5 - 0.5 * np.cos(th)) ** 1.3)

    b.part("Mask", None, DENY_HEAD, rigid("Head"))
    angles = np.linspace(-1.28, 1.28, 15)
    heights = np.linspace(1.566, 1.684, 9)
    aa, zz = np.meshgrid(angles, heights)
    surface = point(aa, zz, 0.0)
    out = unit(point(aa, zz, 0.02) - surface)
    lift = 0.006 + 0.004 * np.cos(aa * 1.2) ** 2
    b.patch(surface, out, lift, "Mask", cp.painter(P, "mask"), sink=0.003)

    b.part("Goggles", None, DENY_HEAD, rigid("Head"))
    omega = TWO_PI * np.arange(32) / 32
    strap_z = 1.698 + 0.012 * (0.5 - 0.5 * np.cos(omega))
    path = point(omega, strap_z, 0.011)
    b.band(path, unit(point(omega, strap_z, 0.03) - path), (0, 0, 1), 0.003, 0.012, "GoggleStrap", cp.painter(P, "goggle_strap"), sheet=1, section=6,
           exponent=3.0)
    for side in (1, -1):
        angle = np.array(0.37 * side)
        base = point(angle, np.array(1.696), 0.0)
        n = unit(point(angle, np.array(1.696), 0.02) - base)
        n = unit(n + np.array([0.0, -0.5, 0.0]))
        sweep(b, [base - n * 0.004, base + n * 0.012, base + n * 0.024], [0.029, 0.029, 0.027], 12, "Goggle_" + side_name(side),
              cp.painter(P, "goggle"), sheet=1, rows_per_segment=1, exponent=2.8, squash=0.86, tip=0.18)
    bridge = point(np.array(0.0), np.array(1.696), 0.012)
    sweep(b, [bridge + np.array([-0.014, 0, 0]), bridge, bridge + np.array([0.014, 0, 0])], [0.008, 0.007, 0.008], 6, "GoggleBridge",
          cp.painter(P, "goggle_strap"), sheet=1, rows_per_segment=1, round_end=False, up=(0, 0, 1))

    return Character("Orderly", b, {0: 2048, 1: 1024}, ["M_Orderly", "M_OrderlyRubber"])


BUILDERS = {"Protagonist": protagonist, "Patient": patient, "Orderly": orderly}
