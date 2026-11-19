"""
Tileable noise and patterns.

Every function returns a float32 array of shape (size, size) that repeats seamlessly: the value at
the right edge continues at the left, and the same top to bottom. Coordinates run 0..1 over the
image; `cells` (or `scale`) is how many features fit across it and has to be a whole number for the
result to tile. Everything is seeded, so a material comes out the same on every run.
"""

import numpy as np


def grid(size):
    """u and v coordinates of the texel centres, each of shape (size, size). v is the row."""
    axis = (np.arange(size, dtype=np.float32) + 0.5) / size
    u, v = np.meshgrid(axis, axis)
    return u, v


def _fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def perlin(size, cells, seed=0):
    """
    Gradient noise in about -1..1. `cells` is the number of lattice cells across the image, or a
    pair (across, down) for noise stretched one way: (4, 64) gives horizontal streaks.
    """
    if isinstance(cells, (tuple, list)):
        cells_x, cells_y = max(1, int(cells[0])), max(1, int(cells[1]))
    else:
        cells_x = cells_y = max(1, int(cells))
    rng = np.random.default_rng(seed)
    angles = rng.uniform(0.0, 2.0 * np.pi, (cells_y, cells_x)).astype(np.float32)
    gx, gy = np.cos(angles), np.sin(angles)

    u, v = grid(size)
    x, y = u * cells_x, v * cells_y
    x0, y0 = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32)
    fx, fy = x - x0, y - y0
    x0 %= cells_x
    y0 %= cells_y
    x1, y1 = (x0 + 1) % cells_x, (y0 + 1) % cells_y

    def corner(ix, iy, dx, dy):
        return gx[iy, ix] * dx + gy[iy, ix] * dy

    n00 = corner(x0, y0, fx, fy)
    n10 = corner(x1, y0, fx - 1.0, fy)
    n01 = corner(x0, y1, fx, fy - 1.0)
    n11 = corner(x1, y1, fx - 1.0, fy - 1.0)
    sx, sy = _fade(fx), _fade(fy)
    top = n00 + (n10 - n00) * sx
    bottom = n01 + (n11 - n01) * sx
    return ((top + (bottom - top) * sy) * 1.41421356).astype(np.float32)


def fbm(size, cells, octaves=5, seed=0, gain=0.5, lacunarity=2):
    """Layers of perlin noise, each finer and weaker. Roughly -1..1. `cells` as in perlin."""
    total = np.zeros((size, size), dtype=np.float32)
    amplitude, norm = 1.0, 0.0
    if isinstance(cells, (tuple, list)):
        frequency = [max(1, int(cells[0])), max(1, int(cells[1]))]
    else:
        frequency = [max(1, int(cells)), max(1, int(cells))]
    for octave in range(octaves):
        if max(frequency) > size // 2:
            break
        total += amplitude * perlin(size, tuple(frequency), seed + octave * 101)
        norm += amplitude
        amplitude *= gain
        frequency = [f * int(lacunarity) for f in frequency]
    return total / max(norm, 1.0e-6)


def unit(values):
    """Stretches an array to exactly 0..1."""
    low, high = float(values.min()), float(values.max())
    if high - low < 1.0e-9:
        return np.zeros_like(values, dtype=np.float32)
    return ((values - low) / (high - low)).astype(np.float32)


def white(size, seed=0):
    """Independent random values per texel, 0..1: grain."""
    return np.random.default_rng(seed).random((size, size), dtype=np.float32)


def blur(values, radius):
    """Box blur that wraps around the edges; apply twice for something close to a gaussian."""
    radius = int(radius)
    if radius <= 0:
        return values.astype(np.float32)
    out = values.astype(np.float32)
    for axis in (0, 1):
        padded = np.concatenate([out.take(range(-radius, 0), axis=axis), out, out.take(range(0, radius), axis=axis)], axis=axis)
        total = np.cumsum(padded, axis=axis, dtype=np.float64)
        zero = np.zeros_like(total.take([0], axis=axis))
        total = np.concatenate([zero, total], axis=axis)
        window = 2 * radius + 1
        upper = total.take(range(window, window + out.shape[axis]), axis=axis)
        lower = total.take(range(0, out.shape[axis]), axis=axis)
        out = ((upper - lower) / window).astype(np.float32)
    return out


def smoothstep(edge0, edge1, values):
    """
    0 at edge0, 1 at edge1, eased in between. The edges may be given in falling order
    (smoothstep(0.3, 0.1, d) is 1 near d = 0.1 and 0 beyond 0.3), which reads naturally for distances.
    """
    span = edge1 - edge0
    if abs(span) < 1.0e-9:
        return (np.asarray(values) >= edge1).astype(np.float32)
    t = np.clip((values - edge0) / span, 0.0, 1.0)
    return (t * t * (3.0 - 2.0 * t)).astype(np.float32)


