"""
Furniture and equipment.

Every prop stands on the floor at its origin (wall-hung ones hang from it, and say so) and faces
-Y: that is the side a person uses it from. Sizes are taken from real things. Shapes are built
from boxes, tubes and turned profiles and then bevelled, which is enough for objects seen in a dark
room by torchlight; what sells them is proportion, the gaps between parts, and the materials.
"""

from .kit import piece

STEEL = "ClinicSteel"
PAINT = "ClinicPaintedMetal"
ENAMEL = "ClinicEnamelWhite"
RUST = "ClinicRustedMetal"
ZINC = "ClinicGalvanized"
LAMINATE = "ClinicLaminateGrey"
WOOD = "ClinicWoodDark"
VINYL = "ClinicVinylGreen"
CLOTH = "ClinicFabricCurtain"
MATTRESS = "ClinicMattress"
PLASTIC = "ClinicPlasticBeige"
RUBBER = "ClinicRubber"
CARD = "ClinicCardboard"
PAPER = "ClinicPaper"
GLASS = "ClinicGlass"
SCREEN = "ClinicScreenOff"
LIGHTBOX = "ClinicLightbox"
RED = "ClinicRedPaint"
BRASS = "ClinicBrass"
TUBE_LIGHT = "ClinicLightDiffuser"


def castor(m, x, y, radius=0.05):
    """A swivel wheel under (x, y): a fork and a rubber tyre."""
    m.box((x - 0.012, y - 0.012, radius * 1.6), (x + 0.012, y + 0.012, radius * 2.2), STEEL)
    m.cylinder((x - 0.016, y, radius), (1, 0, 0), radius, 0.032, RUBBER, segments=14)


def legs(m, x0, y0, x1, y1, height, thick=0.03, material=STEEL, foot=0.0):
    """Four square legs at the corners of a rectangle, from the floor (or from `foot`) up to `height`."""
    for x in (x0, x1 - thick):
        for y in (y0, y1 - thick):
            m.box((x, y, foot), (x + thick, y + thick, height), material, skip="zZ" if foot == 0.0 else "")


# --------------------------------------------------------------------------------------------------
# Beds and patient handling
# --------------------------------------------------------------------------------------------------


@piece("SM_Prop_HospitalBed", bevel=0.004)
def hospital_bed(m):
    """A 1960s ward bed, 2.0 x 0.9 m: tubular steel ends, a mesh base, a thin mattress, castors. Head at +Y."""
    w, length, base = 0.9, 2.0, 0.52
    half = w * 0.5
    # Head and foot ends: a bent tube with vertical bars.
    for y, top in ((length * 0.5, 1.05), (-length * 0.5, 0.85)):
        m.tube([(-half, y, 0.12), (-half, y, top - 0.06), (-half + 0.06, y, top), (half - 0.06, y, top), (half, y, top - 0.06), (half, y, 0.12)],
               0.016, ENAMEL, segments=10)
        for i in range(1, 6):
            x = -half + w * i / 6.0
            m.tube([(x, y, base - 0.04), (x, y, top - 0.005)], 0.007, ENAMEL, segments=6)
        m.tube([(-half, y, base - 0.04), (half, y, base - 0.04)], 0.012, ENAMEL, segments=8)
        for x in (-half, half):
            castor(m, x, y)
    # Side rails and the sprung base.
    for x in (-half + 0.02, half - 0.02):
        m.box((x - 0.02, -length * 0.5, base - 0.06), (x + 0.02, length * 0.5, base - 0.02), ENAMEL)
    m.box((-half + 0.04, -length * 0.5 + 0.03, base - 0.035), (half - 0.04, length * 0.5 - 0.03, base - 0.02), ZINC)
    m.box((-half + 0.03, -length * 0.5 + 0.04, base - 0.02), (half - 0.03, length * 0.5 - 0.04, base + 0.10), MATTRESS)
    # A flat pillow, askew.
    with m.at((0.05, length * 0.5 - 0.28, base + 0.10), rot_z=7.0):
        m.box((-0.30, -0.19, 0.0), (0.30, 0.19, 0.07), CLOTH)


@piece("SM_Prop_Gurney", bevel=0.004)
def gurney(m):
    """A stretcher trolley, 1.9 x 0.62 m, 0.82 m high, with a thin vinyl pad and a push bar at the head (+Y)."""
    w, length, top = 0.62, 1.9, 0.82
    half, hl = w * 0.5, length * 0.5
    for x in (-half + 0.03, half - 0.03):
        for y in (-hl + 0.18, hl - 0.18):
            m.tube([(x, y, 0.13), (x, y, top - 0.05)], 0.016, STEEL, segments=8)
            castor(m, x, y, 0.06)
        m.tube([(x, -hl + 0.18, 0.30), (x, hl - 0.18, 0.30)], 0.012, STEEL, segments=8)
    for y in (-hl + 0.18, hl - 0.18):
        m.tube([(-half + 0.03, y, 0.30), (half - 0.03, y, 0.30)], 0.012, STEEL, segments=8)
    m.box((-half, -hl, top - 0.05), (half, hl, top - 0.02), STEEL)
    m.box((-half + 0.02, -hl + 0.03, top - 0.02), (half - 0.02, hl - 0.03, top + 0.05), VINYL)
    m.tube([(-half + 0.03, hl, top - 0.03), (-half + 0.03, hl + 0.10, top + 0.10), (half - 0.03, hl + 0.10, top + 0.10), (half - 0.03, hl, top - 0.03)],
           0.013, STEEL, segments=8)
    # A drop-down side rail, raised.
    m.tube([(half, -hl + 0.35, top), (half, -hl + 0.35, top + 0.26), (half, hl - 0.45, top + 0.26), (half, hl - 0.45, top)], 0.01, STEEL, segments=8)


