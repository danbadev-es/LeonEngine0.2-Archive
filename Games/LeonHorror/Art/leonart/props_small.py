"""
Doors beyond the plain one, and the things the player picks up or uses.

Door leaves have their origin on the hinge edge at floor level and extend along +X, front towards
-Y, as SM_Door_Single does; frames have theirs in the middle of the threshold. Items lie on their
origin as they would on a table.
"""

from .kit import DOOR_HEIGHT, piece
from .props import BRASS, ENAMEL, GLASS, PAINT, PAPER, PLASTIC, RED, RUBBER, RUST, SCREEN, STEEL, ZINC

DOOR = "ClinicDoorLaminate"
DOUBLE_WIDTH = 1.6


@piece("SM_DoorFrame_Double", bevel=0.004)
def door_frame_double(m):
    """The frame of a pair of ward doors: 1.6 m clear, no stop in the middle so the leaves swing both ways."""
    half, depth, face = DOUBLE_WIDTH * 0.5, 0.16, 0.06
    m.box((-half - face, -depth * 0.5, 0), (-half, depth * 0.5, DOOR_HEIGHT + face), PAINT, skip="z")
    m.box((half, -depth * 0.5, 0), (half + face, depth * 0.5, DOOR_HEIGHT + face), PAINT, skip="z")
    m.box((-half, -depth * 0.5, DOOR_HEIGHT), (half, depth * 0.5, DOOR_HEIGHT + face), PAINT)


@piece("SM_Door_Swing", bevel=0.002)
def door_swing(m):
    """One leaf of a pair of ward doors, 0.8 m: a porthole at eye height, a push plate and a kick plate on both faces."""
    width, thick, clear = DOUBLE_WIDTH * 0.5 - 0.006, 0.044, 0.004
    x0, x1, z0, z1 = clear, clear + width, 0.008, DOOR_HEIGHT - clear
    # The leaf is built around the hole for the porthole: four slabs.
    px0, px1, pz0, pz1 = x0 + width * 0.5 - 0.15, x0 + width * 0.5 + 0.15, 1.35, 1.65
    m.box((x0, -thick * 0.5, z0), (x1, thick * 0.5, pz0), DOOR)
    m.box((x0, -thick * 0.5, pz1), (x1, thick * 0.5, z1), DOOR)
    m.box((x0, -thick * 0.5, pz0), (px0, thick * 0.5, pz1), DOOR, skip="zZ")
    m.box((px1, -thick * 0.5, pz0), (x1, thick * 0.5, pz1), DOOR, skip="zZ")
    m.box((px0, -0.004, pz0), (px1, 0.004, pz1), GLASS)
    for side in (-1, 1):
        y0, y1 = sorted((side * thick * 0.5, side * (thick * 0.5 + 0.0015)))
        m.box((x0 + 0.03, y0, 0.03), (x1 - 0.03, y1, 0.30), STEEL)  # kick plate
        m.box((x1 - 0.16, y0, 0.95), (x1 - 0.04, y1, 1.30), STEEL)  # push plate
        # A steel rim around the porthole.
        m.box((px0 - 0.02, y0, pz0 - 0.02), (px1 + 0.02, y1, pz0), STEEL)
        m.box((px0 - 0.02, y0, pz1), (px1 + 0.02, y1, pz1 + 0.02), STEEL)
        m.box((px0 - 0.02, y0, pz0), (px0, y1, pz1), STEEL)
        m.box((px1, y0, pz0), (px1 + 0.02, y1, pz1), STEEL)
    for hz in (0.25, 1.05, 1.80):
        m.cylinder((0.0, -thick * 0.5 - 0.006, hz - 0.05), (0, 0, 1), 0.007, 0.10, STEEL, segments=10)


