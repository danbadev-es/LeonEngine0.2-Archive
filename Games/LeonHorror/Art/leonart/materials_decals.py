"""
Decals: what has happened to the clinic's surfaces. Each is laid over a wall or a floor on a quad of
its own (SM_Decal_* in decals.py), so one texture marks any surface without the surface needing a
second set of maps.

A decal is a texture set with an alpha channel. It does not tile: the alpha is nothing at the
borders, and `tile_size` is the size in metres of the square it was painted for.
"""

import numpy as np

from . import noise
from .materials import material, mix, rgb


def falloff(size, inner=0.55, outer=0.98, wobble=0.0, seed=0, centre=(0.5, 0.5), stretch=(1.0, 1.0)):
    """1 in the middle of the image, 0 at its borders, with the edge pushed in and out by noise."""
    u, v = noise.grid(size)
    distance = np.sqrt(((u - centre[0]) * 2.0 / stretch[0]) ** 2 + ((v - centre[1]) * 2.0 / stretch[1]) ** 2)
    if wobble > 0.0:
        distance = distance + (noise.unit(noise.fbm(size, 5, 4, seed)) - 0.5) * wobble
    return noise.smoothstep(outer, inner, distance)


def border(size, width=0.06):
    """0 on the border of the image rising to 1 `width` inside it: whatever is painted ends before the quad does."""
    u, v = noise.grid(size)
    edge = np.minimum(np.minimum(u, 1.0 - u), np.minimum(v, 1.0 - v))
    return noise.smoothstep(0.0, width, edge)


def finish(t, color, alpha, roughness, height=None):
    n = t.size
    t.color = np.clip(color, 0.0, 1.0).astype(np.float32)
    t.alpha = np.clip(alpha * border(n), 0.0, 1.0).astype(np.float32)
    t.roughness = np.clip(roughness, 0.03, 1.0).astype(np.float32)
    t.height = (height if height is not None else np.zeros((n, n))).astype(np.float32)
    # Nothing of the surface under a decal is hidden by its occlusion.
    t.occlusion = np.ones((n, n), dtype=np.float32)


@material("DecalDamp", size=1024, tile_size=1.6)
def decal_damp(t):
    """A damp patch on plaster or a ceiling: a brown tide line around a paler middle, with mould in it."""
    n = t.size
    body = falloff(n, 0.35, 0.9, wobble=0.5, seed=301)
    inner = falloff(n, 0.2, 0.7, wobble=0.55, seed=302)
    ring = np.clip(body - inner * 0.85, 0.0, 1.0)
    second = np.clip(falloff(n, 0.15, 0.55, wobble=0.5, seed=303) - falloff(n, 0.1, 0.45, wobble=0.5, seed=304) * 0.9, 0.0, 1.0)
    mould = noise.smoothstep(0.62, 0.8, noise.unit(noise.fbm(n, 28, 4, seed=305))) * inner
    color = np.broadcast_to(rgb("#7d6a45"), (n, n, 3)).copy()
    color = mix(color, rgb("#4f3f25"), np.clip(ring * 1.4 + second, 0.0, 1.0))
    color = mix(color, rgb("#2c3324"), mould * 0.8)
    alpha = np.clip(body * 0.22 + ring * 0.55 + second * 0.35 + mould * 0.5, 0.0, 0.85)
    finish(t, color, alpha, 0.85 - 0.25 * inner, height=-mould * 0.0003)