@piece("SM_Prop_IVStand", bevel=0.002)
def iv_stand(m):
    """A drip stand: five-legged base on castors, a telescopic pole to 1.9 m and two hooks."""
    import math
    for i in range(5):
        angle = 2 * math.pi * i / 5
        x, y = math.cos(angle) * 0.27, math.sin(angle) * 0.27
        m.tube([(0, 0, 0.11), (x, y, 0.09)], 0.011, STEEL, segments=6)
        m.cylinder((x - 0.012, y, 0.03), (1, 0, 0), 0.03, 0.024, RUBBER, segments=10)
    m.tube([(0, 0, 0.09), (0, 0, 1.15)], 0.014, STEEL, segments=10)
    m.cylinder((0, 0, 1.13), (0, 0, 1), 0.019, 0.05, PLASTIC, segments=12)
    m.tube([(0, 0, 1.15), (0, 0, 1.9)], 0.009, STEEL, segments=8)
    for sign in (-1, 1):
        m.tube([(0, 0, 1.88), (sign * 0.09, 0, 1.90), (sign * 0.13, 0, 1.86), (sign * 0.11, 0, 1.81)], 0.004, STEEL, segments=6)


@piece("SM_Prop_Wheelchair", bevel=0.003)
def wheelchair(m):
    """A folding wheelchair of the old kind: big rear wheels, sling seat, push handles. Faces -Y."""
    half = 0.24
    for x in (-half, half):
        # Frame: seat rail, back post with handle, front post down to the footrest.
        m.tube([(x, 0.22, 0.30), (x, 0.22, 0.95), (x, 0.32, 0.97)], 0.012, STEEL, segments=8)
        m.tube([(x, 0.22, 0.50), (x, -0.22, 0.50), (x, -0.26, 0.22), (x, -0.42, 0.14)], 0.012, STEEL, segments=8)
        m.tube([(x, 0.22, 0.30), (x, -0.20, 0.30)], 0.011, STEEL, segments=8)
        m.tube([(x, 0.20, 0.72), (x, -0.18, 0.72), (x, -0.20, 0.50)], 0.011, STEEL, segments=8)  # arm rest
        m.box((x - 0.02, -0.14, 0.725), (x + 0.02, 0.16, 0.745), RUBBER)
    # Rear wheels with their push rims. A lathe turns about Z, so each is built on its side and stood up.
    for x in (-half - 0.05, half + 0.05):
        with m.at((x, 0.16, 0.30), rot_y=90.0):
            m.lathe([(0.285, -0.012), (0.30, 0.0), (0.285, 0.012)], RUBBER, segments=28, closed_top=False, closed_bottom=False)
            m.lathe([(0.26, -0.018), (0.27, -0.016), (0.26, -0.014)], STEEL, segments=28, closed_top=False, closed_bottom=False)
            for k in range(8):
                with m.at(rot_z=k * 45.0):
                    m.tube([(0.02, 0, 0), (0.285, 0, 0)], 0.003, STEEL, segments=5, caps=False)
            m.cylinder((0, 0, -0.02), (0, 0, 1), 0.025, 0.04, STEEL, segments=10)
    for x in (-half, half):
        m.cylinder((x - 0.012, -0.40, 0.07), (1, 0, 0), 0.07, 0.024, RUBBER, segments=14)
        m.box((x - 0.07, -0.50, 0.12), (x + 0.07, -0.36, 0.135), ZINC)  # footplate
    m.box((-half, -0.20, 0.49), (half, 0.20, 0.505), VINYL)  # seat sling
    with m.at((0, 0.22, 0.55), rot_x=-4.0):
        m.box((-half, -0.008, 0.0), (half, 0.008, 0.36), VINYL)  # back sling


# --------------------------------------------------------------------------------------------------
# Office and storage furniture
# --------------------------------------------------------------------------------------------------


@piece("SM_Prop_Desk", bevel=0.004)
def desk(m):
    """A steel office desk, 1.4 x 0.7 m, 0.75 high: laminate top, a drawer pedestal on the right, modesty panel."""
    w, d, h = 1.4, 0.7, 0.75
    m.box((-w * 0.5, -d * 0.5, h - 0.03), (w * 0.5, d * 0.5, h), LAMINATE)
    legs(m, -w * 0.5 + 0.03, -d * 0.5 + 0.03, -w * 0.5 + 0.06, d * 0.5 - 0.03, h - 0.03, material=PAINT)
    m.box((w * 0.5 - 0.44, -d * 0.5 + 0.04, 0.08), (w * 0.5 - 0.03, d * 0.5 - 0.04, h - 0.03), PAINT)
    for i in range(3):
        z = 0.12 + i * 0.20
        m.box((w * 0.5 - 0.42, -d * 0.5 + 0.025, z), (w * 0.5 - 0.05, -d * 0.5 + 0.04, z + 0.18), PAINT)
        m.box((w * 0.5 - 0.29, -d * 0.5 + 0.012, z + 0.08), (w * 0.5 - 0.18, -d * 0.5 + 0.025, z + 0.10), STEEL)
    m.box((-w * 0.5 + 0.05, d * 0.5 - 0.06, 0.30), (w * 0.5 - 0.44, d * 0.5 - 0.045, h - 0.03), PAINT)