@piece("SM_Door_Metal", bevel=0.003)
def door_metal(m):
    """A steel security door, 0.9 m: pressed panels, a long lever handle, a small wired-glass slot, heavy hinges."""
    width, thick, clear = 0.9 - 0.008, 0.055, 0.004
    x0, x1, z0, z1 = clear, clear + width, 0.008, DOOR_HEIGHT - clear
    m.box((x0, -thick * 0.5, z0), (x1, thick * 0.5, z1), RUST)
    for side in (-1, 1):
        y0, y1 = sorted((side * thick * 0.5, side * (thick * 0.5 + 0.006)))
        # Two raised panels and a rail between them.
        m.box((x0 + 0.08, y0, 0.12), (x1 - 0.08, y1, 0.95), RUST)
        m.box((x0 + 0.08, y0, 1.10), (x1 - 0.08, y1, 1.48), RUST)
        m.box((x0 + 0.30, y0, 1.58), (x1 - 0.30, y1, 1.80), SCREEN)  # the viewing slot, dark
        face = side * thick * 0.5
        m.cylinder((x1 - 0.08, face, 1.02), (0, side, 0), 0.03, 0.012, STEEL, segments=16)
        m.cylinder((x1 - 0.08, face, 1.02), (0, side, 0), 0.012, 0.06, STEEL, segments=10)
        m.cylinder((x1 - 0.072, face + side * 0.055, 1.02), (-1, 0, 0), 0.011, 0.16, STEEL, segments=10)
    for hz in (0.22, 1.0, 1.82):
        m.cylinder((0.0, -thick * 0.5 - 0.01, hz - 0.07), (0, 0, 1), 0.012, 0.14, STEEL, segments=10)


# --------------------------------------------------------------------------------------------------
# Things to pick up
# --------------------------------------------------------------------------------------------------


@piece("SM_Item_Key", bevel=0.0008)
def item_key(m):
    """A door key on a brass tag, lying flat: about 9 cm long."""
    m.cylinder((0.0, 0.0, 0.0), (0, 0, 1), 0.012, 0.003, BRASS, segments=14)
    m.box((0.010, -0.003, 0.0), (0.055, 0.003, 0.003), BRASS)
    m.box((0.040, -0.011, 0.0), (0.046, -0.003, 0.003), BRASS)
    m.box((0.049, -0.009, 0.0), (0.055, -0.003, 0.003), BRASS)
    with m.at((-0.022, 0.012, 0.0), rot_z=35.0):
        m.box((-0.022, -0.012, 0.0), (0.012, 0.012, 0.002), RED)


@piece("SM_Item_Fuse", bevel=0.001)
def item_fuse(m):
    """A ceramic cartridge fuse with brass end caps, 8 cm, lying on its side."""
    with m.at((0.0, 0.0, 0.013), rot_y=90.0):
        m.cylinder((0, 0, -0.026), (0, 0, 1), 0.011, 0.052, ENAMEL, segments=14)
        m.cylinder((0, 0, -0.040), (0, 0, 1), 0.013, 0.016, BRASS, segments=14)
        m.cylinder((0, 0, 0.024), (0, 0, 1), 0.013, 0.016, BRASS, segments=14)


@piece("SM_Item_Medkit", bevel=0.003)
def item_medkit(m):
    """A first aid tin: white enamel, a red cross on the lid, a wire handle."""
    m.box((-0.11, -0.075, 0.0), (0.11, 0.075, 0.06), ENAMEL)
    m.box((-0.112, -0.077, 0.045), (0.112, 0.077, 0.05), STEEL)
    m.box((-0.04, -0.012, 0.06), (0.04, 0.012, 0.0615), RED)
    m.box((-0.012, -0.04, 0.06), (0.012, 0.04, 0.0615), RED)
    m.tube([(-0.05, -0.078, 0.03), (-0.05, -0.095, 0.035), (0.05, -0.095, 0.035), (0.05, -0.078, 0.03)], 0.003, STEEL, segments=6)


