"""
Building a character out of grids.

Every surface of a character is a grid of points: rings stacked along a limb, a panel wrapped
round the chest, a band swept round a wrist. A grid keeps three things that a sculpted or remeshed
surface loses, and the rest of the build leans on all three:

  * rings that cross a joint at right angles, which is what bends cleanly;
  * a rectangle of texture per grid, so a seam, a pocket or a bandage wrap is painted where the
    geometry says it is (the painter gets the same row and column coordinates the mesh was built
    from);
  * the point of the bone axis each vertex was built around, which is how skin weights are read
    off the mannequin (see charrig.Rig.reference_weights).

Grids share vertices where a garment is continuous (a sleeve starts on the ring of vertices round
the armhole), so there are no gaps to open when a joint bends.

Coordinates: metres, Z up, the character faces -Y, its left is +X.
"""

import math

import bpy
import numpy as np

from . import charrig, noise

# --------------------------------------------------------------------------------------
# Small numeric helpers
# --------------------------------------------------------------------------------------


def pchip(xs, ys, x):
    """Monotone cubic interpolation of the samples (xs rising, ys) at x: smooth, without overshoot."""
    xs = np.asarray(xs, dtype=np.float64)
    ys = np.asarray(ys, dtype=np.float64)
    x = np.clip(np.asarray(x, dtype=np.float64), xs[0], xs[-1])
    h = np.diff(xs)
    delta = np.diff(ys) / h
    slope = np.zeros_like(ys)
    for i in range(1, len(xs) - 1):
        if delta[i - 1] * delta[i] > 0.0:
            w1, w2 = 2.0 * h[i] + h[i - 1], h[i] + 2.0 * h[i - 1]
            slope[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i])
    slope[0], slope[-1] = delta[0], delta[-1]
    k = np.clip(np.searchsorted(xs, x, side="right") - 1, 0, len(xs) - 2)
    t = (x - xs[k]) / h[k]
    t2, t3 = t * t, t * t * t
    return ((2 * t3 - 3 * t2 + 1) * ys[k] + (t3 - 2 * t2 + t) * h[k] * slope[k] +
            (-2 * t3 + 3 * t2) * ys[k + 1] + (t3 - t2) * h[k] * slope[k + 1])


class Table:
    """Stations along a limb: rows of (key, value, value, ...), read back smoothly at any key."""

    def __init__(self, rows):
        data = np.array(rows, dtype=np.float64)
        order = np.argsort(data[:, 0])
        self.keys = data[order, 0]
        self.values = data[order, 1:]

    def at(self, x):
        """Shape (len(x), values): one column per value."""
        x = np.atleast_1d(np.asarray(x, dtype=np.float64))
        return np.stack([pchip(self.keys, self.values[:, c], x) for c in range(self.values.shape[1])], axis=-1)


def superellipse(theta, exponent):
    """cos and sin of theta bent toward a rounded box: exponent 2 is an ellipse, 4 nearly a rectangle."""
    c, s = np.cos(theta), np.sin(theta)
    e = 2.0 / np.asarray(exponent, dtype=np.float64)
    return np.sign(c) * np.abs(c) ** e, np.sign(s) * np.abs(s) ** e