@piece("SM_Prop_Chair", bevel=0.003)
def chair(m):
    """A stacking chair: tubular steel frame, padded vinyl seat and back. Faces -Y."""
    for x in (-0.20, 0.20):
        m.tube([(x, -0.20, 0.0), (x, -0.19, 0.44), (x, 0.19, 0.44), (x, 0.22, 0.86)], 0.011, STEEL, segments=8)
        m.tube([(x, 0.20, 0.0), (x, 0.19, 0.44)], 0.011, STEEL, segments=8)
    m.box((-0.21, -0.21, 0.44), (0.21, 0.20, 0.49), VINYL)
    with m.at((0.0, 0.215, 0.60), rot_x=-6.0):
        m.box((-0.20, -0.015, 0.0), (0.20, 0.015, 0.24), VINYL)


@piece("SM_Prop_FilingCabinet", bevel=0.003)
def filing_cabinet(m):
    """A four-drawer steel filing cabinet, 0.47 x 0.62 m, 1.32 high; the third drawer hangs open."""
    w, d, h = 0.47, 0.62, 1.32
    m.box((-w * 0.5, -d * 0.5 + 0.02, 0.0), (w * 0.5, d * 0.5, h), PAINT, skip="z")
    for i in range(4):
        z = 0.05 + i * 0.315
        out = 0.22 if i == 1 else 0.0
        y = -d * 0.5 - out
        m.box((-w * 0.5 + 0.01, y, z), (w * 0.5 - 0.01, y + 0.02, z + 0.295), PAINT)
        m.box((-0.06, y - 0.012, z + 0.19), (0.06, y, z + 0.21), STEEL)
        m.box((-0.035, y - 0.002, z + 0.235), (0.035, y, z + 0.265), PAPER)
        if out:
            m.box((-w * 0.5 + 0.02, y + 0.02, z + 0.01), (w * 0.5 - 0.02, -d * 0.5 + 0.02, z + 0.25), PAINT, skip="Z")
            m.box((-w * 0.5 + 0.03, y + 0.04, z + 0.03), (w * 0.5 - 0.03, y + 0.18, z + 0.24), PAPER)


@piece("SM_Prop_Locker", bevel=0.003)
def locker(m):
    """Two steel staff lockers side by side, 0.6 m wide, 1.8 high, with vents and a hasp; one door ajar."""
    w, d, h = 0.6, 0.45, 1.8
    m.box((-w * 0.5, -d * 0.5 + 0.02, 0.0), (w * 0.5, d * 0.5, h), PAINT, skip="z")
    m.box((-w * 0.5, -d * 0.5 + 0.02, 0.0), (w * 0.5, d * 0.5, 0.08), PAINT, skip="z")
    for i, swing in enumerate((0.0, -22.0)):
        x0 = -w * 0.5 + i * w * 0.5
        with m.at((x0 + 0.005, -d * 0.5 + 0.02, 0.0), rot_z=swing):
            m.box((0.0, -0.015, 0.09), (w * 0.5 - 0.01, 0.0, h - 0.01), PAINT)
            for k in range(5):
                m.box((0.06, -0.018, h - 0.20 - k * 0.025), (w * 0.5 - 0.07, -0.015, h - 0.19 - k * 0.025), RUBBER)
            m.box((w * 0.5 - 0.05, -0.03, 0.95), (w * 0.5 - 0.02, -0.015, 1.03), STEEL)


@piece("SM_Prop_ShelfMetal", bevel=0.002)
def shelf_metal(m):
    """Bolted steel shelving, 1.0 x 0.4 m, 1.9 high, five shelves."""
    w, d, h = 1.0, 0.4, 1.9
    for x in (-w * 0.5, w * 0.5 - 0.035):
        for y in (-d * 0.5, d * 0.5 - 0.035):
            m.box((x, y, 0.0), (x + 0.035, y + 0.035, h), ZINC, skip="z")
    for i in range(5):
        z = 0.12 + i * 0.42
        m.box((-w * 0.5 + 0.004, -d * 0.5 + 0.004, z), (w * 0.5 - 0.004, d * 0.5 - 0.004, z + 0.03), ZINC)
    for x in (-w * 0.5 + 0.017, w * 0.5 - 0.017):
        m.tube([(x, -d * 0.5 + 0.017, 0.15), (x, d * 0.5 - 0.017, 0.95)], 0.006, ZINC, segments=5)