@piece("SM_Item_Ammo", bevel=0.002)
def item_ammo(m):
    """A carton of handgun cartridges, half open, with a few rounds beside it."""
    m.box((-0.05, -0.032, 0.0), (0.05, 0.032, 0.035), PAPER)
    with m.at((-0.05, 0.0, 0.035), rot_y=-70.0):
        m.box((0.0, -0.032, 0.0), (0.04, 0.032, 0.002), PAPER)
    for i, (x, y, turn) in enumerate(((0.075, 0.01, 20.0), (0.085, -0.02, -40.0), (0.068, -0.035, 75.0))):
        with m.at((x, y, 0.0048), rot_z=turn):
            with m.at(rot_y=90.0):
                m.cylinder((0, 0, -0.012), (0, 0, 1), 0.0048, 0.019, BRASS, segments=8)
                m.cylinder((0, 0, 0.007), (0, 0, 1), 0.0044, 0.008, ZINC, segments=8)


def pistol_parts(m):
    """A service automatic, about 20 cm: muzzle towards +X, sights up +Y, the grip below the origin."""
    m.box((-0.10, 0.012, -0.014), (0.095, 0.045, 0.014), SCREEN)  # slide
    m.box((-0.095, -0.004, -0.012), (0.07, 0.012, 0.012), ZINC)  # frame
    with m.at((-0.075, -0.004, 0.0), rot_z=-14.0):
        m.box((-0.022, -0.10, -0.013), (0.026, 0.0, 0.013), RUBBER)  # grip
    m.tube([(-0.035, -0.004, 0.0), (-0.03, -0.035, 0.0), (0.005, -0.038, 0.0), (0.012, -0.004, 0.0)], 0.003, ZINC, segments=6)
    m.box((-0.018, -0.022, -0.003), (-0.012, -0.004, 0.003), ZINC)  # trigger
    m.box((0.085, 0.045, -0.003), (0.092, 0.050, 0.003), SCREEN)  # front sight
    m.box((-0.098, 0.045, -0.006), (-0.09, 0.050, 0.006), SCREEN)  # rear sight


@piece("SM_Item_Pistol", bevel=0.002)
def item_pistol(m):
    """The handgun lying on its side, to be picked up."""
    with m.at((0.0, 0.0, 0.016), rot_x=90.0):
        pistol_parts(m)


@piece("SM_Item_Pipe", bevel=0.0)
def item_pipe(m):
    """A length of steel water pipe with a threaded elbow at one end, 85 cm: the first weapon."""
    with m.at((0.0, 0.0, 0.017)):
        m.tube([(-0.42, 0.0, 0.0), (0.36, 0.0, 0.0)], 0.014, RUST, segments=10)
        m.tube([(0.34, 0.0, 0.0), (0.40, 0.0, 0.0), (0.40, 0.06, 0.0)], 0.019, RUST, segments=10)
        m.tube([(-0.42, 0.0, 0.0), (-0.38, 0.0, 0.0)], 0.017, ZINC, segments=10)


# --------------------------------------------------------------------------------------------------
# The same things as they are held. Origin where the hand closes, pointing along +Y (which is the
# way the engine calls forward), up along +Z. The game hangs them from a hand bone.
# --------------------------------------------------------------------------------------------------


@piece("SM_Held_Pistol", bevel=0.002)
def held_pistol(m):
    """The handgun in the hand: origin in the middle of the grip, muzzle forward."""
    with m.at(rot_z=90.0, rot_x=90.0):
        with m.at((0.068, 0.055, 0.0)):
            pistol_parts(m)


@piece("SM_Held_Pipe", bevel=0.0)
def held_pipe(m):
    """The pipe in the hand: held a hand's width from its plain end, the elbow at the far end."""
    m.tube([(0.0, -0.12, 0.0), (0.0, 0.66, 0.0)], 0.014, RUST, segments=10)
    m.tube([(0.0, 0.64, 0.0), (0.0, 0.70, 0.0), (0.0, 0.70, 0.06)], 0.019, RUST, segments=10)
    m.tube([(0.0, -0.12, 0.0), (0.0, -0.08, 0.0)], 0.017, ZINC, segments=10)


