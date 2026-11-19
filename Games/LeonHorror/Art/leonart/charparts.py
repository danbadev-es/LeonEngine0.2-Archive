"""
The pieces characters are assembled from: a garment for the upper body with set-in sleeves,
trousers, a foot (boot, clog or bare), a hand with fingers, a head, and swept shapes for the rest.

Each piece reads its joints from the rig, so it fits the skeleton whatever its proportions, and
takes its measurements as tables of stations (see charbody.Table) so a character is a set of
numbers rather than a new piece of code.

Angles round the torso: 0 is the front (-Y), a quarter turn is the character's left (+X), a half
turn the back. `side` is +1 for the left limb and -1 for the right.
"""

import math

import numpy as np

from .charbody import Table, bump, nearest_on_polyline, pchip, smooth01, superellipse, unit

TWO_PI = 2.0 * math.pi

# Bones a part must never follow, by part. A sleeve that picked up a little of a finger or a leg
# from the mannequin would tear when that bone moves.
DENY_TORSO = ("ForeArm", "Hand", "LeftLeg", "RightLeg", "Foot", "Toe", "Head")
DENY_HEAD = ("Arm", "Hand", "Leg", "Foot", "Toe", "Hips", "Spine1", "Shoulder")


def deny_limb(side, kind):
    other = "Right" if side > 0 else "Left"
    if kind == "arm":
        return (other, "Leg", "Foot", "Toe", "Head", "Neck", "Hips")
    return (other, "Arm", "Hand", "Shoulder", "Neck", "Head", "Spine1", "Spine2")


def side_name(side):
    return "Left" if side > 0 else "Right"


def mirror(points, side):
    points = np.array(points, dtype=np.float64)
    points[..., 0] *= side
    return points


def spine_line(rig):
    return [rig.j("Hips"), rig.j("Spine"), rig.j("Spine1"), rig.j("Spine2"), rig.j("Neck"), rig.j("Head"), rig.tail["Head"]]


def arm_line(rig, side):
    s = side_name(side)
    return [rig.j(s + "Shoulder"), rig.j(s + "Arm"), rig.j(s + "ForeArm"), rig.j(s + "Hand"), rig.tail[s + "Hand"]]


def leg_line(rig, side):
    s = side_name(side)
    return [rig.j("Hips"), rig.j(s + "UpLeg"), rig.j(s + "Leg"), rig.j(s + "Foot"), rig.j(s + "ToeBase"), rig.tail[s + "ToeBase"]]


def rigid(bone):
    """A weight recipe: everything follows one bone."""
    def apply(points, weights, rig):
        out = np.zeros_like(weights)
        out[:, rig.index[bone]] = 1.0
        return out
    return apply


def toward(bone, amount):
    """A weight recipe: pull the weights part of the way to one bone (stiffens a loose piece)."""
    def apply(points, weights, rig):
        out = weights * (1.0 - amount)
        out[:, rig.index[bone]] += amount
        return out
    return apply


# --------------------------------------------------------------------------------------
# Swept tubes: fingers, toes, ties, ears
# --------------------------------------------------------------------------------------
def sweep(b, centres, radii, cols, name, paint=None, sheet=0, rows_per_segment=2, round_start=False, round_end=True,
          up=(0.0, 0.0, 1.0), squash=1.0, exponent=2.0, tip=0.8):
    """
    A tube through `centres` (K x 3) with the given radii (K), its section a superellipse squashed
    along `up` by `squash`. Ends are rounded off (and closed) or left open.
    """
    centres = np.asarray(centres, dtype=np.float64)
    radii = np.asarray(radii, dtype=np.float64)
    distance = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(centres, axis=0), axis=1))])
    count = (len(centres) - 1) * rows_per_segment + 1
    u = np.interp(np.linspace(0, len(centres) - 1, count), np.arange(len(centres)), distance)
    path = np.stack([pchip(distance, centres[:, k], u) for k in range(3)], axis=-1)
    r = pchip(distance, radii, u)
    tangent = unit(np.gradient(path, axis=0))

    def cap(at, direction, radius):
        # A flattened hemisphere of three rings and a point.
        angles = np.array([0.45, 0.85, 1.2])
        extra = at[None] + direction[None] * (np.sin(angles) * radius * tip)[:, None]
        return extra, np.cos(angles) * radius, at + direction * radius * tip

    start_point = end_point = None
    if round_end:
        extra, extra_r, end_point = cap(path[-1], tangent[-1], r[-1])
        path, r, tangent = np.concatenate([path, extra]), np.concatenate([r, extra_r]), np.concatenate([tangent, np.repeat(tangent[-1:], 3, 0)])
    if round_start:
        extra, extra_r, start_point = cap(path[0], -tangent[0], r[0])
        path, r, tangent = np.concatenate([extra[::-1], path]), np.concatenate([extra_r[::-1], r]), np.concatenate([np.repeat(tangent[:1], 3, 0), tangent])

    up = np.asarray(up, dtype=np.float64)
    n1 = unit(up[None] - (tangent @ up)[:, None] * tangent)
    n2 = np.cross(tangent, n1)
    phi = np.linspace(0.0, TWO_PI, cols, endpoint=False)
    c, s = superellipse(phi, exponent)
    rings = path[:, None] + r[:, None, None] * (c[None, :, None] * n2[:, None] + squash * s[None, :, None] * n1[:, None])
    return b.tube(rings, name, paint, sheet, cap_start=start_point, cap_end=end_point)