@piece("SM_Prop_MedicineCabinet", bevel=0.003)
def medicine_cabinet(m):
    """A glazed instrument cabinet, 0.8 x 0.4 m, 1.75 high: enamelled steel, glass doors above, a cupboard below."""
    w, d, h = 0.8, 0.4, 1.75
    m.box((-w * 0.5, -d * 0.5, 0.12), (w * 0.5, d * 0.5, h), ENAMEL, skip="y")
    legs(m, -w * 0.5 + 0.02, -d * 0.5 + 0.02, w * 0.5 - 0.02, d * 0.5 - 0.02, 0.12, thick=0.035, material=ENAMEL)
    m.box((-w * 0.5 + 0.02, -d * 0.5, 0.14), (w * 0.5 - 0.02, -d * 0.5 + 0.02, 0.70), ENAMEL)
    for z in (0.72, 1.05, 1.38):
        m.box((-w * 0.5 + 0.02, -d * 0.5 + 0.03, z), (w * 0.5 - 0.02, d * 0.5 - 0.02, z + 0.006), GLASS)
    for x0, x1 in ((-w * 0.5 + 0.02, -0.005), (0.005, w * 0.5 - 0.02)):
        # A door: four thin stiles and rails around a pane.
        m.box((x0, -d * 0.5, 0.72), (x0 + 0.03, -d * 0.5 + 0.02, h - 0.03), ENAMEL)
        m.box((x1 - 0.03, -d * 0.5, 0.72), (x1, -d * 0.5 + 0.02, h - 0.03), ENAMEL)
        m.box((x0 + 0.03, -d * 0.5, 0.72), (x1 - 0.03, -d * 0.5 + 0.02, 0.75), ENAMEL)
        m.box((x0 + 0.03, -d * 0.5, h - 0.06), (x1 - 0.03, -d * 0.5 + 0.02, h - 0.03), ENAMEL)
        m.box((x0 + 0.03, -d * 0.5 + 0.008, 0.75), (x1 - 0.03, -d * 0.5 + 0.012, h - 0.06), GLASS)
    # Bottles and tins on the shelves.
    for i, (x, z, r, hh) in enumerate(((-0.25, 0.726, 0.03, 0.12), (-0.12, 0.726, 0.025, 0.16), (0.2, 0.726, 0.04, 0.09), (-0.2, 1.056, 0.035, 0.14),
                                       (0.05, 1.056, 0.028, 0.18), (0.26, 1.056, 0.03, 0.1), (0.1, 1.386, 0.045, 0.12), (-0.28, 1.386, 0.03, 0.2))):
        m.lathe([(r, 0.0), (r, hh * 0.7), (r * 0.45, hh * 0.85), (r * 0.45, hh)], GLASS if i % 3 else PLASTIC, segments=12, axis_origin=(x, 0.02, z))


@piece("SM_Prop_ReceptionDesk", bevel=0.005)
def reception_desk(m):
    """The reception counter: 2.6 m of front with a return of 1.2 m, a high shelf for visitors and a low desk behind it."""
    front, ret, depth, low, high = 2.6, 1.2, 0.7, 0.74, 1.08
    # Front run along X, staff side at +Y.
    m.box((-front * 0.5, -0.04, 0.0), (front * 0.5, 0.0, high), WOOD, skip="z")
    m.box((-front * 0.5, -0.16, high), (front * 0.5, 0.14, high + 0.035), LAMINATE)
    m.box((-front * 0.5, 0.0, low - 0.03), (front * 0.5, depth, low), LAMINATE)
    m.box((-front * 0.5, 0.0, 0.0), (-front * 0.5 + 0.03, depth, low - 0.03), WOOD, skip="z")
    # The return, at the right-hand end as staff see it.
    m.box((front * 0.5 - 0.04, 0.0, 0.0), (front * 0.5, ret, high), WOOD, skip="z")
    m.box((front * 0.5 - depth, depth, low - 0.03), (front * 0.5 - 0.04, ret, low), LAMINATE)
    m.box((front * 0.5 - 0.16, 0.14, high), (front * 0.5 + 0.12, ret, high + 0.035), LAMINATE)
    # A kick strip, and a row of pigeon holes under the counter.
    m.box((-front * 0.5, -0.045, 0.0), (front * 0.5, -0.04, 0.10), STEEL)
    for i in range(6):
        x = -front * 0.5 + 0.1 + i * 0.36
        m.box((x, 0.02, low + 0.02), (x + 0.33, 0.3, low + 0.03), WOOD)
        m.box((x, 0.02, low), (x + 0.012, 0.3, low + 0.3), WOOD)


# --------------------------------------------------------------------------------------------------
# Sanitary
# --------------------------------------------------------------------------------------------------


@piece("SM_Prop_Sink", bevel=0.004)
def sink(m):
    """A wall-hung enamelled basin with a splash back and two taps. Hangs from its origin at floor level against a wall at +Y."""
    rim = 0.85
    m.box((-0.28, -0.44, rim - 0.20), (0.28, 0.0, rim), ENAMEL, skip="Z")
    # The bowl: a rim around a recess.
    m.box((-0.28, -0.44, rim - 0.01), (0.28, -0.40, rim), ENAMEL)
    m.box((-0.28, -0.06, rim - 0.01), (0.28, 0.0, rim), ENAMEL)
    m.box((-0.28, -0.40, rim - 0.01), (-0.24, -0.06, rim), ENAMEL)
    m.box((0.24, -0.40, rim - 0.01), (0.28, -0.06, rim), ENAMEL)
    m.box((-0.24, -0.40, rim - 0.16), (0.24, -0.06, rim - 0.15), ENAMEL)
    m.box((-0.30, -0.02, rim), (0.30, 0.0, rim + 0.30), ENAMEL)
    for x in (-0.12, 0.12):
        m.tube([(x, -0.03, rim), (x, -0.03, rim + 0.10), (x, -0.12, rim + 0.12), (x, -0.14, rim + 0.07)], 0.011, STEEL, segments=8)
        m.cylinder((x, -0.03, rim + 0.10), (0, 0, 1), 0.022, 0.02, STEEL, segments=10)
    m.tube([(0, -0.22, rim - 0.20), (0, -0.22, rim - 0.36), (0, -0.05, rim - 0.40), (0, 0.0, rim - 0.40)], 0.02, STEEL, segments=8)


