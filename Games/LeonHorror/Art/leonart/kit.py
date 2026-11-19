"""
The pieces of the level.

Every function builds one mesh around its own origin, so the level builder can place it with a
position and a yaw. Sizes are real: a door is 0.9 by 2.05 m, a seat is 0.45 m high. Blender axes
(X right, Y back, Z up); what "front" means is said per piece.

Register a piece with @piece(name). `run` builds the requested ones, exports them and renders a
review sheet.
"""

import os

from . import materials as material_library
from . import mesh as meshes
from . import sheet
from . import shell

PIECES = {}

WALL = "ClinicPlasterGreen"
FLOOR = "ClinicVinylFloor"
CEILING = "ClinicCeilingTile"
TRIM = "ClinicRubber"
FRAME = "ClinicPaintedMetal"
DOOR = "ClinicDoorLaminate"
STEEL = "ClinicSteel"
LIGHT = "ClinicLightDiffuser"

DOOR_WIDTH = 0.9
DOOR_HEIGHT = 2.05
ROOM_HEIGHT = 2.8


def piece(name, bevel=0.0, texels_per_metre=24.0):
    def register(function):
        PIECES[name] = (function, bevel, texels_per_metre)
        return function

    return register


# --------------------------------------------------------------------------------------------------
# Test corridor (plan step 7): 8 m long along X, 2.4 m wide along Y, door in the far end wall.
# --------------------------------------------------------------------------------------------------
CORRIDOR = (8.0, 2.4)


@piece("SM_TestCorridor_Floor", texels_per_metre=20.0)
def corridor_floor(m):
    m.panel((0, 0, 0), (1, 0, 0), (0, 1, 0), CORRIDOR[0], CORRIDOR[1], FLOOR)


@piece("SM_TestCorridor_Ceiling", texels_per_metre=14.0)
def corridor_ceiling(m):
    m.panel((0, 0, ROOM_HEIGHT), (0, 1, 0), (1, 0, 0), CORRIDOR[1], CORRIDOR[0], CEILING)


@piece("SM_TestCorridor_Walls", texels_per_metre=18.0)
def corridor_walls(m):
    length, width = CORRIDOR
    m.panel((length, 0, 0), (-1, 0, 0), (0, 0, 1), length, ROOM_HEIGHT, WALL)  # y = 0, looking +Y
    m.panel((0, width, 0), (1, 0, 0), (0, 0, 1), length, ROOM_HEIGHT, WALL)  # y = width, looking -Y
    m.panel((0, 0, 0), (0, 1, 0), (0, 0, 1), width, ROOM_HEIGHT, WALL)  # x = 0, looking +X
    # The far wall, with the doorway in its middle; its reveal is 15 cm of wall thickness.
    left = (width - DOOR_WIDTH) * 0.5
    hole = (left, 0.0, left + DOOR_WIDTH, DOOR_HEIGHT)
    m.panel((length, width, 0), (0, -1, 0), (0, 0, 1), width, ROOM_HEIGHT, WALL, holes=[hole])
    y0, y1 = width - hole[2], width - hole[0]
    m.quad([(length, y0, 0), (length + 0.15, y0, 0), (length + 0.15, y0, DOOR_HEIGHT), (length, y0, DOOR_HEIGHT)], WALL)
    m.quad([(length + 0.15, y1, 0), (length, y1, 0), (length, y1, DOOR_HEIGHT), (length + 0.15, y1, DOOR_HEIGHT)], WALL)
    m.quad([(length, y0, DOOR_HEIGHT), (length + 0.15, y0, DOOR_HEIGHT), (length + 0.15, y1, DOOR_HEIGHT), (length, y1, DOOR_HEIGHT)],
           WALL)


