"""
The clinic's floor plan, as data.

Both the art build (which models each room's shell in Blender) and the map builder (which places
them, the doors, the lights and everything else) read this file, so the plan is written once.

Plan coordinates are Blender's: X runs east, Y runs north, Z is up, metres. A room is a rectangle
on the plan; neighbouring rooms share an edge, and the wall between them is modelled as two faces
set back from that edge by half the wall thickness.

    y
    32.4 +-----------+-------+---------+
         | Theatre   | Morgue| Plant   |
    26.4 +-----------+-------+---------+----+
         |        Service corridor     |Bay |
    24.0 +--------+--+-----------------+----+
         | Staff  |  | Storage   |
         +--------+C +-----------+
         |Pharmacy|o | Toilets   |
         +--------+r +-----------+
         |Consult2|r | X-ray     |
         +--------+i +-----------+
         |Consult1|d | Ward      |
     6.0 +--------+--+-----------+
         | Reception | Waiting   |
     0.0 +-----------+-----------+   x
         0        6  8.4      14.4  18
"""

WALL_THICKNESS = 0.15
ROOM_HEIGHT = 2.8
DOOR_WIDTH = 0.9
DOOR_HEIGHT = 2.05
DOUBLE_DOOR_WIDTH = 1.6

# name: (x0, y0, x1, y1), finishes. `dado` is a second wall material up to `dado_height`.
ROOMS = {
    "Reception": {"rect": (0.0, 0.0, 8.4, 6.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterGreen", "ceiling": "ClinicCeilingTile"},
    "Waiting": {"rect": (8.4, 0.0, 14.4, 6.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterGreen", "ceiling": "ClinicCeilingTile"},
    "Corridor": {"rect": (6.0, 6.0, 8.4, 24.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterGreen", "ceiling": "ClinicCeilingTile"},
    "Consult1": {"rect": (0.0, 6.0, 6.0, 10.5), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterWhite", "ceiling": "ClinicCeilingTile"},
    "Consult2": {"rect": (0.0, 10.5, 6.0, 15.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterWhite", "ceiling": "ClinicCeilingTile"},
    "Pharmacy": {"rect": (0.0, 15.0, 6.0, 19.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterWhite", "ceiling": "ClinicCeilingTile",
                 "dado": "ClinicWallTile", "dado_height": 1.5},
    "Staff": {"rect": (0.0, 19.0, 6.0, 24.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterWhite", "ceiling": "ClinicCeilingTile"},
    "Ward": {"rect": (8.4, 6.0, 14.4, 13.0), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterGreen", "ceiling": "ClinicCeilingTile"},
    "XRay": {"rect": (8.4, 13.0, 14.4, 17.5), "floor": "ClinicVinylFloor", "wall": "ClinicPlasterWhite", "ceiling": "ClinicCeilingTile"},
    "Toilets": {"rect": (8.4, 17.5, 14.4, 20.0), "floor": "ClinicFloorTile", "wall": "ClinicWallTileDirty", "ceiling": "ClinicPlasterWhite"},
    "Storage": {"rect": (8.4, 20.0, 14.4, 24.0), "floor": "ClinicConcrete", "wall": "ClinicPlasterWhite", "ceiling": "ClinicConcrete"},
    "Service": {"rect": (0.0, 24.0, 14.4, 26.4), "floor": "ClinicFloorTile", "wall": "ClinicPlasterGreen", "ceiling": "ClinicCeilingTile",
                "dado": "ClinicWallTile", "dado_height": 1.2},
    "Theatre": {"rect": (0.0, 26.4, 6.0, 32.4), "floor": "ClinicFloorTile", "wall": "ClinicWallTile", "ceiling": "ClinicPlasterWhite"},
    "Morgue": {"rect": (6.0, 26.4, 10.0, 32.4), "floor": "ClinicFloorTile", "wall": "ClinicWallTileDirty", "ceiling": "ClinicConcrete"},
    "Plant": {"rect": (10.0, 26.4, 14.4, 32.4), "floor": "ClinicConcrete", "wall": "ClinicConcrete", "ceiling": "ClinicConcrete"},
    "Bay": {"rect": (14.4, 24.0, 18.0, 28.0), "floor": "ClinicConcrete", "wall": "ClinicConcrete", "ceiling": "ClinicConcrete"},
}

# An opening joins two rooms through their shared edge. `at` is the centre of the opening along
# that edge, in plan coordinates (x for a wall that runs east-west, y for one that runs north-south).
# kind: "single" and "double" get a frame and leaves, "metal" a steel security door, "open" nothing.
OPENINGS = [
    {"name": "Lobby", "rooms": ("Reception", "Waiting"), "at": 3.0, "kind": "open", "width": 3.0, "height": 2.4},
    {"name": "Corridor", "rooms": ("Reception", "Corridor"), "at": 7.2, "kind": "double"},
    {"name": "Consult1", "rooms": ("Corridor", "Consult1"), "at": 8.2, "kind": "single"},
    {"name": "Consult2", "rooms": ("Corridor", "Consult2"), "at": 12.8, "kind": "single"},
    {"name": "Pharmacy", "rooms": ("Corridor", "Pharmacy"), "at": 17.0, "kind": "single"},
    {"name": "Staff", "rooms": ("Corridor", "Staff"), "at": 21.5, "kind": "single"},
    {"name": "Ward", "rooms": ("Corridor", "Ward"), "at": 9.5, "kind": "double"},
    {"name": "XRay", "rooms": ("Corridor", "XRay"), "at": 15.2, "kind": "single"},
    {"name": "Toilets", "rooms": ("Corridor", "Toilets"), "at": 18.8, "kind": "single"},
    {"name": "Storage", "rooms": ("Corridor", "Storage"), "at": 22.0, "kind": "single"},
    {"name": "Service", "rooms": ("Corridor", "Service"), "at": 7.2, "kind": "double"},
    {"name": "Theatre", "rooms": ("Service", "Theatre"), "at": 3.0, "kind": "double"},
    {"name": "Morgue", "rooms": ("Service", "Morgue"), "at": 8.0, "kind": "metal"},
    {"name": "Plant", "rooms": ("Service", "Plant"), "at": 12.2, "kind": "metal"},
    {"name": "Bay", "rooms": ("Service", "Bay"), "at": 25.2, "kind": "metal"},
    {"name": "WardXRay", "rooms": ("Ward", "XRay"), "at": 12.5, "kind": "single"},
]


def opening_size(opening):
    """Clear width and height of an opening."""
    if "width" in opening:
        return opening["width"], opening.get("height", DOOR_HEIGHT)
    if opening["kind"] == "double":
        return DOUBLE_DOOR_WIDTH, DOOR_HEIGHT
    return DOOR_WIDTH, DOOR_HEIGHT


def shared_edge(room_a, room_b):
    """
    The edge two rooms share: ("x", value, low, high) for a wall at x = value running along Y from
    low to high, or ("y", value, low, high) for a wall at y = value running along X.
    """
    ax0, ay0, ax1, ay1 = ROOMS[room_a]["rect"]
    bx0, by0, bx1, by1 = ROOMS[room_b]["rect"]
    for value_a, value_b in ((ax1, bx0), (ax0, bx1)):
        if abs(value_a - value_b) < 1.0e-6:
            low, high = max(ay0, by0), min(ay1, by1)
            if high - low > 1.0e-6:
                return ("x", value_a, low, high)
    for value_a, value_b in ((ay1, by0), (ay0, by1)):
        if abs(value_a - value_b) < 1.0e-6:
            low, high = max(ax0, bx0), min(ax1, bx1)
            if high - low > 1.0e-6:
                return ("y", value_a, low, high)
    raise ValueError("rooms %s and %s do not share a wall" % (room_a, room_b))


def openings_of(room):
    """
    The openings in a room's walls, as (side, along, width, height, opening) where side is one of
    "west" "east" "south" "north" and `along` is the centre of the opening along that wall, in
    plan coordinates.
    """
    x0, y0, x1, y1 = ROOMS[room]["rect"]
    found = []
    for opening in OPENINGS:
        if room not in opening["rooms"]:
            continue
        axis, value, low, high = shared_edge(*opening["rooms"])
        width, height = opening_size(opening)
        assert low + width * 0.5 <= opening["at"] <= high - width * 0.5, "opening %s does not fit its wall" % opening["name"]
        if axis == "x":
            side = "west" if abs(value - x0) < 1.0e-6 else "east"
        else:
            side = "south" if abs(value - y0) < 1.0e-6 else "north"
        found.append((side, opening["at"], width, height, opening))
    return found


def validate():
    """Checks the plan against itself: rooms do not overlap and every opening fits a shared wall."""
    names = list(ROOMS)
    for i, a in enumerate(names):
        ax0, ay0, ax1, ay1 = ROOMS[a]["rect"]
        assert ax1 > ax0 and ay1 > ay0, a
        for b in names[i + 1:]:
            bx0, by0, bx1, by1 = ROOMS[b]["rect"]
            overlap_x = min(ax1, bx1) - max(ax0, bx0)
            overlap_y = min(ay1, by1) - max(ay0, by0)
            assert not (overlap_x > 1.0e-6 and overlap_y > 1.0e-6), "rooms %s and %s overlap" % (a, b)
    for room in ROOMS:
        openings_of(room)
    return True


if __name__ == "__main__":
    validate()
    area = sum((r["rect"][2] - r["rect"][0]) * (r["rect"][3] - r["rect"][1]) for r in ROOMS.values())
    print("clinic plan OK: %d rooms, %d openings, %.0f m2" % (len(ROOMS), len(OPENINGS), area))
