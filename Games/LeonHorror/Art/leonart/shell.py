"""
Room shells: the floor, ceiling, walls and skirting of every room of the plan.

The plan is Games/LeonHorror/Scripts/clinic_layout.py. Each room becomes four meshes, modelled in
plan coordinates (so the map builder places them all at the origin) and seen only from inside:

    SM_Clinic_<Room>_Floor      one face, plus the thresholds of its doorways
    SM_Clinic_<Room>_Ceiling
    SM_Clinic_<Room>_Walls      the four inner faces with the openings cut out and their reveals;
                                a dado in a second material where the plan asks for one
    SM_Clinic_<Room>_Skirting   a strip along the foot of the walls, broken at the doorways

A wall between two rooms is two such faces, each set back from the shared edge by half the wall
thickness; the reveals of an opening reach the edge from both sides and meet there.
"""

import os
import sys

from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "Scripts"))

import clinic_layout as plan  # noqa: E402

SKIRTING = "ClinicRubber"
SKIRTING_HEIGHT = 0.10
SKIRTING_DEPTH = 0.012


def mesh_name(room, part):
    return "SM_Clinic_%s_%s" % (room, part)


class Side:
    """One wall of a room: where its inner face is and how plan coordinates map onto it."""

    def __init__(self, room, side):
        x0, y0, x1, y1 = plan.ROOMS[room]["rect"]
        t = plan.WALL_THICKNESS * 0.5
        self.side = side
        self.t = t
        if side in ("south", "north"):
            self.low, self.high = x0 + t, x1 - t
            self.face = y0 + t if side == "south" else y1 - t
            self.out = -1.0 if side == "south" else 1.0
        else:
            self.low, self.high = y0 + t, y1 - t
            self.face = x0 + t if side == "west" else x1 - t
            self.out = -1.0 if side == "west" else 1.0

    def point(self, along, depth, z):
        """`along` in plan coordinates, `depth` metres behind the inner face (into the wall)."""
        across = self.face + self.out * depth
        return Vector((along, across, z)) if self.side in ("south", "north") else Vector((across, along, z))

    def inward(self):
        """Unit vector from the wall into the room."""
        return Vector((0.0, -self.out, 0.0)) if self.side in ("south", "north") else Vector((-self.out, 0.0, 0.0))

    def along_axis(self):
        return Vector((1.0, 0.0, 0.0)) if self.side in ("south", "north") else Vector((0.0, 1.0, 0.0))


def facing_quad(m, corners, toward, material):
    """A quad wound so that it looks along `toward`."""
    a, b, c, d = [Vector(corner) for corner in corners]
    if (b - a).cross(d - a).dot(Vector(toward)) < 0.0:
        a, b, c, d = d, c, b, a
    return m.quad([a, b, c, d], material)


def wall_face(m, side, z0, z1, material, holes):
    """The inner face of one wall between two heights, minus the openings (along0, along1, top)."""
    spans = [(side.low, side.high)]
    # Cut the wall into columns at the edges of the openings; a column inside an opening only
    # keeps what is above it.
    cuts = sorted({side.low, side.high} | {min(max(h[0], side.low), side.high) for h in holes}
                  | {min(max(h[1], side.low), side.high) for h in holes})
    del spans
    for i in range(len(cuts) - 1):
        a0, a1 = cuts[i], cuts[i + 1]
        if a1 - a0 < 1.0e-6:
            continue
        middle = (a0 + a1) * 0.5
        bottom = z0
        for h0, h1, top in holes:
            if h0 < middle < h1:
                bottom = max(bottom, top)
        if bottom >= z1 - 1.0e-6:
            continue
        facing_quad(m, [side.point(a0, 0.0, bottom), side.point(a1, 0.0, bottom), side.point(a1, 0.0, z1), side.point(a0, 0.0, z1)],
                    side.inward(), material)


