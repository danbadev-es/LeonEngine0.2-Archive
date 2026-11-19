"""
More of the clinic's material library: the rooms beyond the corridor.

Same rules as materials.py, which imports this module so that everything ends up in one LIBRARY.
"""

import numpy as np

from . import noise
from .materials import grime, material, mix, rgb

# --------------------------------------------------------------------------------------------------
# Tiled rooms: treatment rooms, toilets, the operating theatre, the morgue
# --------------------------------------------------------------------------------------------------


def _ceramic(t, tile_color, grout_color, across, dirt_amount, seed):
    """Glazed square wall tiles with cement grout; shared by the clean and the filthy version."""
    n = t.size
    grid = noise.tiles(n, across, across, gap=0.035, bevel=0.03, seed=seed)
    face = 1.0 - grid["joint"]
    # Each tile is glazed a hair differently and sits a hair off flat.
    tint = (grid["id"] - 0.5) * 0.07
    glaze = noise.fbm(n, 6, 4, seed=seed + 1) * 0.03
    color = np.broadcast_to(rgb(tile_color), (n, n, 3)) * (1.0 + tint + glaze)[..., None]

    # Crazing: a faint net of hairlines in the glaze of some tiles.
    f1, f2, _ = noise.worley(n, across * 9, seed=seed + 2)
    craze = noise.smoothstep(0.03, 0.0, f2 - f1) * (grid["id"] > 0.62) * face
    color = mix(color, rgb("#8d8a7c"), craze * 0.35)

    # A few chipped corners showing the biscuit.
    corner = np.minimum(np.minimum(grid["u"], 1.0 - grid["u"]), np.minimum(grid["v"], 1.0 - grid["v"]))
    chip = noise.smoothstep(0.10, 0.03, corner + noise.fbm(n, 40, 3, seed=seed + 3) * 0.05) * (grid["id"] < 0.12) * face
    color = mix(color, rgb("#b9a88a"), chip)

    grout = np.broadcast_to(rgb(grout_color), (n, n, 3)) * (1.0 + 0.15 * noise.fbm(n, 60, 3, seed=seed + 4))[..., None]
    color = mix(color, grout, grid["joint"])

    # Dirt: runs down from the joints and fills them, soap scum dulls the lower faces.
    dirt = grime(n, seed + 5, dirt_amount)
    drips = noise.smoothstep(0.55, 0.9, noise.unit(noise.fbm(n, (36, 2), 4, seed=seed + 6))) * dirt_amount
    soil = np.clip(dirt * 0.7 + drips * 0.6 + grid["joint"] * 0.5 * dirt_amount, 0.0, 1.0)
    color = mix(color, rgb("#4f4a3a"), soil * 0.55)

    wobble = (grid["id"] - 0.5) * 0.0002
    t.height = (grid["height"] * 0.0016 + face * wobble - chip * 0.0008 - craze * 0.00005).astype(np.float32)
    t.roughness = np.clip(mix(0.10 + 0.05 * glaze * 10.0, 0.88, grid["joint"]) + soil * 0.45 + chip * 0.5, 0.06, 0.95).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 1.1


@material("ClinicWallTile", size=1024, tile_size=1.5)
def clinic_wall_tile(t):
    """White glazed wall tiles, 15 cm, as kept reasonably clean: treatment rooms, the pharmacy."""
    _ceramic(t, "#d9dcd4", "#8f8d83", 10, 0.35, 101)


@material("ClinicWallTileDirty", size=1024, tile_size=1.5)
def clinic_wall_tile_dirty(t):
    """The same tiles where nobody has cleaned for years: toilets, the morgue, the basement rooms."""
    _ceramic(t, "#c4c7b6", "#5d5a4c", 10, 1.0, 111)