# --------------------------------------------------------------------------------------
# Upper garment: jacket, gown, tunic
# --------------------------------------------------------------------------------------
class UpperGarment:
    """
    A body tube from the hem to the armpits, a front and a back panel from there to the shoulder
    line (welded along it, open round the neck), and sleeves that start on the armhole's own ring
    of vertices.

    Settings (g):
      M, S, R1, R2, Hn   columns round the body, columns under the arm, rows below and above the
                         armpit, columns from the centre to the side of the neck
      seam               angle of the column where the texture is cut (0 front, pi back)
      open               True for a garment that is open along the seam (a gown): the two edges
                         are separate and `gap(z)` gives half the opening in metres
      hem, armpit        heights
      body               Table of (z, centre y, half width, front depth, back depth, exponent)
      neck, shoulder     the side of the neck and the tip of the shoulder (x, y, z), left side
      neck_front/back    (depth, dip) of the neckline
      relief(theta, z)   folds: metres added to the radius
      sleeve             Table of (x, centre y, centre z, half depth, half height), rows, relief(psi, x)
    """

    def __init__(self, b, rig, g, name, paint=None, sleeve_paint=None, sheet=0):
        self.b, self.rig, self.g, self.name = b, rig, g, name
        M, S, R1, R2, Hn = g["M"], g["S"], g["R1"], g["R2"], g["Hn"]
        self.first_vertex = len(b.pos)
        self.M, self.S, self.R1, self.R2, self.Hn = M, S, R1, R2, Hn
        self.H = M // 4 - S // 2
        self.d = TWO_PI / M
        self.open = g.get("open", False)
        self.kseam = int(round(g.get("seam", 0.0) / self.d))
        ncol = M + 1 if self.open else M
        self.ncol = ncol

        b.part(name, spine_line(rig), DENY_TORSO, g.get("weights"))
        rows = R1 + R2 + 1
        idx = np.full((rows, ncol), -1, dtype=np.int64)
        angle_index = (np.arange(ncol) + self.kseam)
        theta = angle_index * self.d
        h_low = np.arange(R1 + 1) / R1
        idx[:R1 + 1] = b.add(self.point(theta[None, :], h_low[:, None]))

        top = {}
        for i in range(ncol):
            where = self._column(angle_index[i] % M)
            if where is None:
                continue
            panel, k = where
            h_up = 1.0 + np.arange(1, R2) / R2
            idx[R1 + 1:R1 + R2, i] = b.add(self.point(np.full(R2 - 1, theta[i]), h_up))
            if abs(k) >= Hn:
                if k not in top:
                    top[k] = int(b.add(self.point(np.array([theta[i]]), np.array([2.0])))[0])
                idx[R1 + R2, i] = top[k]
            else:
                idx[R1 + R2, i] = int(b.add(self.point(np.array([theta[i]]), np.array([2.0])))[0])
        self.idx = idx
        if self.open:
            self._spread_opening(angle_index)
        full = idx if self.open else np.concatenate([idx, idx[:, :1]], axis=1)
        b.grid(full, name, paint, sheet, marks={"armpit": R1})
        if g.get("lining") is not None:
            # An open garment shows its inside: the same surface again, a few millimetres in, facing inward.
            h = np.concatenate([np.arange(R1 + 1) / R1, 1.0 + np.arange(1, R2 + 1) / R2])
            normals = self.normal(theta[None, :], h[:, None])
            b.part(name + "_lining", spine_line(rig), DENY_TORSO, g.get("weights"))
            inner = np.full_like(idx, -1)
            made = {}
            # It stops short of the shoulder line, where the surface folds over and has no one inside.
            for r in range(rows - 3):
                for c in range(ncol):
                    if idx[r, c] >= 0:
                        if idx[r, c] not in made:
                            made[idx[r, c]] = int(b.add((b.positions(idx[r, c]) - normals[r, c] * 0.005)[None])[0])
                            b.follow(made[idx[r, c]], idx[r, c])
                        inner[r, c] = made[idx[r, c]]
            inner_full = inner if self.open else np.concatenate([inner, inner[:, :1]], axis=1)
            normals_full = normals if self.open else np.concatenate([normals, normals[:, :1]], axis=1)
            b.grid(inner_full, name + "_lining", g["lining"], sheet, facing=-normals_full, detail=0.45)
            b.part(name, spine_line(rig), DENY_TORSO, g.get("weights"))

        self.sleeve_end = {}
        for side in (1, -1):
            self._sleeve(side, sleeve_paint, sheet)
        for side in (1, -1):
            sp = mirror(g["shoulder"], side)
            b.relax(sp + np.array([-0.02 * side, 0.0, -0.06]), g.get("relax", 0.15), iterations=g.get("relax_iterations", 5), strength=0.55,
                    since=self.first_vertex)

    # ---- which panel a column belongs to above the armpit
    def _column(self, a):
        M, H = self.M, self.H
        if a <= H:
            return "f", a
        if a >= M - H:
            return "f", a - M
        if abs(a - M // 2) <= H:
            return "b", M // 2 - a
        return None

    def _warp(self, theta):
        # The arms sit behind the middle of the chest, so the columns under them are turned back.
        shift = self.g.get("arm_back", 0.42)
        s = np.sin(theta)
        return theta + shift * s * np.abs(s)

    def ring(self, theta, z):
        """Points of the body tube at height z; theta is the unwarped column angle."""
        g = self.g
        cy, rx, ryf, ryb, n = np.moveaxis(g["body"].at(np.ravel(z)).reshape(np.shape(z) + (5,)), -1, 0)
        c, s = superellipse(self._warp(theta), n)
        ry = np.where(c > 0.0, ryf, ryb)
        x, y = rx * s, -ry * c
        relief = g.get("relief")
        if relief is not None:
            out = unit(np.stack([x / np.maximum(rx, 1e-6) ** 2, y / np.maximum(ry, 1e-6) ** 2], axis=-1))
            amount = relief(theta, z)
            x, y = x + out[..., 0] * amount, y + out[..., 1] * amount
        return x, cy + y

    def point(self, theta, h):
        """
        The garment's surface: h runs 0..1 from the hem to the armpit and 1..2 from there to the
        shoulder line or the neckline. Works for any theta on a panel, so details can be laid on it.
        """
        g = self.g
        theta, h = np.broadcast_arrays(np.asarray(theta, dtype=np.float64), np.asarray(h, dtype=np.float64))
        low = np.minimum(h, 1.0)
        z = g["hem"] + (g["armpit"] - g["hem"]) * low
        x, y = self.ring(theta, z)
        s = np.clip(h - 1.0, 0.0, 1.0)
        if not np.any(s > 0.0):
            return np.stack([x, y, z], axis=-1)

        # Lateral position of the column on its panel, in columns from the centre line.
        wrapped = (theta + math.pi) % TWO_PI - math.pi
        front = np.abs(wrapped) < math.pi / 2
        k = np.where(front, wrapped, np.where(wrapped > 0, math.pi - wrapped, -math.pi - wrapped)) / self.d
        f = np.clip(np.abs(k), 0.0, self.H)
        sign = np.sign(k)
        xn, yn, zn = g["neck"]
        xs, ys, zs = g["shoulder"]
        q = np.clip(f / self.Hn, 0.0, 1.0)
        depth = np.where(front, -g["neck_front"][0], g["neck_back"][0])
        dip = np.where(front, g["neck_front"][1], g["neck_back"][1])
        on_neck = f <= self.Hn
        r = np.clip((f - self.Hn) / (self.H - self.Hn), 0.0, 1.0)
        sag = g.get("shoulder_sag", 0.0) * np.sin(r * math.pi)
        xt = np.where(on_neck, xn * q, xn + (xs - xn) * r) * sign
        yt = np.where(on_neck, yn + depth * np.sqrt(np.maximum(1.0 - q * q, 0.0)), yn + (ys - yn) * r)
        zt = np.where(on_neck, zn - dip * (1.0 - q * q), zn + (zs - zn) * r - sag)

        rise = np.sin(s * math.pi / 2)
        lean = np.cos(s * math.pi / 2) ** g.get("chest", 0.8)
        x2 = x + (xt - x) * s ** g.get("spread", 1.4)
        y2 = yt + (y - yt) * lean
        z2 = z + (zt - g["armpit"]) * rise
        return np.stack([x2, y2, z2], axis=-1)

    def normal(self, theta, h, step=0.01):
        """Outward normal of the surface, by differences."""
        p = self.point(theta, h)
        dt = self.point(theta + step, h) - self.point(theta - step, h)
        dh = self.point(theta, np.clip(h + step, 0, 2)) - self.point(theta, np.clip(h - step, 0, 2))
        n = unit(np.cross(dt, dh))
        centre = np.array([0.0, 0.01, 0.0])
        flip = np.sum(n[..., :2] * (p[..., :2] - centre[:2]), axis=-1) < 0.0
        # Near the shoulder line the surface faces up; there "outward" is simply upward.
        flip = np.where(np.abs(n[..., 2]) > 0.75, n[..., 2] < 0.0, flip)
        n[flip] *= -1.0
        return n

    def neck_path(self, omega, lift=0.0, grow=0.0):
        """The neckline as a closed curve: omega 0 is the front, a quarter turn the left side."""
        g = self.g
        xn, yn, zn = g["neck"]
        c, s = np.cos(omega), np.sin(omega)
        depth = np.where(c > 0, g["neck_front"][0], g["neck_back"][0]) + grow
        dip = np.where(c > 0, g["neck_front"][1], g["neck_back"][1])
        return np.stack([(xn + grow) * s, yn - depth * c, zn - dip * c * c + lift], axis=-1)

    def _spread_opening(self, angle_index):
        """Pulls the two edges of an open garment apart (or lets them overlap) along the seam."""
        b, g = self.b, self.g
        gap = g["gap"]
        reach = g.get("gap_reach", 5)
        for i in range(self.ncol):
            from_edge = min(i, self.ncol - 1 - i)
            if from_edge > reach:
                continue
            sign = 1.0 if i <= reach else -1.0   # the first columns go one way, the last the other
            falloff = smooth01(1.0 - from_edge / (reach + 1.0))
            column = self.idx[:, i]
            column = column[column >= 0]
            p = b.positions(column)
            turn = np.array([gap(z) for z in p[:, 2]])
            # The seam is at the back: moving along -X or +X opens it.
            direction = -sign if abs(self.kseam * self.d - math.pi) < 0.1 else sign
            p[:, 0] += direction * turn * falloff
            b.move(column, p)

    # ---- sleeves
    def armhole(self, side):
        """Vertex indices round the armhole: from the middle underneath, forward, over the top, back."""
        M, S, R1, R2 = self.M, self.S, self.R1, self.R2
        centre = (M // 4 if side > 0 else 3 * M // 4)
        step = -1 if side > 0 else 1   # toward the front

        def col(a):
            return (a - self.kseam) % M

        loop = [self.idx[R1, col(centre + step * j)] for j in range(S // 2 + 1)]
        front, back = col(centre + step * (S // 2)), col(centre - step * (S // 2))
        loop += [self.idx[R1 + j, front] for j in range(1, R2 + 1)]
        loop += [self.idx[R1 + j, back] for j in range(R2 - 1, -1, -1)]
        loop += [self.idx[R1, col(centre - step * j)] for j in range(S // 2 - 1, 0, -1)]
        return np.array(loop)

    def sleeve_ring(self, side, x, psi):
        """Points of the sleeve at distance x along the arm; psi 0 is underneath, a quarter turn the front."""
        sl = self.g["sleeve"]
        cy, cz, ry, rz = np.moveaxis(sl["table"].at(np.ravel(x)).reshape(np.shape(x) + (4,)), -1, 0)
        relief = sl.get("relief")
        add = relief(psi, x) if relief is not None else 0.0
        y = cy - (ry + add) * np.sin(psi)
        z = cz - (rz + add) * np.cos(psi)
        return np.stack([side * (x + 0.0 * y), y, z], axis=-1)

    def _sleeve(self, side, paint, sheet):
        b, g, rig = self.b, self.g, self.rig
        sl = g["sleeve"]
        loop = self.armhole(side)
        n = len(loop)
        b.part("%s_sleeve_%s" % (self.name, side_name(side)), arm_line(rig, side), deny_limb(side, "arm"), sl.get("weights"))
        rows = sl["rows"]
        xs = np.linspace(sl["start"], sl["end"], rows)
        xs = sl["start"] + (sl["end"] - sl["start"]) * (np.linspace(0, 1, rows) ** sl.get("bias", 1.0))
        psi = TWO_PI * np.arange(n) / n
        rings = self.sleeve_ring(side, xs[:, None], psi[None, :])
        share = np.full((rows + 1, n), -1, dtype=np.int64)
        share[0] = loop
        all_rings = np.concatenate([b.positions(loop)[None], rings], axis=0)
        marks = {"elbow": 1 + int(np.argmin(np.abs(xs - abs(rig.j("LeftForeArm")[0]))))}
        idx = b.tube(all_rings, "%s_sleeve_%s" % (self.name, side_name(side)), paint, sheet, share=share, marks=marks)
        self.sleeve_end[side] = idx[-1]


# --------------------------------------------------------------------------------------
# Trousers
# --------------------------------------------------------------------------------------
def trousers(b, rig, g, name, paint=None, sheet=0):
    """
    Two legs, each a tube from the waist to the hem. Above the crotch a leg is only its outer half
    (half of the ring round the pelvis); at the crotch the half ring closes into the leg through
    the line of vertices that runs under the body, which both legs share.

    g: M (columns round the pelvis), K (columns along the crotch line), Rp, Rl (rows),
       waist, crotch, hem (heights), dip, pelvis Table(z, cy, rx, ryf, ryb, n),
       leg Table(z, cx, cy, rx, ryf, ryb), relief(side, phi, z)
    Returns {side: indices of the hem ring}.
    """
    M, K, Rp, Rl = g["M"], g["K"], g["Rp"], g["Rl"]
    n = M // 2 + K
    zc = g["crotch"]
    hems, left_idx = {}, None
    first_vertex = len(b.pos)
    for side in (1, -1):
        b.part("%s_%s" % (name, side_name(side)), leg_line(rig, side), deny_limb(side, "leg"), g.get("weights"))
        rows = Rp + Rl + 1
        pos = np.zeros((rows, n, 3))
        present = np.zeros((rows, n), dtype=bool)
        first, last = K // 2, K // 2 + M // 2
        # Half rings round the pelvis.
        zp = np.linspace(g["waist"], zc, Rp + 1)
        cy, rx, ryf, ryb, ex = g["pelvis"].at(zp).T
        theta = math.pi * (np.arange(first, last + 1) - first) / (M // 2)
        c, s = superellipse(theta[None, :], ex[:, None])
        ry = np.where(c > 0, ryf[:, None], ryb[:, None])
        pos[:Rp + 1, first:last + 1] = np.stack([side * rx[:, None] * s, cy[:, None] - ry * c, np.broadcast_to(zp[:, None], c.shape)], axis=-1)
        present[:Rp + 1, first:last + 1] = True
        # The crotch line, from the front centre under the body to the back centre.
        front_y, back_y = cy[-1] - ryf[-1], cy[-1] + ryb[-1]
        for col in list(range(0, first)) + list(range(last + 1, n)):
            signed = col if col < first else col - n
            w = (first - signed) / K
            pos[Rp, col] = (0.0, front_y + (back_y - front_y) * w, zc - g["dip"] * math.sin(math.pi * w))
            present[Rp, col] = True
        # The leg proper.
        zl = zc + (g["hem"] - zc) * (np.arange(1, Rl + 1) / Rl) ** g.get("bias", 1.0)
        cx, cyl, lrx, lryf, lryb = g["leg"].at(zl).T
        phi = TWO_PI * np.arange(n) / n
        relief = g.get("relief")
        add = relief(side, phi[None, :], zl[:, None]) if relief is not None else 0.0
        sin_phi = np.sin(phi)[None, :]
        lry = np.where(sin_phi > 0, lryf[:, None], lryb[:, None])
        pos[Rp + 1:] = np.stack([side * (cx[:, None] - np.cos(phi)[None, :] * (lrx[:, None] + add)),
                                 cyl[:, None] - sin_phi * (lry + add),
                                 np.broadcast_to(zl[:, None], (Rl, n))], axis=-1)
        present[Rp + 1:] = True

        share = np.where(present, -1, -2)
        if side < 0:
            # The centre line and the crotch line already exist.
            share[:Rp + 1, first] = left_idx[:Rp + 1, first]
            share[:Rp + 1, last] = left_idx[:Rp + 1, last]
            for col in list(range(0, first)) + list(range(last + 1, n)):
                share[Rp, col] = left_idx[Rp, col]
        idx = np.array(share)
        fresh = idx == -1
        idx[fresh] = b.add(pos[fresh])
        idx[idx == -2] = -1
        if side > 0:
            left_idx = idx
        full = np.concatenate([idx, idx[:, :1]], axis=1)
        knee = Rp + 1 + int(np.argmin(np.abs(zl - rig.j("LeftLeg")[2])))
        b.grid(full, "%s_%s" % (name, side_name(side)), paint, sheet, marks={"crotch": Rp, "knee": knee})
        hems[side] = idx[-1]
    b.relax(np.array([0.0, 0.5 * (front_y + back_y), zc - 0.02]), g.get("relax", 0.14), iterations=5, strength=0.5, since=first_vertex)
    return hems


# --------------------------------------------------------------------------------------
# Feet
# --------------------------------------------------------------------------------------
def foot(b, rig, side, g, name, paint=None, sheet=0):
    """
    A leg and foot in one tube, like a sock: level rings down the leg, rings that fan round the
    front of the ankle and take the heel with them, then upright rings forward to the toe.

    g: top, rows_leg, leg Table(z, cx, cy, rx, ryf, ryb), floor (height of the underside),
       heel (how far the heel reaches behind the ankle), instep (height of the fan's pivot),
       half_width(u), top_height(u) with u 0 at the pivot and 1 at the toe tip, length
       cols, rows_bend, rows_foot
    Returns the indices of the top ring.
    """
    ankle, toe = rig.j("LeftFoot"), rig.j("LeftToeBase")
    b.part(name, leg_line(rig, side), deny_limb(side, "leg"), g.get("weights"))
    cols = g["cols"]
    omega = TWO_PI * np.arange(cols) / cols   # 0 is the back of the leg, which becomes the sole
    rings = []

    # Leg: level rings. cos(omega) runs back (+Y), sin(omega) to the outside.
    zl = np.linspace(g["top"], g["instep"], g["rows_leg"], endpoint=False)
    cx, cy, rx, ryf, ryb = g["leg"].at(zl).T
    relief = g.get("relief")
    for j, z in enumerate(zl):
        co, so = np.cos(omega), np.sin(omega)
        ry = np.where(co > 0, ryb[j], ryf[j])
        add = relief(omega, z) if relief is not None else 0.0
        rings.append(np.stack([cx[j] + (rx[j] + add) * so, cy[j] + (ry + add) * co, np.full(cols, z)], axis=-1))

    # Bend: the rings fan about a line just in front of the ankle, `rho` away from the skin, so
    # their near ends trace the hollow where the leg turns into the top of the foot.
    cx0, cy0, rx0, ryf0, ryb0 = g["leg"].at([g["instep"]])[0]
    rho = g.get("rho", 0.02)
    pivot_y, pivot_z = cy0 - ryf0 - rho, g["instep"]
    back = cy0 + ryb0 - pivot_y
    drop = pivot_z - g["floor"]
    alphas = np.linspace(0.0, math.pi / 2, g["rows_bend"] + 1)
    for alpha in alphas:
        e = np.array([0.0, math.cos(alpha), -math.sin(alpha)])
        t = alpha / (math.pi / 2)
        reach_back = (back + g["heel"] * smooth01(t * 3.0)) / max(math.cos(alpha), 1.0e-3)
        reach_down = drop / max(math.sin(alpha), 1.0e-3)
        far = (reach_back ** -5 + reach_down ** -5) ** (-1.0 / 5)
        # The heel is narrower than the ankle above it and the arch in front of it.
        half_w = rx0 * (1.0 - t) + g["half_width"](0.0) * t
        half_w *= 1.0 - g.get("heel_pinch", 0.10) * math.sin(t * math.pi)
        centre = np.array([cx0, pivot_y, pivot_z]) + e * (rho + far) * 0.5
        a = (far - rho) * 0.5
        expo = np.where(np.cos(omega) > 0, 2.0 + 2.0 * t, 2.0 + 0.4 * t)
        co, so = superellipse(omega, expo)
        rings.append(centre[None] + a * co[:, None] * e[None] + half_w * so[:, None] * np.array([1.0, 0.0, 0.0])[None])

    # Foot: upright rings from the pivot to the toe.
    length = g["length"]
    us = (np.arange(1, g["rows_foot"] + 1) / g["rows_foot"]) ** 0.9
    for u in us:
        y = pivot_y - length * u
        top = g["top_height"](u)
        w = g["half_width"](u)
        centre = np.array([cx0 + (toe[0] - ankle[0]) * u + g.get("toe_out", 0.0) * u, y, 0.5 * (top + g["floor"])])
        a = 0.5 * (top - g["floor"])
        expo = np.where(np.cos(omega) > 0, 4.0, 2.4)
        co, so = superellipse(omega, expo)
        rings.append(centre[None] + a * co[:, None] * np.array([0.0, 0.0, -1.0])[None] + w * so[:, None] * np.array([1.0, 0.0, 0.0])[None])
    rings = np.array(rings)
    tip = rings[-1].mean(axis=0) + np.array([0.0, -g.get("tip", 0.012), 0.0])
    rings = mirror(rings, side)
    tip = mirror(tip, side)
    marks = {"ankle": g["rows_leg"], "knee": int(np.argmin(np.abs(zl - rig.j("LeftLeg")[2])))}
    idx = b.tube(rings, name, paint, sheet, cap_end=tip, marks=marks)
    return idx[0]


def sole(b, rig, side, g, name, paint=None, sheet=0):
    """
    The slab under a shoe. g: back, front (y of the heel and of the toe), cx(u), half_width(u)
    with u 0 at the heel and 1 at the toe, thickness, heel (extra height under the heel), cols.
    """
    b.part(name, leg_line(rig, side), deny_limb(side, "leg"), g.get("weights"))
    cols = g["cols"]
    tau = TWO_PI * np.arange(cols) / cols
    c, s = superellipse(tau, 2.6)
    u = 0.5 - 0.5 * c            # 0 at the heel (+Y end)
    y = g["back"] + (g["front"] - g["back"]) * u
    x = np.array([g["cx"](v) for v in u]) + np.array([g["half_width"](v) for v in u]) * np.sign(s) * np.abs(s) ** 0.9
    outline = np.stack([x, y, np.zeros(cols)], axis=-1)
    centre = outline.mean(axis=0)
    h = g["thickness"]

    def ring(scale, z):
        p = centre + (outline - centre) * scale
        p[:, 2] = z
        return p

    rings = np.array([ring(0.93, 0.0), ring(1.0, 0.006), ring(1.0, h - 0.004), ring(0.97, h), ring(0.80, h + 0.001)])
    rings = mirror(rings, side)
    bottom = mirror(centre + np.array([0.0, 0.0, 0.0]), side)
    b.tube(rings, name, paint, sheet, cap_start=bottom, facing=None)


# --------------------------------------------------------------------------------------
# Limbs of bare skin
# --------------------------------------------------------------------------------------
def arm(b, rig, side, g, name, paint=None, sheet=0):
    """
    An arm as a tube along the bone. g: Table(x, cy, cz, ry, rz) as `table`, start, end, rows,
    cols, relief(psi, x). Returns (first ring, last ring).
    """
    b.part(name, arm_line(rig, side), deny_limb(side, "arm"), g.get("weights"))
    xs = np.linspace(g["start"], g["end"], g["rows"])
    psi = TWO_PI * np.arange(g["cols"]) / g["cols"]
    cy, cz, ry, rz = g["table"].at(xs).T
    relief = g.get("relief")
    add = relief(psi[None, :], xs[:, None]) if relief is not None else 0.0
    rings = np.stack([side * np.broadcast_to(xs[:, None], (len(xs), len(psi))),
                      cy[:, None] - (ry[:, None] + add) * np.sin(psi)[None, :],
                      cz[:, None] - (rz[:, None] + add) * np.cos(psi)[None, :]], axis=-1)
    elbow = int(np.argmin(np.abs(xs - abs(rig.j("LeftForeArm")[0]))))
    idx = b.tube(rings, name, paint, sheet, marks={"elbow": elbow})
    return idx[0], idx[-1]


# --------------------------------------------------------------------------------------
# Hands
# --------------------------------------------------------------------------------------
def hand(b, rig, side, g, name, paint=None, sheet=0):
    """
    A palm and five fingers built along the finger bones. g: thick (scale of every radius),
    long (scale of the finger length beyond the knuckle), wrist (how far the palm reaches up the
    forearm), wrist_radius (ry, rz)
    """
    b.part(name, None, deny_limb(side, "arm") + ("Shoulder", "Spine"), g.get("weights"))
    thick = g.get("thick", 1.0)
    wrist = rig.j("LeftHand")
    fingers = ("Index", "Middle", "Ring", "Pinky")
    knuckles = np.array([rig.j("LeftHand%s1" % f) for f in fingers])
    knuckle_x = knuckles[:, 0].mean()
    y_mid = 0.5 * (knuckles[0, 1] + knuckles[-1, 1])
    half_span = 0.5 * (knuckles[-1, 1] - knuckles[0, 1]) + 0.0115 * thick
    wy, wz = g.get("wrist_radius", (0.030, 0.021))
    x0 = wrist[0] - g.get("wrist", 0.03)
    table = Table([
        # x, centre y, centre z, half width (Y), half thickness (Z), exponent
        (x0, wrist[1], wrist[2], wy, wz, 2.2),
        (wrist[0] + 0.004, wrist[1], wrist[2], wy * 0.98, wz * 0.9, 2.3),
        (wrist[0] + 0.045, 0.5 * (wrist[1] + y_mid) - 0.004, wrist[2] - 0.002, half_span * 0.97, 0.0165 * thick, 2.7),
        (knuckle_x - 0.012, y_mid, wrist[2] - 0.001, half_span, 0.0145 * thick, 3.0),
        (knuckle_x + 0.006, y_mid, wrist[2] - 0.001, half_span * 0.97, 0.0115 * thick, 3.0),
    ])
    rows, cols = 9, 16
    xs = np.linspace(x0, knuckle_x + 0.006, rows)
    cy, cz, hw, ht, ex = table.at(xs).T
    phi = TWO_PI * np.arange(cols) / cols
    c, s = superellipse(phi[None, :], ex[:, None])
    # A fuller ball of the thumb: the palm is thicker on the thumb side (front), underneath.
    thenar = 0.006 * thick * bump(xs, wrist[0] + 0.04, 0.03)[:, None] * np.clip(-c, 0, 1) * np.clip(-s, 0, 1) * 4.0
    rings = np.stack([np.broadcast_to(xs[:, None], c.shape), cy[:, None] + hw[:, None] * c, cz[:, None] + (ht[:, None] + thenar) * s], axis=-1)
    cap = np.array([knuckle_x + 0.012, y_mid, wrist[2] - 0.001])
    palm = b.tube(mirror(rings, side), name + "_palm", paint, sheet, cap_end=mirror(cap, side))

    radii = {"Index": 0.0098, "Middle": 0.0101, "Ring": 0.0094, "Pinky": 0.0084}
    long = g.get("long", 1.0)
    for f in fingers:
        joints = [rig.j("LeftHand%s%d" % (f, k)) for k in (1, 2, 3)] + [rig.head["LeftHand%s4" % f]]
        base = joints[0]
        joints = [base + (p - base) * long for p in joints]
        r = radii[f] * thick
        centres = [base - np.array([0.016, 0, 0]), base + np.array([0.004, 0, 0]),
                   0.5 * (joints[0] + joints[1]), joints[1], 0.5 * (joints[1] + joints[2]), joints[2],
                   0.5 * (joints[2] + joints[3]), joints[3] - np.array([r * 0.8, 0, 0])]
        rs = [r * 0.98, r * 1.03, r * 0.95, r * 1.02, r * 0.9, r * 0.95, r * 0.84, r * 0.80]
        sweep(b, mirror(centres, side), rs, 8, "%s_%s" % (name, f), paint, sheet, rows_per_segment=1, squash=0.9, tip=0.9)

    t = [rig.j("LeftHandThumb%d" % k) for k in (1, 2, 3)] + [rig.head["LeftHandThumb4"]]
    d = unit(t[1] - t[0])
    r = 0.0118 * thick
    centres = [t[0] - d * 0.022 + np.array([0, 0.008, 0.004]), t[0], 0.5 * (t[0] + t[1]), t[1], 0.5 * (t[1] + t[2]), t[2],
               0.5 * (t[2] + t[3]), t[3] - d * r * 0.7]
    rs = [r * 1.45, r * 1.35, r * 1.15, r * 1.08, r * 0.98, r * 1.0, r * 0.9, r * 0.84]
    sweep(b, mirror(centres, side), rs, 8, name + "_Thumb", paint, sheet, rows_per_segment=1, squash=0.92, tip=0.9,
          up=(0.0, -0.5, 0.85))
    return palm[0]


# --------------------------------------------------------------------------------------
# Head
# --------------------------------------------------------------------------------------
def head(b, rig, g, name, paint=None, sheet=0):
    """
    Level slices from the base of the neck to the crown. g: slices Table(z, cy, rx, ryf, ryb, n),
    bottom, top, rows, cols, relief(theta, z). The texture is cut down the back.
    Returns a function point(theta, z, lift) on the surface, for whatever is worn on the head.
    """
    b.part(name, None, DENY_HEAD, g.get("weights"))
    cols, rows = g["cols"], g["rows"]
    theta = math.pi + TWO_PI * np.arange(cols) / cols

    def point(th, z, lift=0.0):
        th, z = np.broadcast_arrays(np.asarray(th, dtype=np.float64), np.asarray(z, dtype=np.float64))
        cy, rx, ryf, ryb, n = np.moveaxis(g["slices"].at(np.ravel(z)).reshape(np.shape(z) + (5,)), -1, 0)
        c, s = superellipse(th, n)
        ry = np.where(c > 0, ryf, ryb)
        relief = g.get("relief")
        add = lift + (relief(th, z) if relief is not None else 0.0)
        x, y = rx * s, -ry * c
        out = unit(np.stack([x / np.maximum(rx, 1e-6) ** 2, y / np.maximum(ry, 1e-6) ** 2], axis=-1))
        return np.stack([x + out[..., 0] * add, cy + y + out[..., 1] * add, z], axis=-1)

    # Rows crowd toward the crown, where the slices shrink fast.
    u = np.linspace(0.0, 1.0, rows)
    zs = g["bottom"] + (g["top"] - g["bottom"]) * np.sin(u * math.pi / 2) ** g.get("crowd", 1.0)
    rings = point(theta[None, :], zs[:, None])
    crown = np.array([0.0, float(g["slices"].at([g["top"]])[0, 0]), g["top"] + g.get("dome", 0.004)])
    marks = {key: int(np.argmin(np.abs(zs - z))) for key, z in dict(g.get("marks", {}), jaw=rig.j("Head")[2]).items()}
    b.tube(rings, name, paint, sheet, cap_end=crown, marks=marks)
    return point


def shell(b, point, name, z0, z1, rows, cols, lift, paint=None, sheet=0, brim=None, crown=None, seam=math.pi, tilt=None):
    """
    Something worn close on the head (a beanie, a cap, a wrap of bandage): the head's surface from
    z0 to z1 raised by `lift` (a number or a function of theta and the 0..1 height). `tilt(theta)`
    lowers or raises the bottom edge round the head. `crown` closes the top at that height.
    """
    theta = seam + TWO_PI * np.arange(cols) / cols
    u = np.linspace(0.0, 1.0, rows)
    low = z0 + (tilt(theta) if tilt is not None else np.zeros_like(theta))
    climb = np.sin(u * math.pi / 2) if crown is not None else u   # rows crowd toward a closed top
    z = low[None, :] + (z1 - low[None, :]) * climb[:, None]
    amount = lift(theta[None, :], u[:, None]) if callable(lift) else lift
    rings = point(theta[None, :], z, amount)
    cap = None
    if crown is not None:
        cap = np.array([0.0, rings[-1][:, 1].mean(), crown])
    return b.tube(rings, name, paint, sheet, cap_end=cap)