@piece("SM_Prop_Toilet", bevel=0.004)
def toilet(m):
    """A floor-standing WC with its cistern, against a wall at +Y."""
    m.lathe([(0.13, 0.0), (0.15, 0.05), (0.13, 0.20), (0.19, 0.36), (0.20, 0.40)], ENAMEL, segments=18, axis_origin=(0, -0.30, 0.0), closed_top=False)
    m.lathe([(0.20, 0.40), (0.14, 0.38), (0.10, 0.22)], ENAMEL, segments=18, axis_origin=(0, -0.30, 0.0), closed_top=False, closed_bottom=False)
    m.lathe([(0.205, 0.0), (0.21, 0.012), (0.205, 0.024)], PLASTIC, segments=18, axis_origin=(0, -0.30, 0.40), closed_top=False, closed_bottom=False)
    m.box((-0.12, -0.14, 0.0), (0.12, 0.0, 0.38), ENAMEL, skip="z")
    m.box((-0.22, -0.20, 0.42), (0.22, 0.0, 0.80), ENAMEL)
    m.box((-0.23, -0.21, 0.80), (0.23, 0.0, 0.83), ENAMEL)
    m.cylinder((0.16, -0.21, 0.72), (0, -1, 0), 0.012, 0.03, STEEL, segments=8)


# --------------------------------------------------------------------------------------------------
# Clinical equipment
# --------------------------------------------------------------------------------------------------


@piece("SM_Prop_OperatingTable", bevel=0.005)
def operating_table(m):
    """An operating table: a heavy enamelled column on a broad base, a three-part padded top 1.95 x 0.5 m, head at +Y."""
    m.box((-0.30, -0.42, 0.0), (0.30, 0.42, 0.10), ENAMEL, skip="z")
    m.box((-0.13, -0.16, 0.10), (0.13, 0.16, 0.72), ENAMEL, skip="z")
    m.cylinder((0.13, 0.0, 0.32), (1, 0, 0), 0.05, 0.10, STEEL, segments=14)
    m.tube([(0.23, 0.0, 0.32), (0.30, 0.0, 0.32), (0.30, 0.0, 0.20)], 0.012, STEEL, segments=8)
    m.box((-0.22, -0.55, 0.72), (0.22, 0.55, 0.78), STEEL)
    top = 0.78
    m.box((-0.25, -0.50, top), (0.25, 0.42, top + 0.06), VINYL)
    with m.at((0.0, 0.44, top), rot_x=14.0):
        m.box((-0.25, 0.0, 0.0), (0.25, 0.50, 0.06), VINYL)
    with m.at((0.0, -0.52, top), rot_x=-10.0):
        m.box((-0.25, -0.44, 0.0), (0.25, 0.0, 0.06), VINYL)
    for x in (-0.27, 0.27):
        m.box((x - 0.006, -0.48, top + 0.01), (x + 0.006, 0.40, top + 0.04), STEEL)  # side rails for clamps
    # Leather straps left across the pad.
    for y in (-0.25, 0.15):
        m.box((-0.27, y, top + 0.058), (0.27, y + 0.05, top + 0.066), RUBBER)


@piece("SM_Prop_SurgicalLamp", bevel=0.004)
def surgical_lamp(m):
    """A ceiling-hung theatre lamp: a jointed arm and a shallow dish 0.8 m across. Hangs from its origin."""
    m.cylinder((0, 0, -0.04), (0, 0, 1), 0.07, 0.04, ENAMEL, segments=16)
    m.tube([(0, 0, -0.04), (0, 0, -0.55), (0.45, 0, -0.62), (0.50, 0, -0.95)], 0.02, ENAMEL, segments=10)
    with m.at((0.50, 0, -1.02), rot_y=12.0):
        m.lathe([(0.06, 0.10), (0.30, 0.06), (0.40, 0.0)], ENAMEL, segments=28, closed_top=True, closed_bottom=False)
        m.lathe([(0.0, 0.012), (0.38, 0.004)], LIGHTBOX, segments=28, closed_top=False, closed_bottom=False)
        m.tube([(0.41, 0, 0.0), (0.47, 0, -0.02)], 0.008, STEEL, segments=6)


@piece("SM_Prop_XRayViewer", bevel=0.003)
def xray_viewer(m):
    """A wall light box for films, 0.8 x 0.45 m, 8 cm deep. Hangs with its back on a wall at +Y, bottom edge at the origin."""
    m.box((-0.40, -0.08, 0.0), (0.40, 0.0, 0.45), ENAMEL)
    m.box((-0.38, -0.084, 0.03), (0.38, -0.08, 0.43), LIGHTBOX)
    m.box((-0.40, -0.09, 0.42), (0.40, -0.08, 0.45), STEEL)
    m.box((-0.33, -0.086, 0.06), (-0.03, -0.084, 0.42), SCREEN)  # a film left clipped up