def smooth01(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def bump(x, centre, width):
    """A gaussian bump of 1 at `centre`."""
    return np.exp(-((np.asarray(x) - centre) / width) ** 2)


def unit(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1.0e-12)


def nearest_on_polyline(points, polyline):
    """The closest point of a polyline (K x 3) to each of the points (N x 3)."""
    points = np.asarray(points, dtype=np.float64)
    polyline = np.asarray(polyline, dtype=np.float64)
    best = np.full(len(points), np.inf)
    out = np.zeros_like(points)
    for a, b in zip(polyline[:-1], polyline[1:]):
        ab = b - a
        t = np.clip(((points - a) @ ab) / max(ab @ ab, 1.0e-12), 0.0, 1.0)
        candidate = a + t[:, None] * ab
        d = np.linalg.norm(points - candidate, axis=1)
        closer = d < best
        best[closer] = d[closer]
        out[closer] = candidate[closer]
    return out


# --------------------------------------------------------------------------------------
# The builder
# --------------------------------------------------------------------------------------
class Island:
    def __init__(self, name, width, height, paint, sheet, marks, detail=1.0):
        self.name = name
        self.width = max(float(width), 0.004)   # metres across (the s direction)
        self.height = max(float(height), 0.004)  # metres along (the t direction)
        self.detail = detail  # share of the sheet's texel density this island gets
        self.paint = paint
        self.sheet = sheet
        self.marks = marks or {}
        self.rect = None  # (x, y, w, h) in texels, without the padding


class Part:
    def __init__(self, name, polyline, deny, weights):
        self.name = name
        self.polyline = polyline  # bone axis the vertices are projected from, or None for nearest
        self.deny = deny          # substrings of bone names this part must not follow
        self.weights = weights    # optional function(points, weights, rig) -> weights, run last


class Builder:
    PAD = 6  # texels of painted margin round every island

    def __init__(self, rig):
        self.rig = rig
        self.pos = []
        self.part_of = []
        self.parts = []
        self.faces = []        # tuples of vertex indices
        self.face_uv = []      # per face: tuple of (s, t) per corner
        self.face_island = []
        self.islands = []
        self.followers = {}    # vertex -> the vertex whose weights it takes
        self._part = None

    # ------------------------------------------------------------------ parts and points
    def part(self, name, polyline=None, deny=(), weights=None):
        """Starts a part: everything added until the next call follows this weight recipe."""
        self.parts.append(Part(name, None if polyline is None else np.array(polyline), tuple(deny), weights))
        self._part = len(self.parts) - 1
        return self._part

    def add(self, points):
        """Adds points of any shape (..., 3) and returns their indices in the same shape."""
        points = np.asarray(points, dtype=np.float64)
        flat = points.reshape(-1, 3)
        start = len(self.pos)
        self.pos.extend(flat)
        self.part_of.extend([self._part] * len(flat))
        return np.arange(start, start + len(flat)).reshape(points.shape[:-1])

    def follow(self, vertex, source):
        """Gives a vertex exactly the weights of another: a lining must never part from its cloth."""
        self.followers[int(vertex)] = int(source)

    def positions(self, indices=None):
        array = np.array(self.pos)
        return array if indices is None else array[np.asarray(indices)]

    def move(self, indices, positions):
        indices = np.asarray(indices).reshape(-1)
        positions = np.asarray(positions).reshape(-1, 3)
        for i, p in zip(indices, positions):
            self.pos[int(i)] = p

    # ------------------------------------------------------------------ grids
    def grid(self, idx, name, paint=None, sheet=0, facing=None, mask=None, marks=None, size=None, s=None, t=None, detail=None):
        """
        Faces between the rows and columns of `idx` (rows x cols of vertex indices; repeat the first
        column at the end for a closed tube, repeat an index along a row for a cap).

        facing   None: faces look away from the part's bone axis. Or a vector, or (rows, cols, 3)
                 vectors, the faces should look along.
        mask     (rows-1, cols-1) booleans, False where no face is wanted.
        marks    {name: row} rows the painter wants to find; it gets their t.
        size     (width, height) in metres of the texture rectangle, measured when not given.
        """
        idx = np.asarray(idx)
        rows, cols = idx.shape
        pos = self.positions(idx)
        valid = idx >= 0

        # t follows the mean distance between rows, s the columns evenly (or by distance if open).
        step = np.linalg.norm(pos[1:] - pos[:-1], axis=-1)
        both = valid[1:] & valid[:-1]
        row_step = np.array([step[r][both[r]].mean() if both[r].any() else 0.0 for r in range(rows - 1)])
        if t is None:
            t = np.concatenate([[0.0], np.cumsum(row_step)])
            t = t / max(t[-1], 1.0e-9)
        across = np.linalg.norm(pos[:, 1:] - pos[:, :-1], axis=-1)
        across_valid = valid[:, 1:] & valid[:, :-1]
        widths = [across[r][across_valid[r]].sum() for r in range(rows) if across_valid[r].all()]
        if not widths:
            widths = [across[r][across_valid[r]].sum() for r in range(rows)]
        if s is None:
            s = np.linspace(0.0, 1.0, cols)
        width = float(np.max(widths)) if size is None else size[0]
        height = float(row_step.sum()) if size is None else size[1]
        if detail is None:
            detail = getattr(paint, "detail", 1.0)
        island = Island(name, width, height, paint, sheet, {k: float(t[v]) for k, v in (marks or {}).items()}, detail)
        self.islands.append(island)
        island_index = len(self.islands) - 1

        new_faces = []
        for r in range(rows - 1):
            for c in range(cols - 1):
                if mask is not None and not mask[r, c]:
                    continue
                corners = [(r, c), (r, c + 1), (r + 1, c + 1), (r + 1, c)]
                if any(idx[a, b] < 0 for a, b in corners):
                    continue
                seen, kept = set(), []
                for a, b in corners:
                    if idx[a, b] not in seen:
                        seen.add(idx[a, b])
                        kept.append((a, b))
                if len(kept) < 3:
                    continue
                new_faces.append(kept)

        # Winding: compare the summed face normals with where the faces should look.
        vote = 0.0
        axis_points = None
        if facing is None:
            flat = pos.reshape(-1, 3)
            part = self.parts[self.part_of[int(idx[valid][0])]]
            axis_points = (nearest_on_polyline(flat, part.polyline) if part.polyline is not None
                           else np.broadcast_to(flat[valid.reshape(-1)].mean(axis=0), flat.shape)).reshape(pos.shape)
        for kept in new_faces:
            p = np.array([pos[a, b] for a, b in kept])
            normal = np.cross(p[1] - p[0], p[2] - p[0])
            if len(kept) == 4:
                normal = np.cross(p[2] - p[0], p[3] - p[1])
            centre = p.mean(axis=0)
            if facing is None:
                want = centre - np.mean([axis_points[a, b] for a, b in kept], axis=0)
            else:
                f = np.asarray(facing, dtype=np.float64)
                want = f if f.ndim == 1 else np.mean([f[a, b] for a, b in kept], axis=0)
            vote += float(normal @ want)
        for kept in new_faces:
            order = kept if vote >= 0.0 else kept[::-1]
            self.faces.append(tuple(int(idx[a, b]) for a, b in order))
            self.face_uv.append(tuple((float(s[b]), float(t[a])) for a, b in order))
            self.face_island.append(island_index)
        return island

    def tube(self, rings, name, paint=None, sheet=0, cap_start=None, cap_end=None, facing=None, marks=None, share=None):
        """
        A closed tube from rings (rows x cols x 3). `cap_start` / `cap_end` are points that close
        an end with a fan. `share` (rows x cols, -1 where a new vertex is wanted) reuses vertices.
        Returns the indices (rows x cols) of the rings, without the caps.
        """
        rings = np.asarray(rings, dtype=np.float64)
        rows, cols, _ = rings.shape
        if share is None:
            idx = self.add(rings)
        else:
            idx = np.array(share)
            fresh = idx == -1
            idx[fresh] = self.add(rings[fresh])
        full = np.concatenate([idx, idx[:, :1]], axis=1)
        offset = 0
        if cap_start is not None:
            full = np.concatenate([np.full((1, cols + 1), int(self.add(np.asarray(cap_start)[None])[0])), full], axis=0)
            offset = 1
        if cap_end is not None:
            full = np.concatenate([full, np.full((1, cols + 1), int(self.add(np.asarray(cap_end)[None])[0]))], axis=0)
        marks = {k: v + offset for k, v in (marks or {}).items()}
        self.grid(full, name, paint, sheet, facing, marks=marks)
        return idx

    def band(self, path, outward, up, half_thickness, half_height, name, paint=None, sheet=0, section=10, exponent=2.0, tilt=0.0):
        """
        A ring of material swept round a closed path (cols x 3): a cuff, a collar, a scarf, a strap
        end. The section is a superellipse `half_thickness` along `outward` and `half_height` along
        `up` (both cols x 3, or single vectors; the sizes may vary per column).
        """
        path = np.asarray(path, dtype=np.float64)
        cols = len(path)
        outward = unit(np.broadcast_to(outward, path.shape))
        up = unit(np.broadcast_to(up, path.shape))
        half_thickness = np.broadcast_to(half_thickness, (cols,))
        half_height = np.broadcast_to(half_height, (cols,))
        phi = np.linspace(0.0, 2.0 * math.pi, section, endpoint=False) + tilt
        c, s = superellipse(phi, exponent)
        rings = (path[None] + c[:, None, None] * (outward * half_thickness[:, None])[None] +
                 s[:, None, None] * (up * half_height[:, None])[None])
        idx = self.add(rings)
        full = np.concatenate([idx, idx[:1]], axis=0)
        full = np.concatenate([full, full[:, :1]], axis=1)
        facing = c[:, None, None] * outward[None] + s[:, None, None] * up[None]
        facing = np.concatenate([facing, facing[:1]], axis=0)
        facing = np.concatenate([facing, facing[:, :1]], axis=1)
        self.grid(full, name, paint, sheet, facing=facing)
        return idx

    def patch(self, points, normals, lift, name, paint=None, sheet=0, sink=0.004, border=1):
        """
        A layer lying on a surface: a pocket flap, a strap, a bib. `points` (rows x cols x 3) are on
        the surface and `normals` point out of it. The layer is raised by `lift` and its outer rows
        and columns dive back under the surface, so it needs no back faces.
        """
        points = np.asarray(points, dtype=np.float64)
        normals = unit(normals)
        rows, cols, _ = points.shape
        height = np.full((rows, cols), float(lift)) if np.isscalar(lift) else np.array(lift, dtype=np.float64)
        height = np.broadcast_to(height, (rows, cols)).copy()
        edge = np.zeros((rows, cols), dtype=bool)
        edge[:border], edge[-border:], edge[:, :border], edge[:, -border:] = True, True, True, True
        height[edge] = -sink
        idx = self.add(points + normals * height[..., None])
        self.grid(idx, name, paint, sheet, facing=normals)
        return idx

    def slab(self, points, normals, thickness, name, paint=None, sheet=0):
        """
        A free-hanging sheet with two sides and a rim: a collar wing, an apron, a tie. `points`
        (rows x cols x 3) is the outer side; the inner side is `thickness` behind it.
        """
        points = np.asarray(points, dtype=np.float64)
        normals = unit(normals)
        rows, cols, _ = points.shape
        outer = self.add(points)
        inner = self.add(points - normals * thickness)
        # One closed loop of columns: across the outer side and back along the inner one.
        loop = np.concatenate([outer, inner[:, ::-1], outer[:, :1]], axis=1)
        facing = np.concatenate([normals, -normals[:, ::-1], normals[:, :1]], axis=1)
        self.grid(loop, name, paint, sheet, facing=facing)
        for label, row in (("a", 0), ("b", rows - 1)):
            end = np.stack([outer[row], inner[row]], axis=0)
            along = points[row] - points[1 if row == 0 else rows - 2]
            self.grid(end, name + "_end_" + label, paint, sheet, facing=along.mean(axis=0))
        return outer

    def close_opening(self, ring, centre, inward, thickness, depth, name, paint=None, sheet=0, plug=True):
        """
        Gives the open end of a garment an inside: a lip turned in by `thickness`, a wall going
        `depth` along `inward`, and (with `plug`) a floor, so nobody sees out through the back
        faces. Whatever comes out of the opening simply passes through the floor.
        """
        ring = np.asarray(ring).reshape(-1)
        p = self.positions(ring)
        centre = np.asarray(centre, dtype=np.float64)
        inward = unit(inward)
        to_centre = centre - p
        to_centre -= np.outer(to_centre @ inward, inward)
        radial = unit(to_centre)
        lip = p + radial * thickness
        wall = lip + inward * depth + radial * thickness
        rows = [ring, self.add(lip), self.add(wall)]
        if plug:
            rows.append(np.full(len(ring), int(self.add((centre + inward * depth * 1.15)[None])[0])))
        idx = np.stack(rows, axis=0)
        idx = np.concatenate([idx, idx[:, :1]], axis=1)
        # The lip looks back along the opening, the wall at the axis, the floor out of the opening.
        normals = np.broadcast_to(-inward, p.shape)
        facing = np.stack([normals - radial, normals, radial - inward * 0.3] + ([normals] if plug else []), axis=0)
        facing = np.concatenate([facing, facing[:, :1]], axis=1)
        self.grid(idx, name, paint, sheet, facing=facing, detail=0.5)
        return rows[1]

    # ------------------------------------------------------------------ shaping after the fact
    def edges(self):
        pairs = set()
        for face in self.faces:
            for a, b in zip(face, face[1:] + face[:1]):
                pairs.add((min(a, b), max(a, b)))
        return np.array(sorted(pairs), dtype=np.int64)

    def relax(self, centre, radius, iterations=6, strength=0.6, since=0):
        """
        Laplacian smoothing of the vertices within `radius` of `centre`, fading to the rim. Only
        vertices added from index `since` on move, so a piece can be relaxed without disturbing
        what was built before it.
        """
        pos = self.positions()
        distance = np.linalg.norm(pos - np.asarray(centre), axis=1)
        factor = smooth01(1.0 - distance / radius) * strength
        factor[:since] = 0.0
        active = factor > 0.0
        if not active.any():
            return
        edges = self.edges()
        a = np.concatenate([edges[:, 0], edges[:, 1]])
        b = np.concatenate([edges[:, 1], edges[:, 0]])
        keep = active[a]
        a, b = a[keep], b[keep]
        degree = np.maximum(np.bincount(a, minlength=len(pos)), 1)[:, None]
        for _ in range(iterations):
            total = np.zeros_like(pos)
            np.add.at(total, a, pos[b])
            average = total / degree
            pos[active] += (average[active] - pos[active]) * factor[active, None]
        self.pos = list(pos)

    # ------------------------------------------------------------------ weights
    def weights(self):
        rig = self.rig
        pos = self.positions()
        part_of = np.array(self.part_of)
        out = np.zeros((len(pos), len(rig.bones)), dtype=np.float32)
        for index, part in enumerate(self.parts):
            mine = np.nonzero(part_of == index)[0]
            if len(mine) == 0:
                continue
            points = pos[mine]
            axis = nearest_on_polyline(points, part.polyline) if part.polyline is not None else None
            w = rig.reference_weights(points, axis)
            for column, bone in enumerate(rig.bones):
                if any(token in bone for token in part.deny):
                    w[:, column] = 0.0
            total = w.sum(axis=1, keepdims=True)
            empty = total[:, 0] < 1.0e-5
            if empty.any():
                # Everything the mannequin offered was denied: fall back to the nearest allowed bone.
                allowed = [c for c, bone in enumerate(rig.bones) if not any(token in bone for token in part.deny)]
                heads = np.array([rig.head[rig.bones[c]] for c in allowed])
                nearest = np.argmin(np.linalg.norm(points[empty][:, None] - heads[None], axis=-1), axis=1)
                w[np.nonzero(empty)[0], np.array(allowed)[nearest]] = 1.0
                total = w.sum(axis=1, keepdims=True)
            out[mine] = w / total
        # A pass of smoothing takes out the triangle-to-triangle noise of the reference.
        out = charrig.smooth_weights(out, self.edges(), iterations=3, factor=0.5)
        for index, part in enumerate(self.parts):
            if part.weights is None:
                continue
            mine = np.nonzero(part_of == index)[0]
            if len(mine):
                out[mine] = part.weights(pos[mine], out[mine], rig)
        out = charrig.limit_weights(out)
        if self.followers:
            targets = np.array(list(self.followers.keys()))
            out[targets] = out[np.array(list(self.followers.values()))]
        return out

    # ------------------------------------------------------------------ the atlas
    def pack(self, sizes):
        """Places every island in its sheet's square. Returns the texel density of each sheet."""
        densities = {}
        for sheet, size in sizes.items():
            islands = [i for i in self.islands if i.sheet == sheet]
            if not islands:
                continue
            order = sorted(islands, key=lambda i: -i.height)

            def place(density, commit):
                x, y, shelf = 0, 0, 0
                for island in order:
                    w = int(math.ceil(island.width * density * island.detail)) + 2 * self.PAD
                    h = int(math.ceil(island.height * density * island.detail)) + 2 * self.PAD
                    if w > size:
                        return False
                    if x + w > size:
                        x, y, shelf = 0, y + shelf, 0
                    if y + h > size:
                        return False
                    if commit:
                        island.rect = (x + self.PAD, y + self.PAD, w - 2 * self.PAD, h - 2 * self.PAD)
                    x += w
                    shelf = max(shelf, h)
                return True

            low, high = 20.0, 6000.0
            for _ in range(40):
                middle = 0.5 * (low + high)
                if place(middle, False):
                    low = middle
                else:
                    high = middle
            place(low, True)
            densities[sheet] = low
        return densities

    # ------------------------------------------------------------------ the Blender object
    def to_object(self, name, material_names, sizes):
        """The mesh object, UV-mapped into the packed atlas, one material per sheet."""
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(p) for p in self.pos], [], self.faces)
        mesh.update()
        uv_layer = mesh.uv_layers.new(name="UVMap")
        uvs = np.zeros((len(mesh.loops), 2), dtype=np.float32)
        material_index = np.zeros(len(mesh.polygons), dtype=np.int32)
        sheets = sorted(sizes)
        for polygon in mesh.polygons:
            island = self.islands[self.face_island[polygon.index]]
            x, y, w, h = island.rect
            size = sizes[island.sheet]
            material_index[polygon.index] = sheets.index(island.sheet)
            corner_uv = self.face_uv[polygon.index]
            # from_pydata keeps the corner order of the face tuple.
            for k, loop_index in enumerate(polygon.loop_indices):
                s, t = corner_uv[k]
                uvs[loop_index] = ((x + s * w) / size, (y + t * h) / size)
        uv_layer.data.foreach_set("uv", uvs.reshape(-1))
        mesh.polygons.foreach_set("material_index", material_index)
        mesh.polygons.foreach_set("use_smooth", np.ones(len(mesh.polygons), dtype=bool))
        for material_name in material_names:
            mesh.materials.append(bpy.data.materials.get(material_name) or bpy.data.materials.new(material_name))
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj

    def triangle_count(self):
        return sum(len(face) - 2 for face in self.faces)