@material("DecalBloodPool", size=1024, tile_size=1.4)
def decal_blood_pool(t):
    """Blood that pooled and half dried: nearly black and glossy in the middle, brown and matt at the rim, drops thrown around it."""
    n = t.size
    pool = falloff(n, 0.3, 0.62, wobble=0.45, seed=311, centre=(0.48, 0.52))
    pool = noise.smoothstep(0.35, 0.6, pool)
    f1, _, _ = noise.worley(n, 9, seed=312)
    drops = noise.smoothstep(0.045, 0.02, f1) * noise.smoothstep(0.55, 0.75, noise.unit(noise.fbm(n, 4, 3, seed=313))) * falloff(n, 0.5, 0.95)
    fine = noise.smoothstep(0.02, 0.008, noise.worley(n, 30, seed=314)[0]) * noise.smoothstep(0.6, 0.8, noise.unit(noise.fbm(n, 6, 3, seed=315))) * falloff(n, 0.4, 0.9)
    wet = noise.smoothstep(0.5, 0.95, falloff(n, 0.1, 0.5, wobble=0.4, seed=316, centre=(0.48, 0.52))) * pool
    edge = np.clip(pool - noise.blur(pool, max(2, n // 80)), 0.0, 1.0) * 4.0
    color = np.broadcast_to(rgb("#3a0b08"), (n, n, 3)).copy()
    color = mix(color, rgb("#170303"), wet)
    color = mix(color, rgb("#2a1108"), np.clip(edge, 0.0, 1.0) * 0.8)
    alpha = np.clip(pool * 0.93 + drops * 0.9 + fine * 0.8, 0.0, 0.96)
    roughness = 0.55 - 0.45 * wet + 0.3 * np.clip(edge, 0.0, 1.0)
    finish(t, color, alpha, roughness, height=pool * 0.0004)


@material("DecalBloodSmear", size=1024, tile_size=2.0)
def decal_blood_smear(t):
    """Something bleeding was dragged: long streaks along +u that thin out and break up, a hand's width of them."""
    n = t.size
    u, v = noise.grid(n)
    streaks = noise.unit(noise.fbm(n, (2, 46), 4, seed=321))
    streaks = noise.smoothstep(0.42, 0.72, streaks)
    band = noise.smoothstep(0.34, 0.12, np.abs(v - 0.5 + 0.06 * np.sin(u * 5.0) + 0.04 * (noise.unit(noise.fbm(n, 3, 3, seed=322)) - 0.5)))
    thinning = noise.smoothstep(1.0, 0.15, u) * noise.smoothstep(0.0, 0.06, u)
    gaps = noise.smoothstep(0.3, 0.6, noise.unit(noise.fbm(n, (7, 3), 3, seed=323)) + thinning * 0.5)
    smear = streaks * band * thinning * gaps
    start = falloff(n, 0.1, 0.3, wobble=0.25, seed=324, centre=(0.12, 0.5), stretch=(0.8, 1.2))
    color = np.broadcast_to(rgb("#3d0d09"), (n, n, 3)).copy()
    color = mix(color, rgb("#1c0504"), start)
    color = mix(color, rgb("#3a1a10"), noise.smoothstep(0.3, 0.9, u) * 0.6)
    alpha = np.clip(smear * 0.8 + start * 0.9, 0.0, 0.94)
    finish(t, color, alpha, 0.5 + 0.35 * u - 0.3 * start)


@material("DecalGrime", size=1024, tile_size=2.0)
def decal_grime(t):
    """Dirt trodden into a floor or gathered where a wall meets it: dark, soft, matt. Thickest along the low edge (v = 0)."""
    n = t.size
    u, v = noise.grid(n)
    patches = noise.unit(noise.fbm(n, 4, 5, seed=331))
    breakup = noise.unit(noise.fbm(n, 30, 4, seed=332))
    low = noise.smoothstep(0.75, 0.0, v + (patches - 0.5) * 0.5)
    dirt = np.clip(low * (0.45 + 0.55 * breakup) + noise.smoothstep(0.6, 0.85, patches) * 0.35 * (1.0 - v), 0.0, 1.0)
    scuffs = noise.scratches(n, 26, seed=333, length=(0.08, 0.3), width=2.0, direction=0.0, spread=0.5) * noise.smoothstep(0.9, 0.2, v)
    color = np.broadcast_to(rgb("#2b2720"), (n, n, 3)).copy()
    color = mix(color, rgb("#15130f"), noise.smoothstep(0.5, 0.9, dirt))
    alpha = np.clip(dirt * 0.7 + scuffs * 0.5, 0.0, 0.85)
    finish(t, color, alpha, 0.9 - 0.15 * scuffs)


@material("DecalScratches", size=1024, tile_size=1.2)
def decal_scratches(t):
    """A wall clawed at: bundles of four or five gouges through the paint to the pale plaster, running down and across."""
    n = t.size
    rng = np.random.default_rng(341)
    u, v = noise.grid(n)
    gouges = np.zeros((n, n), dtype=np.float32)
    for bundle in range(5):
        centre_u, centre_v = rng.uniform(0.25, 0.75), rng.uniform(0.3, 0.7)
        angle = rng.uniform(-1.9, -1.2)
        along = np.array([np.cos(angle), np.sin(angle)], dtype=np.float32)
        across = np.array([-along[1], along[0]], dtype=np.float32)
        length = rng.uniform(0.18, 0.34)
        for finger in range(rng.integers(3, 6)):
            offset = (finger - 2.0) * 0.022 + rng.uniform(-0.004, 0.004)
            a = (u - centre_u) * along[0] + (v - centre_v) * along[1]
            c = (u - centre_u) * across[0] + (v - centre_v) * across[1] - offset - 0.0025 * np.sin(a * 14.0 + finger)
            reach = length * rng.uniform(0.7, 1.0)
            body = noise.smoothstep(reach, reach * 0.55, np.abs(a - rng.uniform(-0.03, 0.03)))
            width = 0.0035 * rng.uniform(0.7, 1.3) * (0.4 + 0.6 * body)
            # The width varies along the gouge, so the edge is worked out per texel.
            gouges = np.maximum(gouges, np.clip((width - np.abs(c)) / (width * 0.7), 0.0, 1.0) * body)
    ragged = noise.unit(noise.fbm(n, 60, 3, seed=342))
    gouges = gouges * (0.65 + 0.35 * ragged)
    dust = noise.blur(gouges, max(2, n // 120)) * 0.5
    color = np.broadcast_to(rgb("#cfc8b4"), (n, n, 3)).copy()
    color = mix(color, rgb("#8d8672"), noise.smoothstep(0.5, 1.0, gouges))
    alpha = np.clip(gouges * 0.95 + dust * 0.25, 0.0, 0.95)
    finish(t, color, alpha, 0.9 * np.ones((n, n)), height=-gouges * 0.0012)
