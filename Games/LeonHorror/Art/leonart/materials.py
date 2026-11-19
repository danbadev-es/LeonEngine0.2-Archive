"""
The material library of the clinic.

Each material is a function that fills a TextureSet. They are written the way the surface is
built up in reality: a base, what the manufacturing leaves in it, then what years of use add
(scuffs, dirt in the low places, stains). Colours are given as they look on screen (sRGB hex) and
converted to linear.

Register a material with @material(name, size, tile_size); tile_size is how many metres one repeat
of the texture covers, and the meshes are unwrapped to match.
"""

import numpy as np

from . import noise, png
from .texture import TextureSet

LIBRARY = {}


def material(name, size=1024, tile_size=1.0):
    def register(function):
        LIBRARY[name] = (function, size, tile_size)
        return function

    return register


def build(name, size_override=None):
    function, size, tile_size = LIBRARY[name]
    result = TextureSet(name, size_override or size, tile_size)
    function(result)
    return result


def rgb(hex_color):
    """'#8fa39a' as a linear colour."""
    hex_color = hex_color.lstrip("#")
    encoded = np.array([int(hex_color[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32) / 255.0
    return png.srgb_decode(encoded)


def mix(a, b, mask):
    """a where mask is 0, b where it is 1. Colours (..., 3) or single channels."""
    mask = np.asarray(mask, dtype=np.float32)
    if np.ndim(a) == 3 or np.ndim(b) == 3 or (np.ndim(a) == 1 and np.ndim(mask) == 2):
        mask = mask[..., None]
    return a * (1.0 - mask) + b * mask


def grime(size, seed, amount=1.0):
    """Blotchy dirt mask 0..1: large soft patches broken up by finer noise."""
    patches = noise.unit(noise.fbm(size, 3, 5, seed))
    breakup = noise.unit(noise.fbm(size, 24, 4, seed + 7))
    return np.clip((noise.smoothstep(0.45, 0.85, patches) * (0.55 + 0.45 * breakup)) * amount, 0.0, 1.0)


# --------------------------------------------------------------------------------------------------
# Floors
# --------------------------------------------------------------------------------------------------


@material("ClinicVinylFloor", size=2048, tile_size=2.0)
def clinic_vinyl_floor(t):
    """
    Sheet vinyl as laid in hospitals: a pale grey-green base full of small flecks, welded seams
    every metre, polished by cleaning where people walk and dull, scuffed and dirty elsewhere.
    """
    n = t.size
    base = rgb("#8b968c")
    # Flecks of two other colours pressed into the sheet.
    f1, _, id1 = noise.worley(n, 180, seed=11)
    f2, _, id2 = noise.worley(n, 260, seed=12)
    flecks_dark = noise.smoothstep(0.34, 0.20, f1) * (id1 > 0.45)
    flecks_light = noise.smoothstep(0.30, 0.18, f2) * (id2 > 0.6)
    color = np.broadcast_to(base, (n, n, 3)).copy()
    color = mix(color, rgb("#5f6a63"), flecks_dark * 0.75)
    color = mix(color, rgb("#c3cabd"), flecks_light * 0.7)
    # Uneven tone from the calendering, very low frequency.
    mottle = noise.fbm(n, 4, 5, seed=13)
    color *= (1.0 + 0.07 * mottle)[..., None]

    # Welded seams: a shallow rounded groove with a slightly different tone, one per metre.
    seams = noise.tiles(n, 2, 2, gap=0.006, bevel=0.004, seed=14)
    color = mix(color, rgb("#77837a"), seams["joint"] * 0.4)

    # Wear. Scuffs run mostly along the corridor; dirt gathers in broad patches and in the seams.
    scuffs = noise.scratches(n, 70, seed=15, length=(0.02, 0.10), width=2.2, direction=0.0, spread=0.6)
    fine = noise.scratches(n, 260, seed=16, length=(0.01, 0.05), width=1.0)
    dirt = grime(n, 17, 0.8)
    dirt = np.clip(dirt + seams["joint"] * 0.25, 0.0, 1.0)
    color = mix(color, rgb("#4b463c"), dirt * 0.38)
    color = mix(color, rgb("#2e2c29"), scuffs * 0.22)

    # Height: flat but for the seams, the scuffs and a faint ripple; all well under a millimetre.
    ripple = noise.fbm(n, 9, 4, seed=18)
    t.height = (seams["height"] * 0.0006 + ripple * 0.00012 - scuffs * 0.00008 - fine * 0.00003).astype(np.float32)

    # Satin where it is clean, dull where dirt or scratches broke the finish.
    polish = noise.unit(noise.fbm(n, 5, 4, seed=19))
    roughness = 0.30 + 0.16 * polish + 0.30 * dirt + 0.25 * scuffs + 0.12 * fine
    t.roughness = np.clip(roughness, 0.18, 0.9).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 0.8


# --------------------------------------------------------------------------------------------------
# Walls and ceilings
# --------------------------------------------------------------------------------------------------


@material("ClinicPlasterGreen", size=2048, tile_size=2.5)
def clinic_plaster_green(t):
    """
    Plaster painted institutional green decades ago: roller texture, hairline cracks, tide marks
    where water ran down, and a general yellowing.
    """
    n = t.size
    color = np.broadcast_to(rgb("#a9b8a6"), (n, n, 3)).copy()
    # Roller stipple: fine, even, slightly directional.
    stipple = noise.fbm(n, 96, 3, seed=21)
    trowel = noise.fbm(n, (6, 10), 4, seed=22)
    # Yellowed, uneven tone.
    tone = noise.fbm(n, 3, 5, seed=23)
    color *= (1.0 + 0.06 * tone + 0.025 * stipple)[..., None]
    color = mix(color, rgb("#b0a886"), noise.smoothstep(0.1, 0.9, noise.unit(tone)) * 0.22)

    # Water streaks: noise stretched down the wall, sharpened into runs.
    runs = noise.unit(noise.fbm(n, (40, 2), 4, seed=24))
    runs = noise.smoothstep(0.62, 0.9, runs) * noise.smoothstep(0.35, 0.8, noise.unit(noise.fbm(n, 3, 3, seed=25)))
    color = mix(color, rgb("#6f6a52"), runs * 0.45)

    # Hairline cracks along the borders of large cells.
    f1, f2, _ = noise.worley(n, 5, seed=26)
    wobble = noise.fbm(n, 20, 3, seed=27) * 0.05
    cracks = noise.smoothstep(0.009, 0.0, (f2 - f1) + wobble * 0.4) * noise.smoothstep(0.4, 0.75, noise.unit(noise.fbm(n, 2, 3, seed=28)))
    color = mix(color, rgb("#4a4b42"), cracks * 0.6)

    dirt = grime(n, 29, 0.6)
    color = mix(color, rgb("#5e5a48"), dirt * 0.25)

    t.height = (stipple * 0.00022 + trowel * 0.0007 - cracks * 0.0009).astype(np.float32)
    t.roughness = np.clip(0.62 + 0.10 * noise.unit(tone) + 0.18 * dirt + 0.15 * runs + 0.2 * cracks, 0.4, 0.95).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicCeilingTile", size=1024, tile_size=1.2)
def clinic_ceiling_tile(t):
    """
    Suspended ceiling: mineral fibre tiles of 60 cm with pinholes and fissures, lying in a white
    painted T-bar grid. Two by two tiles to a repeat; water has stained a few.
    """
    n = t.size
    grid = noise.tiles(n, 2, 2, gap=0.085, bevel=0.012, seed=31)
    bar = grid["joint"]  # the metal grid shows where the joints are

    fibre = noise.fbm(n, 64, 4, seed=32)
    tile_color = np.broadcast_to(rgb("#cfcdc2"), (n, n, 3)).copy()
    tile_color *= (1.0 + 0.05 * fibre + 0.05 * (grid["id"] - 0.5))[..., None]

    # Pinholes: small dark dots, and longer fissures.
    f1, _, ids = noise.worley(n, 110, seed=33)
    holes = noise.smoothstep(0.22, 0.10, f1) * (ids > 0.35)
    fissures = noise.scratches(n, 260, seed=34, length=(0.01, 0.045), width=1.4)
    pits = np.clip(holes + fissures * 0.8, 0.0, 1.0) * (1.0 - bar)
    tile_color = mix(tile_color, rgb("#5d5a52"), pits * 0.8)

    # Brown rings where the roof leaked, on some tiles only.
    stain_shape = noise.unit(noise.fbm(n, 3, 5, seed=35))
    ring = noise.smoothstep(0.55, 0.66, stain_shape) - noise.smoothstep(0.70, 0.9, stain_shape) * 0.55
    stain = np.clip(ring, 0.0, 1.0) * (grid["id"] > 0.5) * (1.0 - bar)
    tile_color = mix(tile_color, rgb("#8a6f45"), stain * 0.55)

    bar_color = np.broadcast_to(rgb("#d8d8d2"), (n, n, 3)) * (1.0 + 0.03 * noise.fbm(n, 30, 3, seed=36))[..., None]
    t.color = np.clip(mix(tile_color, bar_color, bar), 0.0, 1.0).astype(np.float32)

    # The tiles sit a few millimetres above the face of the grid (seen from below: recessed).
    t.height = ((1.0 - bar) * (-0.004) + fibre * 0.0004 * (1.0 - bar) - pits * 0.0016).astype(np.float32)
    t.roughness = np.clip(mix(0.9 + 0.05 * fibre, 0.42, bar) + stain * 0.05, 0.3, 1.0).astype(np.float32)
    t.metallic = np.zeros((n, n), dtype=np.float32)
    t.occlusion_strength = 1.2


# --------------------------------------------------------------------------------------------------
# Doors, frames and trim
# --------------------------------------------------------------------------------------------------


@material("ClinicDoorLaminate", size=1024, tile_size=1.0)
def clinic_door_laminate(t):
    """
    Door leaf: a pale wood-grain laminate, darkened by hands around the handle height and scuffed
    by trolleys low down. The repeat is one metre and the grain runs up the door (along v).
    """
    n = t.size
    u, v = noise.grid(n)
    # Grain: long streaks along v, bent slightly by low-frequency noise.
    bend = noise.fbm(n, 3, 3, seed=41) * 0.03
    lines = noise.unit(noise.fbm(n, (90, 3), 4, seed=42))
    rings = 0.5 + 0.5 * np.sin((u + bend) * 2.0 * np.pi * 14.0 + lines * 5.0)
    grain = noise.unit(0.6 * lines + 0.4 * rings)
    color = noise.ramp(grain, [(0.0, rgb("#8f7a5c")), (0.5, rgb("#b39c78")), (1.0, rgb("#c9b48f"))])
    color *= (1.0 + 0.04 * noise.fbm(n, 5, 4, seed=43))[..., None]

    scratches = noise.scratches(n, 16, seed=44, length=(0.03, 0.14), width=1.1)
    dirt = grime(n, 45, 0.5)
    color = mix(color, rgb("#5a4b38"), dirt * 0.3)
    color = mix(color, rgb("#d9cdb4"), scratches * 0.22)

    pores = noise.smoothstep(0.55, 0.9, noise.unit(noise.fbm(n, (160, 6), 3, seed=46)))
    t.height = (-pores * 0.00012 - scratches * 0.00015 + grain * 0.00005).astype(np.float32)
    t.roughness = np.clip(0.36 + 0.14 * grain + 0.22 * dirt + 0.25 * scratches, 0.2, 0.9).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicPaintedMetal", size=1024, tile_size=1.0)
def clinic_painted_metal(t):
    """
    Cream enamel on steel, as on door frames and bed rails: glossy with orange peel, chipped to
    bare metal and rust at the places things knock against it.
    """
    n = t.size
    peel = noise.fbm(n, 70, 3, seed=51)
    color = np.broadcast_to(rgb("#d6d0bd"), (n, n, 3)).copy()
    color *= (1.0 + 0.03 * noise.fbm(n, 4, 4, seed=52))[..., None]

    # Chips: small irregular islands; their rim is primer, their middle metal going to rust.
    shape = noise.unit(noise.fbm(n, 14, 5, seed=53))
    where = noise.smoothstep(0.55, 0.85, noise.unit(noise.fbm(n, 3, 3, seed=54)))
    chip = noise.smoothstep(0.70, 0.76, shape) * where
    core = noise.smoothstep(0.78, 0.84, shape) * where
    rust = core * noise.smoothstep(0.3, 0.7, noise.unit(noise.fbm(n, 30, 3, seed=55)))
    color = mix(color, rgb("#8e8a7c"), chip)
    color = mix(color, rgb("#6b6a68"), core)
    color = mix(color, rgb("#6e3b1c"), rust * 0.9)

    dirt = grime(n, 56, 0.5)
    color = mix(color, rgb("#5b5646"), dirt * 0.22)

    t.height = (peel * 0.00009 - chip * 0.00018 - core * 0.00010 + rust * 0.00012).astype(np.float32)
    t.metallic = np.clip(core - rust, 0.0, 1.0).astype(np.float32)
    t.roughness = np.clip(0.26 + 0.05 * peel + 0.3 * dirt + chip * 0.3 + rust * 0.5, 0.15, 0.95).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicSteel", size=1024, tile_size=0.5)
def clinic_steel(t):
    """Brushed stainless steel: handles, kick plates, trolleys. The brushing runs along u."""
    n = t.size
    brush = noise.fbm(n, (3, 220), 4, seed=61)
    fine = noise.fbm(n, (6, 400), 2, seed=62)
    color = np.broadcast_to(rgb("#b9bcbe"), (n, n, 3)).copy()
    color *= (1.0 + 0.06 * brush + 0.03 * fine)[..., None]

    smudge = noise.smoothstep(0.35, 0.95, noise.unit(noise.fbm(n, 3, 4, seed=63)))
    scratches = noise.scratches(n, 22, seed=64, length=(0.04, 0.2), width=0.9)
    color = mix(color, rgb("#a9abad"), smudge * 0.10)

    t.height = (brush * 0.00003 + fine * 0.00002 - scratches * 0.00008).astype(np.float32)
    t.metallic = np.ones((n, n), dtype=np.float32)
    t.roughness = np.clip(0.30 + 0.10 * noise.unit(brush) + 0.10 * smudge + 0.2 * scratches, 0.15, 0.8).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 0.3


@material("ClinicRubber", size=512, tile_size=0.5)
def clinic_rubber(t):
    """Dark rubber and matt black plastic: skirting, wheels, grips, cable sheaths."""
    n = t.size
    grain = noise.fbm(n, 48, 4, seed=71)
    color = np.broadcast_to(rgb("#262729"), (n, n, 3)).copy()
    color *= (1.0 + 0.12 * grain)[..., None]
    dust = grime(n, 72, 0.7)
    color = mix(color, rgb("#6b675c"), dust * 0.35)
    scuffs = noise.scratches(n, 14, seed=73, length=(0.05, 0.2), width=1.4)
    color = mix(color, rgb("#55554f"), scuffs * 0.4)

    t.height = (grain * 0.00012 - scuffs * 0.0001).astype(np.float32)
    t.roughness = np.clip(0.72 + 0.1 * grain + 0.15 * dust, 0.5, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


# --------------------------------------------------------------------------------------------------
# Materials without textures: a colour and a few numbers are all they need.
# --------------------------------------------------------------------------------------------------

FLAT = {
    # The diffuser of a ceiling light: it is the light people see, so it glows.
    "ClinicLightDiffuser": {"color": (0.90, 0.90, 0.86), "roughness": 0.6, "emissive": (1.0, 0.95, 0.84), "emissive_intensity": 6.0},
}


def write_flat(name, folder):
    """The .lmat of a textureless material."""
    import os

    values = FLAT[name]
    color = values.get("color", (0.8, 0.8, 0.8))
    emissive = values.get("emissive", (0.0, 0.0, 0.0))
    lines = [
        "# LeonEngine2 Material Asset File (.lmat)",
        "# Written by Games/LeonHorror/Art (leonart). Edit the script, not this file.",
        "Material:",
        '  Type: "PBR_Lit"',
        "  AlbedoColor: [%g, %g, %g]" % tuple(color),
        "  Metallic: %g" % values.get("metallic", 0.0),
        "  Roughness: %g" % values.get("roughness", 0.5),
        "  AO: 1",
        "  EmissiveColor: [%g, %g, %g]" % tuple(emissive),
        "  EmissiveIntensity: %g" % values.get("emissive_intensity", 0.0),
        '  AlphaMode: "%s"' % ("Blend" if values.get("opacity", 1.0) < 1.0 else "Opaque"),
        "  Opacity: %g" % values.get("opacity", 1.0),
        "  DoubleSided: %s" % ("true" if values.get("double_sided") else "false"),
    ]
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, "M_%s.lmat" % name)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


from . import materials_more  # noqa: E402,F401  (registers the rest of the library)