@piece("SM_Prop_Monitor", bevel=0.004)
def monitor(m):
    """A bedside monitor: a beige box with a small dark screen, knobs and a carry handle; 0.34 wide, stands on a shelf or trolley."""
    m.box((-0.17, -0.16, 0.0), (0.17, 0.16, 0.26), PLASTIC)
    m.box((-0.14, -0.165, 0.07), (0.04, -0.16, 0.22), SCREEN)
    for i in range(4):
        m.cylinder((0.10, -0.16, 0.06 + i * 0.045), (0, -1, 0), 0.012, 0.014, RUBBER, segments=10)
    m.tube([(-0.10, 0.0, 0.26), (-0.10, 0.0, 0.30), (0.10, 0.0, 0.30), (0.10, 0.0, 0.26)], 0.007, STEEL, segments=6)


@piece("SM_Prop_Trolley", bevel=0.003)
def trolley(m):
    """An instrument trolley, 0.6 x 0.45 m, 0.85 high: two steel trays with raised edges on four castors, a kidney dish left on top."""
    w, d = 0.6, 0.45
    for x in (-w * 0.5 + 0.015, w * 0.5 - 0.015):
        for y in (-d * 0.5 + 0.015, d * 0.5 - 0.015):
            m.tube([(x, y, 0.10), (x, y, 0.85)], 0.011, STEEL, segments=8)
            castor(m, x, y, 0.04)
    for z in (0.22, 0.83):
        m.box((-w * 0.5, -d * 0.5, z), (w * 0.5, d * 0.5, z + 0.012), STEEL)
        m.box((-w * 0.5, -d * 0.5, z + 0.012), (w * 0.5, -d * 0.5 + 0.008, z + 0.035), STEEL)
        m.box((-w * 0.5, d * 0.5 - 0.008, z + 0.012), (w * 0.5, d * 0.5, z + 0.035), STEEL)
    m.lathe([(0.06, 0.0), (0.09, 0.035), (0.085, 0.035), (0.055, 0.006)], STEEL, segments=16, axis_origin=(0.12, 0.02, 0.842), closed_top=False)
    m.box((-0.2, -0.1, 0.842), (-0.02, 0.06, 0.848), CLOTH)


@piece("SM_Prop_OxygenTank", bevel=0.003)
def oxygen_tank(m):
    """An oxygen cylinder, 1.3 m tall, in a wheeled cradle."""
    m.lathe([(0.0, 0.06), (0.09, 0.08), (0.10, 0.16), (0.10, 1.05), (0.07, 1.15), (0.03, 1.20), (0.03, 1.26)], RUST, segments=18, closed_bottom=False)
    m.lathe([(0.04, 1.26), (0.045, 1.30), (0.0, 1.32)], BRASS, segments=10, closed_bottom=False, closed_top=False)
    m.tube([(0.12, 0.0, 0.06), (0.12, 0.0, 0.95), (0.0, 0.14, 1.0), (-0.12, 0.0, 0.95), (-0.12, 0.0, 0.06)], 0.009, STEEL, segments=6)
    for x in (-0.13, 0.11):
        m.cylinder((x, 0.0, 0.06), (1, 0, 0), 0.06, 0.02, RUBBER, segments=12)


# --------------------------------------------------------------------------------------------------
# Small things that fill a room
# --------------------------------------------------------------------------------------------------


@piece("SM_Prop_Boxes", bevel=0.003)
def boxes(m):
    """Three cardboard boxes, two stacked and one beside, the top one with its flaps open."""
    m.box((-0.30, -0.22, 0.0), (0.26, 0.22, 0.36), CARD, skip="z")
    with m.at((0.02, 0.01, 0.36), rot_z=9.0):
        m.box((-0.24, -0.18, 0.0), (0.24, 0.18, 0.30), CARD, skip="Z")
        for sign in (-1, 1):
            with m.at((sign * 0.24, 0.0, 0.30), rot_y=sign * 50.0):
                m.box((min(0.0, sign * 0.20), -0.18, 0.0), (max(0.0, sign * 0.20), 0.18, 0.004), CARD)
    with m.at((0.52, 0.06, 0.0), rot_z=-17.0):
        m.box((-0.20, -0.16, 0.0), (0.20, 0.16, 0.26), CARD, skip="z")


@piece("SM_Prop_Bin", bevel=0.003)
def bin_pedal(m):
    """A pedal bin of enamelled steel with its lid half open."""
    m.lathe([(0.13, 0.0), (0.15, 0.02), (0.16, 0.42)], ENAMEL, segments=18, closed_top=False)
    with m.at((0.0, 0.16, 0.43), rot_x=28.0):
        m.lathe([(0.165, 0.0), (0.15, 0.03), (0.0, 0.045)], ENAMEL, segments=18, axis_origin=(0.0, -0.16, 0.0), closed_bottom=False, closed_top=False)
    m.box((-0.04, -0.21, 0.0), (0.04, -0.14, 0.012), RUBBER)