@piece("SM_TestCorridor_Skirting", bevel=0.003, texels_per_metre=10.0)
def corridor_skirting(m):
    length, width = CORRIDOR
    high, thick = 0.10, 0.012
    left = (width - DOOR_WIDTH) * 0.5
    m.box((0, 0, 0), (length, thick, high), TRIM, skip="yz")
    m.box((0, width - thick, 0), (length, width, high), TRIM, skip="Yz")
    m.box((0, thick, 0), (thick, width - thick, high), TRIM, skip="xz")
    m.box((length - thick, thick, 0), (length, left, high), TRIM, skip="Xz")
    m.box((length - thick, left + DOOR_WIDTH, 0), (length, width - thick, high), TRIM, skip="Xz")


# --------------------------------------------------------------------------------------------------
# Doors. The frame's origin is the middle of the threshold; the leaf's origin is its hinge edge at
# floor level, so turning the actor swings the door. Both lie in the XZ plane, front towards -Y.
# --------------------------------------------------------------------------------------------------


@piece("SM_DoorFrame_Single", bevel=0.004)
def door_frame_single(m):
    half, depth, face = DOOR_WIDTH * 0.5, 0.16, 0.06
    # Jambs and head wrap the wall's thickness; a stop runs around the inside for the leaf to close on.
    m.box((-half - face, -depth * 0.5, 0), (-half, depth * 0.5, DOOR_HEIGHT + face), FRAME, skip="z")
    m.box((half, -depth * 0.5, 0), (half + face, depth * 0.5, DOOR_HEIGHT + face), FRAME, skip="z")
    m.box((-half, -depth * 0.5, DOOR_HEIGHT), (half, depth * 0.5, DOOR_HEIGHT + face), FRAME)
    stop = 0.015
    m.box((-half, 0.01, 0), (-half + stop, 0.035, DOOR_HEIGHT), FRAME, skip="z")
    m.box((half - stop, 0.01, 0), (half, 0.035, DOOR_HEIGHT), FRAME, skip="z")
    m.box((-half + stop, 0.01, DOOR_HEIGHT - stop), (half - stop, 0.035, DOOR_HEIGHT), FRAME)


@piece("SM_Door_Single", bevel=0.002)
def door_single(m):
    thick = 0.044
    clear = 0.004  # gap to the frame
    width = DOOR_WIDTH - 2 * clear
    m.box((clear, -thick * 0.5, 0.008), (clear + width, thick * 0.5, DOOR_HEIGHT - clear), DOOR)
    # Kick plates on both faces.
    for side in (-1, 1):
        y0, y1 = sorted((side * thick * 0.5, side * (thick * 0.5 + 0.0015)))
        m.box((clear + 0.03, y0, 0.03), (clear + width - 0.03, y1, 0.28), STEEL)
    # Lever handles: a rose, a neck and a lever on each face, 1.05 m up, 6 cm from the free edge.
    hx, hz = clear + width - 0.06, 1.05
    for side in (-1, 1):
        face = side * thick * 0.5
        m.cylinder((hx, face, hz), (0, side, 0), 0.026, 0.008, STEEL, segments=20)
        m.cylinder((hx, face, hz), (0, side, 0), 0.010, 0.05, STEEL, segments=12)
        m.cylinder((hx + 0.008, face + side * 0.045, hz), (-1, 0, 0), 0.009, 0.12, STEEL, segments=12)
    # Three hinges on the hinge edge.
    for hz in (0.25, 1.05, 1.80):
        m.cylinder((0.0, -thick * 0.5 - 0.006, hz - 0.05), (0, 0, 1), 0.007, 0.10, STEEL, segments=10)


# --------------------------------------------------------------------------------------------------
# Props
# --------------------------------------------------------------------------------------------------