# --------------------------------------------------------------------------------------
# Painting
# --------------------------------------------------------------------------------------
class Region:
    """What a painter sees of one island: coordinates, and views into the sheet's maps."""

    def __init__(self, canvas, island, pad):
        x, y, w, h = island.rect
        self.island = island
        self.rows = slice(max(y - pad, 0), min(y + h + pad, canvas.size))
        self.cols = slice(max(x - pad, 0), min(x + w + pad, canvas.size))
        px = np.arange(self.cols.start, self.cols.stop, dtype=np.float32) + 0.5
        py = np.arange(self.rows.start, self.rows.stop, dtype=np.float32) + 0.5
        s, t = np.meshgrid((px - x) / w, (py - y) / h)
        self.S, self.T = s, t                     # 0..1 across and along the grid (beyond, in the margin)
        self.X, self.Y = s * island.width, t * island.height  # the same in metres
        self.width, self.height = island.width, island.height
        self.texel = island.width / max(w, 1)   # metres per texel in this island
        self.marks = island.marks
        self.color = canvas.color[self.rows, self.cols]
        self.relief = canvas.height[self.rows, self.cols]   # metres, for the normal map
        self.rough = canvas.roughness[self.rows, self.cols]
        self.metal = canvas.metallic[self.rows, self.cols]
        self.shade = canvas.shade[self.rows, self.cols]      # painted occlusion, 1 = open
        self._canvas = canvas

    def noise(self, cells, seed=0, octaves=4):
        """Noise in about -1..1 over the region, cut from a field shared by the sheet."""
        return self._canvas.field(cells, seed, octaves)[self.rows, self.cols]

    def fill(self, color, rough=0.8, metal=0.0):
        self.color[...] = np.asarray(color, dtype=np.float32)
        self.rough[...] = rough
        self.metal[...] = metal
        self.relief[...] = 0.0
        self.shade[...] = 1.0

    def tint(self, color, mask):
        mask = np.clip(mask, 0.0, 1.0)[..., None]
        self.color[...] = self.color * (1.0 - mask) + np.asarray(color, dtype=np.float32) * mask