@piece("SM_Prop_Extinguisher", bevel=0.002)
def extinguisher(m):
    """A water extinguisher on a wall bracket. Hangs with its back to a wall at +Y; the origin is 1 m below its top."""
    m.lathe([(0.0, 0.42), (0.07, 0.44), (0.075, 0.50), (0.075, 0.90), (0.05, 0.96), (0.02, 0.98), (0.02, 1.0)], RED, segments=16,
            axis_origin=(0, -0.085, 0), closed_bottom=False)
    m.box((-0.03, -0.11, 1.0), (0.03, -0.06, 1.03), STEEL)
    m.tube([(0.0, -0.11, 1.02), (0.0, -0.16, 1.05), (0.0, -0.20, 1.02)], 0.006, STEEL, segments=6)
    m.tube([(0.03, -0.085, 0.99), (0.10, -0.085, 0.92), (0.10, -0.085, 0.60)], 0.008, RUBBER, segments=6)
    m.box((-0.04, -0.01, 0.55), (0.04, 0.0, 0.92), STEEL)
    m.box((-0.045, -0.155, 0.66), (0.045, -0.15, 0.80), PAPER)


@piece("SM_Prop_Clock", bevel=0.002)
def clock(m):
    """A round wall clock, 30 cm, stopped. Its back is on a wall at +Y; the origin is its centre."""
    with m.at(rot_x=90.0):
        m.lathe([(0.15, 0.0), (0.15, 0.03), (0.135, 0.045)], PLASTIC, segments=28, closed_top=False)
        m.lathe([(0.0, 0.036), (0.135, 0.036)], PAPER, segments=28, closed_top=False, closed_bottom=False)
        m.lathe([(0.0, 0.05), (0.136, 0.044)], GLASS, segments=28, closed_top=False, closed_bottom=False)
        with m.at(rot_z=-60.0):
            m.box((-0.004, 0.0, 0.038), (0.004, 0.075, 0.040), RUBBER)
        with m.at(rot_z=95.0):
            m.box((-0.003, 0.0, 0.040), (0.003, 0.11, 0.042), RUBBER)


@piece("SM_Prop_Radiator", bevel=0.004)
def radiator(m):
    """A cast-iron column radiator, ten sections, 0.6 high, with its valve and pipe. Against a wall at +Y."""
    for i in range(10):
        x = -0.36 + i * 0.08
        m.box((x, -0.14, 0.12), (x + 0.055, -0.03, 0.70), RUST)
    for z in (0.16, 0.66):
        m.tube([(-0.38, -0.085, z), (0.42, -0.085, z)], 0.02, RUST, segments=8)
    for x in (-0.33, 0.33):
        m.box((x - 0.02, -0.12, 0.0), (x + 0.02, -0.05, 0.12), RUST, skip="z")
    m.tube([(0.42, -0.085, 0.16), (0.48, -0.085, 0.16), (0.48, -0.085, 0.0)], 0.014, RUST, segments=8)
    m.cylinder((0.44, -0.085, 0.16), (0, -1, 0), 0.022, 0.05, BRASS, segments=10)


@piece("SM_Prop_CurtainRail", bevel=0.0)
def curtain_rail(m):
    """
    A cubicle curtain, 2.0 m of rail with the cloth drawn two thirds across in deep folds, hanging
    from 2.1 m to 30 cm above the floor. The rail runs along X; the origin is on the floor under its middle.
    """
    import math
    m.tube([(-1.0, 0.0, 2.1), (1.0, 0.0, 2.1)], 0.012, STEEL, segments=8)
    for x in (-1.0, 1.0):
        m.tube([(x, 0.0, 2.1), (x, 0.0, 2.8)], 0.006, STEEL, segments=6)
    # The cloth as a pleated ribbon: its folds are gathered at the top and loosen towards the hem.
    columns, rows = 56, 8
    index = m.slot(CLOTH)
    grid = []
    for r in range(rows + 1):
        z = 2.06 - (2.06 - 0.30) * r / rows
        loosen = 0.55 + 0.45 * r / rows
        row = []
        for c in range(columns + 1):
            t = c / columns
            x = -1.0 + 1.36 * t + 0.02 * math.sin(t * 9.0 + r * 0.6)
            y = 0.055 * loosen * math.sin(t * math.pi * 17.0) + 0.02 * math.sin(t * 5.0 + r)
            row.append(m.vert((x, y, z)))
        grid.append(row)
    for r in range(rows):
        for c in range(columns):
            face = m.bm.faces.new([grid[r][c], grid[r + 1][c], grid[r + 1][c + 1], grid[r][c + 1]])
            face.material_index = index
            face.smooth = True


@piece("SM_Prop_MorgueDrawers", bevel=0.005)
def morgue_drawers(m):
    """
    A refrigerated body store: a steel cabinet 2.4 m wide and 2.0 high with two rows of three
    doors, heavy lever handles, and one tray pulled half out. Stands against a wall at +Y.
    """
    w, d, h = 2.4, 2.3, 2.0
    m.box((-w * 0.5, -0.02, 0.0), (w * 0.5, 0.0, h), STEEL, skip="z")
    m.box((-w * 0.5, -0.02, h), (w * 0.5, 0.0, h + 0.18), ZINC)
    for row in range(2):
        for col in range(3):
            x0 = -w * 0.5 + 0.05 + col * 0.78
            z0 = 0.12 + row * 0.92
            pulled = row == 0 and col == 1
            y = -0.02
            if pulled:
                m.box((x0 + 0.04, -1.25, z0 + 0.10), (x0 + 0.70, -0.02, z0 + 0.13), STEEL)
                m.box((x0 + 0.04, -1.25, z0 + 0.13), (x0 + 0.70, -1.22, z0 + 0.18), STEEL)
                with m.at((x0 + 0.74, -0.02, 0.0), rot_z=-95.0):
                    m.box((-0.74, -0.07, z0), (0.0, 0.0, z0 + 0.82), STEEL)
                m.box((x0 + 0.02, -0.02, z0 + 0.02), (x0 + 0.72, -0.012, z0 + 0.80), SCREEN)
                continue
            m.box((x0, y - 0.07, z0), (x0 + 0.74, y, z0 + 0.82), STEEL)
            m.box((x0 + 0.60, y - 0.10, z0 + 0.36), (x0 + 0.68, y - 0.07, z0 + 0.46), STEEL)
            m.tube([(x0 + 0.64, y - 0.10, z0 + 0.41), (x0 + 0.64, y - 0.15, z0 + 0.41), (x0 + 0.44, y - 0.15, z0 + 0.41)], 0.012, STEEL, segments=8)
            m.box((x0 + 0.08, y - 0.074, z0 + 0.64), (x0 + 0.26, y - 0.07, z0 + 0.72), PAPER)