@material("ClinicFloorTile", size=1024, tile_size=1.2)
def clinic_floor_tile(t):
    """
    Unglazed quarry tiles, 20 cm, dull red-brown and slightly cupped, with dark wet-looking grout:
    wet rooms and the service corridor.
    """
    n = t.size
    grid = noise.tiles(n, 6, 6, gap=0.045, bevel=0.05, seed=121)
    face = 1.0 - grid["joint"]
    body = noise.unit(noise.fbm(n, 30, 4, seed=122))
    color = noise.ramp(noise.unit(grid["id"] * 0.6 + body * 0.4),
                       [(0.0, rgb("#5a3a2e")), (0.5, rgb("#6f4a39")), (1.0, rgb("#80594a"))])
    speck = noise.smoothstep(0.25, 0.1, noise.worley(n, 150, seed=123)[0])
    color = mix(color, rgb("#3a2a24"), speck * 0.4 * face)
    grout = np.broadcast_to(rgb("#2f2c28"), (n, n, 3)) * (1.0 + 0.2 * noise.fbm(n, 50, 3, seed=124))[..., None]
    color = mix(color, grout, grid["joint"])

    dirt = grime(n, 125, 0.9)
    color = mix(color, rgb("#2b2620"), np.clip(dirt * 0.5 + grid["joint"] * 0.3, 0.0, 1.0) * 0.6)
    # Worn smooth in the middle of each tile, rough at its edges.
    centre = noise.smoothstep(0.05, 0.35, np.minimum(np.minimum(grid["u"], 1.0 - grid["u"]), np.minimum(grid["v"], 1.0 - grid["v"])))
    t.height = (grid["height"] * 0.0022 - centre * face * 0.0003 + body * 0.00025 * face).astype(np.float32)
    t.roughness = np.clip(mix(0.72 - 0.25 * centre, 0.9, grid["joint"]) + dirt * 0.1, 0.3, 0.95).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 1.2


# --------------------------------------------------------------------------------------------------
# Plaster, concrete, and what the building is made of behind the finishes
# --------------------------------------------------------------------------------------------------


@material("ClinicPlasterWhite", size=2048, tile_size=2.5)
def clinic_plaster_white(t):
    """
    Off-white emulsion over plaster, peeling: where the paint has let go the bare plaster shows,
    edged by a lifted lip of paint. Offices, the staff room, the upper half of two-tone walls.
    """
    n = t.size
    stipple = noise.fbm(n, 96, 3, seed=131)
    tone = noise.fbm(n, 3, 5, seed=132)
    color = np.broadcast_to(rgb("#cfcabb"), (n, n, 3)) * (1.0 + 0.05 * tone + 0.02 * stipple)[..., None]

    # Peeling patches: islands where the paint is gone.
    shape = noise.unit(noise.fbm(n, 7, 5, seed=133))
    where = noise.smoothstep(0.45, 0.8, noise.unit(noise.fbm(n, 2, 3, seed=134)))
    bare = noise.smoothstep(0.60, 0.64, shape) * where
    lip = (noise.smoothstep(0.56, 0.60, shape) - noise.smoothstep(0.60, 0.64, shape)) * where
    plaster = np.broadcast_to(rgb("#9c9685"), (n, n, 3)) * (1.0 + 0.12 * noise.fbm(n, 40, 4, seed=135))[..., None]
    color = mix(color, plaster, bare)
    color = mix(color, rgb("#e0dccf"), np.clip(lip, 0.0, 1.0) * 0.6)

    # Damp: a brown tide with a darker rim, and mould speckle inside it.
    damp_field = noise.unit(noise.fbm(n, 2, 4, seed=136))
    damp = noise.smoothstep(0.62, 0.8, damp_field)
    rim = noise.smoothstep(0.58, 0.62, damp_field) - noise.smoothstep(0.62, 0.66, damp_field)
    mould = noise.smoothstep(0.3, 0.12, noise.worley(n, 90, seed=137)[0]) * damp
    color = mix(color, rgb("#9a8a62"), damp * 0.35)
    color = mix(color, rgb("#6c5c3a"), np.clip(rim, 0.0, 1.0) * 0.45)
    color = mix(color, rgb("#2f3324"), mould * 0.6)

    t.height = (stipple * 0.0002 - bare * 0.0006 + np.clip(lip, 0.0, 1.0) * 0.0009 + noise.fbm(n, (5, 8), 4, seed=138) * 0.0006).astype(np.float32)
    t.roughness = np.clip(0.68 + 0.15 * bare + 0.1 * damp + 0.1 * noise.unit(tone), 0.45, 0.97).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicConcrete", size=1024, tile_size=3.0)