class Canvas:
    """The maps of one sheet, in the engine's layout: row 0 is v = 0."""

    def __init__(self, size, density):
        self.size = size
        self.density = density  # texels per metre
        self.color = np.full((size, size, 3), 0.25, dtype=np.float32)
        self.height = np.zeros((size, size), dtype=np.float32)
        self.roughness = np.full((size, size), 0.8, dtype=np.float32)
        self.metallic = np.zeros((size, size), dtype=np.float32)
        self.shade = np.ones((size, size), dtype=np.float32)
        self._fields = {}

    def field(self, cells, seed, octaves):
        key = (cells, seed, octaves)
        if key not in self._fields:
            self._fields[key] = noise.fbm(self.size, cells, octaves, seed)
        return self._fields[key]

    def normal_map(self):
        """x follows +u, y follows +v (rising row): the tangent basis the engine builds from the UVs."""
        texel = 1.0 / self.density
        du = (np.roll(self.height, -1, axis=1) - np.roll(self.height, 1, axis=1)) / (2.0 * texel)
        dv = (np.roll(self.height, -1, axis=0) - np.roll(self.height, 1, axis=0)) / (2.0 * texel)
        normal = np.stack([-du, -dv, np.ones_like(du)], axis=-1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        return normal.astype(np.float32)


def paint(builder, sizes, densities):
    """Runs every island's painter. Returns {sheet: Canvas}."""
    canvases = {sheet: Canvas(size, densities[sheet]) for sheet, size in sizes.items() if sheet in densities}
    for island in builder.islands:
        canvas = canvases[island.sheet]
        region = Region(canvas, island, builder.PAD)
        if island.paint is None:
            region.fill((0.35, 0.35, 0.35))
        else:
            island.paint(region)
    return canvases
