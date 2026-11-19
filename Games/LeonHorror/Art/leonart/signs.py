"""
Door signs: an enamelled plate with the name of the room, to hang beside its door.

Hung like the other wall pieces: the origin is the middle of the plate's back, against a wall at +Y,
and the lettering looks towards -Y. The letters are geometry (Blender's own font turned into faces),
so a sign needs no texture of its own and stays sharp from any distance.

The level script (Scripts/build_clinic_map.py) hangs SM_Sign_<opening name> beside each door that
has one here.
"""

from .kit import piece
from .props import ENAMEL, PAINT, SCREEN, STEEL

# Opening name in the plan (Scripts/clinic_layout.py) -> what its sign says.
LABELS = {
    "Corridor": "CONSULTING ROOMS  -  WARD",
    "Consult1": "CONSULTING ROOM 1",
    "Consult2": "CONSULTING ROOM 2",
    "Pharmacy": "PHARMACY",
    "Staff": "STAFF ONLY",
    "Ward": "WARD",
    "XRay": "X-RAY",
    "Toilets": "TOILETS",
    "Storage": "STORE ROOM",
    "Service": "NO ADMITTANCE",
    "Theatre": "OPERATING THEATRE",
    "Morgue": "MORTUARY",
    "Plant": "PLANT ROOM",
    "Bay": "AMBULANCE BAY",
}

LETTER_HEIGHT = 0.034
PLATE_HEIGHT = 0.085
PLATE_DEPTH = 0.006


def plate_width(label):
    """Wide enough for the lettering and a margin, in steps of 5 cm so the plates look like a set."""
    needed = len(label) * LETTER_HEIGHT * 0.62 + 0.06
    return max(0.25, round(needed / 0.05 + 0.499) * 0.05)


def sign(m, label):
    width = plate_width(label)
    half, tall = width * 0.5, PLATE_HEIGHT * 0.5
    m.box((-half, -PLATE_DEPTH, -tall), (half, 0.0, tall), ENAMEL, skip="Y")
    # A painted rim, and a screw at each end.
    rim = 0.004
    m.box((-half, -PLATE_DEPTH - 0.0006, tall - rim), (half, -PLATE_DEPTH, tall), PAINT, skip="Y")
    m.box((-half, -PLATE_DEPTH - 0.0006, -tall), (half, -PLATE_DEPTH, -tall + rim), PAINT, skip="Y")
    for x in (-half + 0.014, half - 0.014):
        m.cylinder((x, -PLATE_DEPTH, 0.0), (0, -1, 0), 0.0035, 0.0012, STEEL, segments=8)
    m.text(label, (0.0, -PLATE_DEPTH - 0.0004, 0.0), LETTER_HEIGHT, SCREEN, fit_width=width - 0.07)


def _register(name, label):
    @piece("SM_Sign_" + name, bevel=0.0)
    def build(m):
        sign(m, label)

    build.__doc__ = "The door sign that reads '%s'." % label
    return build


for _name, _label in LABELS.items():
    _register(_name, _label)