@piece("SM_Held_Flashlight", bevel=0.0015)
def held_flashlight(m):
    """A rubber-sleeved flashlight, 19 cm: origin in the middle of the barrel, lens forward."""
    m.cylinder((0.0, -0.085, 0.0), (0, 1, 0), 0.0165, 0.125, RUBBER, segments=14)
    m.cylinder((0.0, -0.092, 0.0), (0, 1, 0), 0.0175, 0.012, ZINC, segments=14)  # tail cap
    for y in (-0.05, -0.03, -0.01):
        m.cylinder((0.0, y, 0.0), (0, 1, 0), 0.0175, 0.006, RUBBER, segments=14)  # grip rings
    m.cylinder((0.0, 0.04, 0.0), (0, 1, 0), 0.021, 0.02, ZINC, segments=14)  # neck
    m.cylinder((0.0, 0.06, 0.0), (0, 1, 0), 0.027, 0.04, ZINC, segments=16)  # head
    m.cylinder((0.0, 0.1, 0.0), (0, 1, 0), 0.023, 0.0015, "ClinicLightDiffuser", segments=16)  # lens
    m.box((-0.004, -0.02, 0.0165), (0.004, 0.0, 0.021), RED)  # switch


@piece("SM_Item_Note", bevel=0.0)
def item_note(m):
    """A sheet of paper, slightly curled, with a clip board under it."""
    m.box((-0.115, -0.16, 0.0), (0.115, 0.16, 0.004), "ClinicWoodDark")
    m.box((-0.04, 0.135, 0.004), (0.04, 0.16, 0.012), STEEL)
    import math
    index = m.slot(PAPER)
    columns, rows = 6, 8
    grid = []
    for r in range(rows + 1):
        row = []
        for c in range(columns + 1):
            x = -0.105 + 0.21 * c / columns
            y = -0.15 + 0.297 * r / rows
            z = 0.0045 + 0.006 * math.sin(c / columns * math.pi) * (1.0 - r / rows) ** 2
            row.append(m.vert((x, y, z)))
        grid.append(row)
    for r in range(rows):
        for c in range(columns):
            face = m.bm.faces.new([grid[r][c], grid[r][c + 1], grid[r + 1][c + 1], grid[r + 1][c]])
            face.material_index = index
            face.smooth = True


@piece("SM_Prop_Typewriter", bevel=0.003)
def typewriter(m):
    """A portable typewriter with a sheet in it: where the game is saved. Faces -Y; 32 cm wide."""
    m.box((-0.16, -0.16, 0.0), (0.16, 0.15, 0.05), PAINT)
    with m.at((0.0, -0.16, 0.05), rot_x=14.0):
        m.box((-0.15, 0.0, 0.0), (0.15, 0.17, 0.012), PAINT)
        for row in range(4):
            for column in range(10):
                x = -0.128 + column * 0.0285 + (row % 2) * 0.012
                m.cylinder((x, 0.022 + row * 0.036, 0.012), (0, 0, 1), 0.0095, 0.008, SCREEN, segments=8)
    m.box((-0.15, 0.03, 0.05), (0.15, 0.15, 0.10), PAINT)
    with m.at((0.0, 0.10, 0.115), rot_y=90.0):
        m.cylinder((0, 0, -0.17), (0, 0, 1), 0.022, 0.34, RUBBER, segments=14)  # platen
        m.cylinder((0, 0, 0.17), (0, 0, 1), 0.028, 0.02, PLASTIC, segments=14)  # knob
    with m.at((0.0, 0.10, 0.125), rot_x=-12.0):
        m.box((-0.105, -0.002, 0.0), (0.105, 0.0, 0.20), PAPER)
    m.tube([(-0.17, 0.10, 0.12), (-0.21, 0.08, 0.16)], 0.004, STEEL, segments=6)  # carriage lever