@piece("SM_Prop_Bench", bevel=0.004)
def prop_bench(m):
    """Three-seat waiting bench: a steel beam on two legs carrying moulded seats. Front is -Y."""
    width, seat_height, seat_depth = 1.65, 0.44, 0.44
    # Legs: a foot bar on the floor and a post up to the beam.
    for x in (0.18, width - 0.18):
        m.box((x - 0.025, -0.25, 0.0), (x + 0.025, 0.25, 0.03), STEEL)
        m.box((x - 0.022, -0.03, 0.03), (x + 0.022, 0.03, seat_height - 0.09), STEEL, skip="zZ")
    m.box((0.05, -0.04, seat_height - 0.09), (width - 0.05, 0.04, seat_height - 0.04), STEEL)
    # Seats and backs, slightly apart.
    seat_width = 0.50
    for i in range(3):
        x0 = 0.06 + i * (seat_width + 0.02)
        m.box((x0, -seat_depth * 0.55, seat_height - 0.04), (x0 + seat_width, seat_depth * 0.45, seat_height), FRAME)
        m.box((x0, seat_depth * 0.45 - 0.03, seat_height + 0.06), (x0 + seat_width, seat_depth * 0.45, seat_height + 0.42), FRAME)
        m.box((x0 + seat_width * 0.5 - 0.02, seat_depth * 0.45 - 0.045, seat_height - 0.02),
              (x0 + seat_width * 0.5 + 0.02, seat_depth * 0.45 - 0.03, seat_height + 0.12), STEEL)


@piece("SM_Light_Panel", bevel=0.002)
def light_panel(m):
    """A 60 cm troffer for the ceiling grid: a pressed steel tray and its diffuser. Hangs from its origin."""
    half = 0.297
    m.box((-half, -half, -0.012), (half, half, 0.0), FRAME, skip="z")
    m.box((-half + 0.025, -half + 0.025, -0.016), (half - 0.025, half - 0.025, -0.012), LIGHT, skip="Z")


# --------------------------------------------------------------------------------------------------


shell.register(PIECES)

from . import props  # noqa: E402,F401  (registers the furniture)


def run(args):
    names = args["only"] or sorted(PIECES)
    folder = os.path.join(args["out"], "Raw", "Meshes")
    textures = os.path.join(args["out"], "Raw", "Textures")
    meshes.reset_scene()
    built = []
    for name in names:
        if name not in PIECES:
            raise SystemExit("unknown piece '%s'; known: %s" % (name, ", ".join(sorted(PIECES))))
        function, bevel, texels = PIECES[name]
        builder = meshes.Mesh(name)
        function(builder)
        obj = builder.finish(bevel=bevel)
        info = meshes.export(obj, folder, texels)
        built.append(obj)
        print("PIECE %-28s %6d tris  %7.2f m2  lightmap %4d  %s" % (name, info["triangles"], info["area"], info["lightmap_resolution"],
                                                                    ",".join(info["materials"])))

    used = {slot.material.name[2:] for obj in built for slot in obj.material_slots if slot.material}
    for name in used:
        if name in material_library.FLAT:
            sheet.flat_material(name, material_library.FLAT[name])
        else:
            sheet.preview_material(name, textures)
    # Small things would vanish beside an eight metre corridor: one sheet for each size class.
    def longest(obj):
        # From the vertices: a new object's bounding box is not filled in until the scene updates.
        points = [vertex.co for vertex in obj.data.vertices]
        return max(max(p[i] for p in points) - min(p[i] for p in points) for i in range(3))

    # The room shells are shown together where they stand, from above and without their ceilings.
    shells = [obj for obj in built if obj.name.startswith("SM_Clinic_")]
    others = [obj for obj in built if not obj.name.startswith("SM_Clinic_")]
    if shells:
        print("SHEET", sheet.render_plan(shells, os.path.join(args["out"], "Sheets", "Clinic_Plan.png")))
    large = [obj for obj in others if longest(obj) > 3.0]
    small = [obj for obj in others if longest(obj) <= 3.0]
    for label, group in (("Large", large), ("Small", small)):
        # Seven to a picture: more and each one is too small to judge.
        for page, start in enumerate(range(0, len(group), 7)):
            name = "Meshes_%s_%d.png" % (label, page + 1)
            print("SHEET", sheet.render(group[start:start + 7], os.path.join(args["out"], "Sheets", name)))
