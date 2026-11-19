"""
The quads decals are laid on (materials in materials_decals.py).

A decal's texture does not repeat, and texture coordinates here are the position in metres over the
material's size, so each quad starts at the origin and is exactly one material wide: its corner, not
its middle, is the origin. The level script places it by its middle.

  SM_Decal_<Name>_Floor   lies on the floor (XY), three millimetres up, looking up
  SM_Decal_<Name>_Wall    stands against a wall at +Y like the other wall pieces, looking towards -Y
"""

from . import materials as material_library
from .kit import piece

# Clear of the surface by more than the depth buffer can confuse at the far end of a corridor; the
# decal material's own depth bias does the rest.
LIFT = 0.003

FLOOR = ("DecalBloodPool", "DecalBloodSmear", "DecalGrime", "DecalDamp")
WALL = ("DecalDamp", "DecalScratches", "DecalGrime", "DecalBloodSmear")


def size_of(name):
    return material_library.LIBRARY[name][2]


def _floor(name):
    @piece("SM_%s_Floor" % name.replace("Decal", "Decal_"))
    def build(m):
        s = size_of(name)
        m.quad([(0.0, 0.0, LIFT), (s, 0.0, LIFT), (s, s, LIFT), (0.0, s, LIFT)], name)

    return build


def _wall(name):
    @piece("SM_%s_Wall" % name.replace("Decal", "Decal_"))
    def build(m):
        s = size_of(name)
        m.quad([(0.0, -LIFT, 0.0), (s, -LIFT, 0.0), (s, -LIFT, s), (0.0, -LIFT, s)], name)

    return build


for _name in FLOOR:
    _floor(_name)
for _name in WALL:
    _wall(_name)