def clinic_concrete(t):
    """Poured concrete with shutter-board lines, pinholes and rust weeping from tie holes: the basement and the plant room."""
    n = t.size
    cloud = noise.unit(noise.fbm(n, 4, 6, seed=141))
    fine = noise.fbm(n, 80, 3, seed=142)
    color = noise.ramp(cloud, [(0.0, rgb("#5d5d58")), (0.6, rgb("#77766f")), (1.0, rgb("#8c8a81"))])
    color *= (1.0 + 0.05 * fine)[..., None]

    boards = noise.tiles(n, 1, 6, gap=0.006, bevel=0.004, seed=143)
    color = mix(color, rgb("#4a4a45"), boards["joint"] * 0.5)
    holes = noise.smoothstep(0.16, 0.06, noise.worley(n, 70, seed=144)[0]) * (noise.worley(n, 70, seed=144)[2] > 0.55)
    color = mix(color, rgb("#35352f"), holes * 0.7)
    # Rust runs below a few tie holes.
    ties_f, _, ties_id = noise.worley(n, 4, seed=145, jitter=0.5)
    tie = noise.smoothstep(0.035, 0.02, ties_f)
    weep = noise.smoothstep(0.6, 0.9, noise.unit(noise.fbm(n, (30, 2), 3, seed=146))) * noise.smoothstep(0.35, 0.05, ties_f)
    color = mix(color, rgb("#6b4a2a"), weep * 0.5)
    color = mix(color, rgb("#2a2520"), tie)

    stain = grime(n, 147, 0.8)
    color = mix(color, rgb("#3d3a32"), stain * 0.35)
    t.height = (-boards["joint"] * 0.0012 - holes * 0.0015 - tie * 0.004 + fine * 0.0003 + (cloud - 0.5) * 0.0008).astype(np.float32)
    t.roughness = np.clip(0.78 + 0.1 * fine + 0.08 * stain, 0.55, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 1.3


# --------------------------------------------------------------------------------------------------
# Metals
# --------------------------------------------------------------------------------------------------


@material("ClinicRustedMetal", size=1024, tile_size=1.0)
def clinic_rusted_metal(t):
    """Painted steel that has lost the fight: blistered grey-green paint over scaling rust. Pipes, old cabinets, the boiler."""
    n = t.size
    field = noise.unit(noise.fbm(n, 5, 6, seed=151))
    detail = noise.unit(noise.fbm(n, 40, 4, seed=152))
    rust = noise.smoothstep(0.42, 0.62, field * 0.75 + detail * 0.25)
    paint = np.broadcast_to(rgb("#6f7a6c"), (n, n, 3)) * (1.0 + 0.06 * noise.fbm(n, 6, 4, seed=153))[..., None]
    scale = noise.ramp(noise.unit(noise.fbm(n, 24, 5, seed=154)),
                       [(0.0, rgb("#2e1a10")), (0.45, rgb("#5e2f15")), (0.8, rgb("#8a4a1e")), (1.0, rgb("#a8672c"))])
    color = mix(paint, scale, rust)
    # Blisters in the paint that has not gone yet, near the rust.
    near = noise.smoothstep(0.30, 0.44, field * 0.75 + detail * 0.25) * (1.0 - rust)
    blister = noise.smoothstep(0.30, 0.10, noise.worley(n, 60, seed=155)[0]) * near
    color = mix(color, rgb("#8b9484"), blister * 0.3)

    pit = noise.fbm(n, 70, 3, seed=156)
    t.height = (rust * (0.0006 + pit * 0.0006) + blister * 0.0009 - (1.0 - rust) * 0.0002).astype(np.float32)
    t.metallic = np.zeros((n, n), dtype=np.float32)  # rust and paint are both dielectrics
    t.roughness = np.clip(mix(0.42, 0.92, rust) + 0.05 * pit, 0.3, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 1.2


@material("ClinicGalvanized", size=1024, tile_size=1.0)
def clinic_galvanized(t):
    """Galvanised sheet: ducts, shelving, bins. The zinc spangle shows as a pattern of dull crystals."""
    n = t.size
    f1, f2, ids = noise.worley(n, 26, seed=161)
    spangle = ids * 0.5 + noise.unit(f2 - f1) * 0.5
    color = np.broadcast_to(rgb("#9ea2a1"), (n, n, 3)) * (0.86 + 0.22 * spangle)[..., None]
    white = noise.smoothstep(0.6, 0.9, noise.unit(noise.fbm(n, 6, 5, seed=162)))
    color = mix(color, rgb("#c9cbc6"), white * 0.35)  # white rust bloom
    dirt = grime(n, 163, 0.6)
    color = mix(color, rgb("#55534a"), dirt * 0.3)

    t.height = (noise.fbm(n, 60, 3, seed=164) * 0.00004 + white * 0.00008).astype(np.float32)
    t.metallic = np.clip(1.0 - white * 0.7 - dirt * 0.5, 0.0, 1.0).astype(np.float32)
    t.roughness = np.clip(0.38 + 0.2 * spangle + 0.3 * white + 0.25 * dirt, 0.25, 0.95).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 0.4


@material("ClinicEnamelWhite", size=1024, tile_size=1.0)
def clinic_enamel_white(t):
    """White stove enamel on steel: basins, cabinets, the body of machines. Glossy, yellowed, chipped black at the edges."""
    n = t.size
    color = np.broadcast_to(rgb("#dedbd0"), (n, n, 3)) * (1.0 + 0.03 * noise.fbm(n, 4, 4, seed=171))[..., None]
    yellow = noise.smoothstep(0.4, 0.9, noise.unit(noise.fbm(n, 3, 4, seed=172)))
    color = mix(color, rgb("#cfc39a"), yellow * 0.3)
    shape = noise.unit(noise.fbm(n, 18, 5, seed=173))
    where = noise.smoothstep(0.6, 0.9, noise.unit(noise.fbm(n, 3, 3, seed=174)))
    chip = noise.smoothstep(0.72, 0.76, shape) * where
    color = mix(color, rgb("#1e1d1c"), chip)
    rust = chip * noise.smoothstep(0.4, 0.7, noise.unit(noise.fbm(n, 30, 3, seed=175)))
    color = mix(color, rgb("#70391a"), rust * 0.8)
    scratches = noise.scratches(n, 30, seed=176, length=(0.03, 0.15), width=1.0)
    stain = grime(n, 177, 0.5)
    color = mix(color, rgb("#6a6350"), stain * 0.25)

    t.height = (-chip * 0.0003 + rust * 0.0001 - scratches * 0.00004).astype(np.float32)
    t.metallic = np.zeros((n, n), dtype=np.float32)
    t.roughness = np.clip(0.14 + 0.2 * stain + 0.5 * chip + 0.2 * scratches, 0.08, 0.95).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


# --------------------------------------------------------------------------------------------------
# Furniture surfaces
# --------------------------------------------------------------------------------------------------


@material("ClinicLaminateGrey", size=1024, tile_size=1.0)
def clinic_laminate_grey(t):
    """Grey desk-top laminate with a fine linen print: desks, counters, cupboard doors. Ringed by cups, scuffed at the edges."""
    n = t.size
    weave = 0.5 * noise.perlin(n, (220, 6), seed=181) + 0.5 * noise.perlin(n, (6, 220), seed=182)
    color = np.broadcast_to(rgb("#a7a9a4"), (n, n, 3)) * (1.0 + 0.05 * weave + 0.03 * noise.fbm(n, 4, 4, seed=183))[..., None]
    # Cup rings: thin circles of stain.
    f1, _, ids = noise.worley(n, 5, seed=184, jitter=0.8)
    ring = (noise.smoothstep(0.045, 0.05, f1) - noise.smoothstep(0.055, 0.062, f1)) * (ids > 0.7)
    color = mix(color, rgb("#6e5f48"), np.clip(ring, 0.0, 1.0) * 0.4)
    scratches = noise.scratches(n, 40, seed=185, length=(0.02, 0.12), width=1.0)
    dirt = grime(n, 186, 0.5)
    color = mix(color, rgb("#5d5a50"), dirt * 0.25)

    t.height = (weave * 0.00002 - scratches * 0.00006).astype(np.float32)
    t.roughness = np.clip(0.34 + 0.06 * weave + 0.25 * dirt + 0.2 * scratches + np.clip(ring, 0.0, 1.0) * 0.2, 0.2, 0.9).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 0.3


@material("ClinicWoodDark", size=1024, tile_size=1.0)
def clinic_wood_dark(t):
    """Dark varnished wood, the grain along v: old desks, shelves, the trim of the director's office."""
    n = t.size
    u, _ = noise.grid(n)
    bend = noise.fbm(n, 2, 3, seed=191) * 0.05
    lines = noise.unit(noise.fbm(n, (70, 2), 5, seed=192))
    rings = 0.5 + 0.5 * np.sin((u + bend) * 2.0 * np.pi * 9.0 + lines * 7.0)
    grain = noise.unit(0.55 * lines + 0.45 * rings)
    color = noise.ramp(grain, [(0.0, rgb("#2b1a10")), (0.5, rgb("#4a2c18")), (1.0, rgb("#6a4326"))])
    # Varnish rubbed through in streaks that follow the grain, not in clouds.
    worn = noise.smoothstep(0.6, 0.95, noise.unit(noise.fbm(n, (10, 2), 4, seed=193))) * noise.smoothstep(0.3, 0.8, grain)
    color = mix(color, rgb("#7a5a3a"), worn * 0.3)
    scratches = noise.scratches(n, 35, seed=194, length=(0.03, 0.2), width=1.1)
    color = mix(color, rgb("#b09672"), scratches * 0.3)
    pores = noise.smoothstep(0.6, 0.9, noise.unit(noise.fbm(n, (180, 5), 3, seed=195)))

    t.height = (-pores * 0.00015 - scratches * 0.00012 + grain * 0.00006).astype(np.float32)
    t.roughness = np.clip(0.22 + 0.35 * worn + 0.2 * scratches + 0.1 * pores, 0.12, 0.9).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicVinylGreen", size=1024, tile_size=1.0)
def clinic_vinyl_green(t):
    """Padded vinyl upholstery, bottle green: examination couches, chair seats, the gurney mattress. Cracked where it creases."""
    n = t.size
    pebble_f, _, _ = noise.worley(n, 130, seed=201)
    pebble = noise.smoothstep(0.1, 0.5, pebble_f)
    color = np.broadcast_to(rgb("#2f4a3c"), (n, n, 3)) * (0.9 + 0.12 * pebble + 0.06 * noise.fbm(n, 5, 4, seed=202))[..., None]
    f1, f2, _ = noise.worley(n, 9, seed=203)
    crack = noise.smoothstep(0.012, 0.0, f2 - f1 + noise.fbm(n, 30, 3, seed=204) * 0.02) * noise.smoothstep(0.5, 0.85, noise.unit(noise.fbm(n, 2, 3, seed=205)))
    color = mix(color, rgb("#b9b39a"), crack * 0.8)  # the backing cloth shows
    shine = noise.smoothstep(0.4, 0.9, noise.unit(noise.fbm(n, 4, 4, seed=206)))
    dirt = grime(n, 207, 0.5)
    color = mix(color, rgb("#3a3528"), dirt * 0.3)

    t.height = (pebble * 0.00018 - crack * 0.0005).astype(np.float32)
    t.roughness = np.clip(0.50 - 0.2 * shine + 0.3 * crack + 0.2 * dirt, 0.2, 0.9).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicFabricCurtain", size=1024, tile_size=1.0)
def clinic_fabric_curtain(t):
    """Woven cotton, washed-out blue-green: cubicle curtains and bed linen. The weave is the detail; stains do the rest."""
    n = t.size
    warp = np.abs(noise.perlin(n, (300, 2), seed=211))
    weft = np.abs(noise.perlin(n, (2, 300), seed=212))
    u, v = noise.grid(n)
    threads = 0.5 + 0.25 * np.sin(u * 2.0 * np.pi * 160.0) + 0.25 * np.sin(v * 2.0 * np.pi * 160.0)
    weave = noise.unit(threads + 0.3 * warp + 0.3 * weft)
    color = np.broadcast_to(rgb("#7f9c96"), (n, n, 3)) * (0.86 + 0.2 * weave + 0.05 * noise.fbm(n, 4, 4, seed=213))[..., None]
    stain_field = noise.unit(noise.fbm(n, 3, 5, seed=214))
    stain = noise.smoothstep(0.62, 0.78, stain_field)
    rim = noise.smoothstep(0.58, 0.62, stain_field) - noise.smoothstep(0.62, 0.66, stain_field)
    color = mix(color, rgb("#8a7d55"), stain * 0.35)
    color = mix(color, rgb("#6b5a36"), np.clip(rim, 0.0, 1.0) * 0.35)

    t.height = (weave * 0.00025).astype(np.float32)
    t.roughness = np.clip(0.85 + 0.08 * weave, 0.7, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 0.9


@material("ClinicMattress", size=1024, tile_size=1.0)
def clinic_mattress(t):
    """Striped ticking over a thin mattress, long past white: stains with dark rims, and worse."""
    n = t.size
    u, v = noise.grid(n)
    stripes = noise.smoothstep(0.42, 0.5, np.abs(((u * 12.0) % 1.0) - 0.5)) * 0.9
    weave = 0.5 + 0.5 * np.sin(v * 2.0 * np.pi * 180.0) * np.sin(u * 2.0 * np.pi * 180.0)
    color = mix(np.broadcast_to(rgb("#c9c2ab"), (n, n, 3)), np.broadcast_to(rgb("#6c7786"), (n, n, 3)), stripes * 0.6)
    color = color * (0.9 + 0.12 * weave)[..., None]
    field = noise.unit(noise.fbm(n, 3, 5, seed=221))
    stain = noise.smoothstep(0.5, 0.7, field)
    rim = noise.smoothstep(0.46, 0.5, field) - noise.smoothstep(0.5, 0.54, field)
    color = mix(color, rgb("#8a7446"), stain * 0.45)
    color = mix(color, rgb("#57401f"), np.clip(rim, 0.0, 1.0) * 0.5)
    blood_field = noise.unit(noise.fbm(n, 4, 5, seed=222))
    blood = noise.smoothstep(0.78, 0.84, blood_field)
    color = mix(color, rgb("#3a100c"), blood * 0.85)
    tuft_f, _, _ = noise.worley(n, 6, seed=223, jitter=0.0)
    tuft = noise.smoothstep(0.12, 0.0, tuft_f)

    t.height = (weave * 0.0002 - tuft * 0.004 + noise.fbm(n, 5, 3, seed=224) * 0.001).astype(np.float32)
    t.roughness = np.clip(0.88 - blood * 0.25, 0.5, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicPlasticBeige", size=512, tile_size=0.5)
def clinic_plastic_beige(t):
    """Injection-moulded ABS gone yellow: monitors, telephones, machine housings, switch plates."""
    n = t.size
    texture = noise.fbm(n, 90, 3, seed=231)
    color = np.broadcast_to(rgb("#c7bd9c"), (n, n, 3)) * (1.0 + 0.03 * texture)[..., None]
    yellow = noise.smoothstep(0.3, 0.9, noise.unit(noise.fbm(n, 3, 4, seed=232)))
    color = mix(color, rgb("#b59f62"), yellow * 0.4)
    grub = grime(n, 233, 0.6)
    color = mix(color, rgb("#5a5444"), grub * 0.3)
    scratches = noise.scratches(n, 16, seed=234, length=(0.04, 0.2), width=1.0)

    t.height = (texture * 0.00006 - scratches * 0.00005).astype(np.float32)
    t.roughness = np.clip(0.46 + 0.08 * texture + 0.2 * grub + 0.15 * scratches, 0.3, 0.9).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.occlusion_strength = 0.4


@material("ClinicCardboard", size=1024, tile_size=1.0)
def clinic_cardboard(t):
    """Corrugated cardboard: the flutes show through the liner as faint ribs; damp has buckled and darkened it."""
    n = t.size
    u, _ = noise.grid(n)
    flutes = 0.5 + 0.5 * np.sin(u * 2.0 * np.pi * 110.0)
    fibre = noise.fbm(n, (20, 120), 4, seed=241)
    color = np.broadcast_to(rgb("#9a7b52"), (n, n, 3)) * (0.92 + 0.08 * fibre + 0.04 * flutes)[..., None]
    damp = noise.smoothstep(0.55, 0.8, noise.unit(noise.fbm(n, 3, 5, seed=242)))
    color = mix(color, rgb("#5a4329"), damp * 0.55)
    tape = noise.tiles(n, 1, 2, gap=0.12, bevel=0.002, seed=243)["joint"]
    color = mix(color, rgb("#b9a069"), tape * 0.5)

    t.height = (flutes * 0.00012 + fibre * 0.00015 + damp * noise.fbm(n, 8, 3, seed=244) * 0.0015).astype(np.float32)
    t.roughness = np.clip(0.82 + 0.08 * fibre - tape * 0.35, 0.35, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)


@material("ClinicPaper", size=512, tile_size=0.5)
def clinic_paper(t):
    """Old paper: forms, charts, wall notices. Foxed and curled; text comes from the signage atlas, not from here."""
    n = t.size
    fibre = noise.fbm(n, 70, 4, seed=251)
    color = np.broadcast_to(rgb("#d6cfb6"), (n, n, 3)) * (1.0 + 0.04 * fibre)[..., None]
    fox = noise.smoothstep(0.26, 0.08, noise.worley(n, 40, seed=252)[0]) * (noise.worley(n, 40, seed=252)[2] > 0.6)
    color = mix(color, rgb("#a07f45"), fox * 0.5)
    edge = noise.smoothstep(0.5, 0.9, noise.unit(noise.fbm(n, 3, 4, seed=253)))
    color = mix(color, rgb("#b7a67a"), edge * 0.4)

    t.height = (fibre * 0.00005 + noise.fbm(n, 4, 3, seed=254) * 0.0006).astype(np.float32)
    t.roughness = np.clip(0.9 + 0.05 * fibre, 0.7, 0.98).astype(np.float32)
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