def worley(size, cells, seed=0, jitter=1.0):
    """
    Cellular pattern: one random point per lattice cell. Returns (f1, f2, cell_id) where f1 and f2
    are the distances to the nearest and second nearest point, in cell units, and cell_id is a
    random value 0..1 that is the same over each cell.
    """
    cells = max(1, int(cells))
    rng = np.random.default_rng(seed)
    px = rng.random((cells, cells), dtype=np.float32) * jitter + (1.0 - jitter) * 0.5
    py = rng.random((cells, cells), dtype=np.float32) * jitter + (1.0 - jitter) * 0.5
    ids = rng.random((cells, cells), dtype=np.float32)

    u, v = grid(size)
    x, y = u * cells, v * cells
    cx, cy = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32)
    f1 = np.full((size, size), 1.0e9, dtype=np.float32)
    f2 = np.full((size, size), 1.0e9, dtype=np.float32)
    cell_id = np.zeros((size, size), dtype=np.float32)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            ix, iy = (cx + ox) % cells, (cy + oy) % cells
            dx = (cx + ox + px[iy, ix]) - x
            dy = (cy + oy + py[iy, ix]) - y
            distance = np.sqrt(dx * dx + dy * dy)
            closer = distance < f1
            f2 = np.where(closer, f1, np.minimum(f2, distance))
            cell_id = np.where(closer, ids[iy, ix], cell_id)
            f1 = np.where(closer, distance, f1)
    return f1, f2, cell_id


def tiles(size, across, down=None, gap=0.02, bevel=0.03, offset=0.0, seed=0):
    """
    A grid of rectangular tiles. `gap` and `bevel` are fractions of a tile's width.

    Returns a dict:
        height   0 in the joints rising to 1 on the face of a tile
        joint    1 in the joints, 0 on the tiles
        id       a random value 0..1 per tile (the same wherever that tile repeats)
        u, v     0..1 across each tile
    `offset` shifts every other row by that fraction of a tile, as in brickwork (0.5 = half bond).
    """
    across = max(1, int(across))
    down = across if down is None else max(1, int(down))
    u, v = grid(size)
    row = np.floor(v * down).astype(np.int32)
    shifted = u * across + np.where(row % 2 == 1, offset, 0.0)
    column = np.floor(shifted).astype(np.int32) % across
    tu = shifted - np.floor(shifted)
    tv = v * down - row

    # Distance to the nearest edge of the tile, in tile widths (rows may be shorter than wide).
    aspect = across / down
    edge = np.minimum(np.minimum(tu, 1.0 - tu), np.minimum(tv, 1.0 - tv) / aspect)
    height = smoothstep(gap * 0.5, gap * 0.5 + max(bevel, 1.0e-4), edge)
    joint = 1.0 - smoothstep(gap * 0.25, gap * 0.5, edge)
    ids = np.random.default_rng(seed).random((down, across), dtype=np.float32)
    return {"height": height, "joint": joint.astype(np.float32), "id": ids[row % down, column], "u": tu.astype(np.float32),
            "v": tv.astype(np.float32)}


def scratches(size, count, seed=0, length=(0.05, 0.3), width=1.2, direction=None, spread=np.pi):
    """
    Thin straight scratches, wrapping at the edges. Returns 0..1, 1 on a scratch.
    `length` is a range as a fraction of the image, `width` is in texels, `direction` in radians is
    the mean angle (None: any) and `spread` how far they stray from it.
    """
    rng = np.random.default_rng(seed)
    out = np.zeros((size, size), dtype=np.float32)
    for _ in range(int(count)):
        cx, cy = rng.random() * size, rng.random() * size
        angle = rng.uniform(0.0, np.pi) if direction is None else direction + rng.uniform(-spread, spread)
        half = rng.uniform(length[0], length[1]) * 0.5 * size
        strength = rng.uniform(0.35, 1.0)
        ax, ay = np.cos(angle), np.sin(angle)
        # Only the texels around the scratch are touched; indices wrap so the pattern tiles.
        reach_x = int(abs(ax) * half + width + 2)
        reach_y = int(abs(ay) * half + width + 2)
        xs = np.arange(int(cx) - reach_x, int(cx) + reach_x + 1)
        ys = np.arange(int(cy) - reach_y, int(cy) + reach_y + 1)
        dx = (xs + 0.5 - cx)[None, :]
        dy = (ys + 0.5 - cy)[:, None]
        along = dx * ax + dy * ay
        across = np.abs(-dx * ay + dy * ax)
        line = np.clip(1.0 - across / width, 0.0, 1.0) * smoothstep(half, half * 0.6, np.abs(along)) * strength
        index = np.ix_(ys % size, xs % size)
        out[index] = np.maximum(out[index], line.astype(np.float32))
    return out


def ramp(values, stops):
    """
    Maps 0..1 values through a colour ramp. stops: [(position, (r, g, b)), ...] in rising position.
    Returns an array of shape (size, size, 3).
    """
    positions = np.array([stop[0] for stop in stops], dtype=np.float32)
    colors = np.array([stop[1] for stop in stops], dtype=np.float32)
    flat = np.clip(values, 0.0, 1.0)
    return np.stack([np.interp(flat, positions, colors[:, channel]) for channel in range(3)], axis=-1).astype(np.float32)