def build_walls(m, room):
    spec = plan.ROOMS[room]
    wall, dado, dado_height = spec["wall"], spec.get("dado"), spec.get("dado_height", 0.0)
    for name in ("south", "north", "west", "east"):
        side = Side(room, name)
        openings = [o for o in plan.openings_of(room) if o[0] == name]
        holes = [(at - width * 0.5, at + width * 0.5, height) for _, at, width, height, _ in openings]
        if dado:
            wall_face(m, side, 0.0, dado_height, dado, holes)
            wall_face(m, side, dado_height, plan.ROOM_HEIGHT, wall, holes)
        else:
            wall_face(m, side, 0.0, plan.ROOM_HEIGHT, wall, holes)

        # Reveals: the cut through the wall, as far as the edge shared with the next room.
        axis = side.along_axis()
        for a0, a1, top in holes:
            for along, toward in ((a0, axis), (a1, -axis)):
                segments = [(0.0, min(top, dado_height), dado), (min(top, dado_height), top, wall)] if dado else [(0.0, top, wall)]
                for z0, z1, material in segments:
                    if z1 - z0 > 1.0e-6:
                        facing_quad(m, [side.point(along, 0.0, z0), side.point(along, side.t, z0), side.point(along, side.t, z1),
                                        side.point(along, 0.0, z1)], toward, material)
            head = dado if dado and top <= dado_height else wall
            facing_quad(m, [side.point(a0, 0.0, top), side.point(a1, 0.0, top), side.point(a1, side.t, top), side.point(a0, side.t, top)],
                        (0.0, 0.0, -1.0), head)


def build_floor(m, room):
    spec = plan.ROOMS[room]
    x0, y0, x1, y1 = spec["rect"]
    t = plan.WALL_THICKNESS * 0.5
    m.panel((x0 + t, y0 + t, 0.0), (1, 0, 0), (0, 1, 0), (x1 - x0) - 2 * t, (y1 - y0) - 2 * t, spec["floor"])
    # Thresholds: the floor carries on under each doorway to the middle of the wall.
    for name, at, width, _, _ in plan.openings_of(room):
        side = Side(room, name)
        facing_quad(m, [side.point(at - width * 0.5, 0.0, 0.0), side.point(at + width * 0.5, 0.0, 0.0),
                        side.point(at + width * 0.5, side.t, 0.0), side.point(at - width * 0.5, side.t, 0.0)], (0.0, 0.0, 1.0), spec["floor"])


def build_ceiling(m, room):
    spec = plan.ROOMS[room]
    x0, y0, x1, y1 = spec["rect"]
    t = plan.WALL_THICKNESS * 0.5
    m.panel((x0 + t, y0 + t, plan.ROOM_HEIGHT), (0, 1, 0), (1, 0, 0), (y1 - y0) - 2 * t, (x1 - x0) - 2 * t, spec["ceiling"])


def build_skirting(m, room):
    for name in ("south", "north", "west", "east"):
        side = Side(room, name)
        holes = sorted((at - width * 0.5, at + width * 0.5) for s, at, width, _, _ in plan.openings_of(room) if s == name)
        cursor = side.low
        runs = []
        for h0, h1 in holes:
            if h0 - cursor > 0.02:
                runs.append((cursor, h0))
            cursor = h1
        if side.high - cursor > 0.02:
            runs.append((cursor, side.high))
        for a0, a1 in runs:
            # The face towards the room and the top; nothing else of it can be seen.
            facing_quad(m, [side.point(a0, -SKIRTING_DEPTH, 0.0), side.point(a1, -SKIRTING_DEPTH, 0.0),
                            side.point(a1, -SKIRTING_DEPTH, SKIRTING_HEIGHT), side.point(a0, -SKIRTING_DEPTH, SKIRTING_HEIGHT)],
                        side.inward(), SKIRTING)
            facing_quad(m, [side.point(a0, -SKIRTING_DEPTH, SKIRTING_HEIGHT), side.point(a1, -SKIRTING_DEPTH, SKIRTING_HEIGHT),
                            side.point(a1, 0.0, SKIRTING_HEIGHT), side.point(a0, 0.0, SKIRTING_HEIGHT)], (0.0, 0.0, 1.0), SKIRTING)


BUILDERS = {"Floor": (build_floor, 20.0), "Ceiling": (build_ceiling, 9.0), "Walls": (build_walls, 16.0), "Skirting": (build_skirting, 6.0)}


def register(pieces):
    """Adds every room's four meshes to the kit's table of pieces."""
    plan.validate()
    for room in plan.ROOMS:
        for part, (builder, texels) in BUILDERS.items():
            pieces[mesh_name(room, part)] = (lambda m, room=room, builder=builder: builder(m, room), 0.0, texels)