@piece("SM_Prop_Boiler", bevel=0.005)
def boiler(m):
    """The plant room's boiler: a lagged cylinder 1.9 m tall on legs, with a burner box, a flue and pipework."""
    m.lathe([(0.0, 0.30), (0.40, 0.36), (0.45, 0.50), (0.45, 1.70), (0.40, 1.84), (0.0, 1.90)], RUST, segments=24, closed_bottom=False, closed_top=False)
    for band in (0.62, 1.10, 1.58):
        m.lathe([(0.456, band), (0.456, band + 0.04)], ZINC, segments=24, closed_bottom=False, closed_top=False)
    import math
    for i in range(3):
        angle = 2 * math.pi * i / 3 + 0.4
        m.tube([(math.cos(angle) * 0.36, math.sin(angle) * 0.36, 0.0), (math.cos(angle) * 0.36, math.sin(angle) * 0.36, 0.38)], 0.025, RUST, segments=8)
    m.box((-0.22, -0.66, 0.42), (0.22, -0.42, 0.86), PAINT)
    m.box((-0.10, -0.67, 0.50), (0.10, -0.66, 0.62), SCREEN)
    m.tube([(0.0, 0.0, 1.88), (0.0, 0.0, 2.30), (0.0, 0.5, 2.55), (0.0, 0.5, 2.8)], 0.09, ZINC, segments=12)
    m.tube([(0.45, 0.0, 1.40), (0.70, 0.0, 1.40), (0.70, 0.0, 2.8)], 0.03, RUST, segments=8)
    m.tube([(0.45, 0.0, 0.60), (0.80, 0.0, 0.60), (0.80, 0.0, 2.8)], 0.03, RUST, segments=8)
    m.cylinder((0.62, -0.05, 1.40), (0, -1, 0), 0.06, 0.02, RED, segments=12)
    m.cylinder((-0.455, 0.0, 1.2), (-1, 0, 0), 0.06, 0.03, BRASS, segments=14)  # pressure gauge


@piece("SM_Prop_FuseBox", bevel=0.003)
def fuse_box(m):
    """
    The distribution board: a grey steel box 0.5 x 0.7 m with its door open, a main switch lever
    and a row of ceramic fuse carriers, one of them missing. Its back is on a wall at +Y; the origin
    is on the floor below it (the box is at 1.1 to 1.8 m).
    """
    z0, z1 = 1.1, 1.8
    m.box((-0.25, -0.14, z0), (0.25, 0.0, z1), PAINT, skip="y")
    m.box((-0.23, -0.02, z0 + 0.02), (0.23, -0.012, z1 - 0.02), ZINC)
    with m.at((-0.25, -0.14, 0.0), rot_z=-105.0):
        m.box((0.0, -0.012, z0), (0.50, 0.0, z1), PAINT)
    m.box((0.10, -0.06, z1 - 0.26), (0.19, -0.02, z1 - 0.08), RUBBER)
    with m.at((0.145, -0.06, z1 - 0.17), rot_x=-35.0):
        m.box((-0.012, -0.11, -0.012), (0.012, 0.0, 0.012), RED)
    for i in range(5):
        if i == 3:
            m.box((-0.20 + i * 0.075, -0.03, z0 + 0.10), (-0.14 + i * 0.075, -0.02, z0 + 0.24), SCREEN)
            continue
        m.box((-0.20 + i * 0.075, -0.07, z0 + 0.10), (-0.14 + i * 0.075, -0.02, z0 + 0.24), ENAMEL)
        m.box((-0.185 + i * 0.075, -0.075, z0 + 0.15), (-0.155 + i * 0.075, -0.07, z0 + 0.19), BRASS)
    m.tube([(0.0, -0.07, z1), (0.0, -0.07, 2.8)], 0.02, ZINC, segments=8)


@piece("SM_Light_Tube", bevel=0.002)
def light_tube(m):
    """A bare fluorescent batten, 1.2 m, for rooms without a ceiling grid. Hangs from its origin."""
    m.box((-0.62, -0.035, -0.045), (0.62, 0.035, 0.0), PAINT)
    m.cylinder((-0.59, 0.0, -0.065), (1, 0, 0), 0.016, 1.18, TUBE_LIGHT, segments=10)
    for x in (-0.61, 0.59):
        m.box((x, -0.02, -0.085), (x + 0.02, 0.02, -0.045), PLASTIC)
