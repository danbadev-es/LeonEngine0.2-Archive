"""
Painting the characters' textures.

A painter is a function of a charbody.Region: it sees one grid of the mesh as a rectangle, with S
and T running 0..1 across the columns and along the rows and X and Y the same in metres, and it
writes colour, relief (metres, which becomes the normal map), roughness, metalness and shade.
Because the rectangle is the grid, a seam or a pocket is painted where the geometry has it: "down
the back" is a value of S, "at the elbow" is a mark the mesh builder left on a row.
"""

import math

import numpy as np

from .materials import rgb
from .noise import smoothstep

TWO_PI = 2.0 * math.pi


def painter(palette, kind, detail=1.0):
    def run(region):
        palette["paint"][kind](region, palette)
    run.detail = detail
    return run


# --------------------------------------------------------------------------------------
# Brushes
# --------------------------------------------------------------------------------------
def fabric(R, color, rough=0.85, weave="twill", period=0.006, depth=0.00025, seed=0, mottle=0.10):
    """A cloth ground: colour with soft blotches and grain, and a weave in the relief."""
    R.fill(color, rough)
    big, mid, fine = R.noise(10, seed + 1), R.noise(60, seed + 2), R.noise(500, seed + 3, 2)
    R.color *= (1.0 + mottle * big + 0.5 * mottle * mid + 0.07 * fine)[..., None]
    k = TWO_PI / period
    if weave == "twill":
        pattern = np.sin((R.X + R.Y) * k)
    elif weave == "plain":
        pattern = np.sin(R.X * k) * np.sin(R.Y * k)
    elif weave == "rib":       # ribs running along the rows
        pattern = np.cos(R.X * k)
    elif weave == "rib_across":
        pattern = np.cos(R.Y * k)
    elif weave == "knit":
        pattern = np.cos(R.X * k) * (0.7 + 0.3 * np.cos(R.Y * k * 1.5))
    else:
        pattern = np.zeros_like(R.X)
    R.relief += depth * pattern + depth * 0.5 * fine
    R.color *= (1.0 + 0.06 * pattern)[..., None]
    R.rough += 0.04 * fine
    return pattern


def seam(R, distance, along=None, width=0.003, depth=0.0008, dark=0.4, stitch=0.006):
    """A sewn seam along the line where `distance` (metres) is zero, with a row of stitches beside it."""
    d = np.abs(distance)
    groove = smoothstep(width, 0.0, d)
    R.relief -= depth * groove
    R.color *= (1.0 - dark * groove)[..., None]
    if along is not None and stitch:
        row = smoothstep(0.0028, 0.0008, np.abs(d - stitch))
        dash = (np.sin(along * TWO_PI / 0.009) > -0.3).astype(np.float32)
        thread = row * dash
        R.relief += 0.00035 * thread
        R.color[...] = R.color * (1.0 + 0.28 * thread[..., None]) + 0.008 * thread[..., None]
    return groove


def wrap_distance(R, s):
    """Metres across the grid from the column at s, the short way round a closed tube."""
    d = np.abs(R.S - s)
    return np.minimum(d, 1.0 - d) * R.width


def box(R, s, t, half_width, half_height):
    """Signed distance in metres to a rectangle centred on (s, t): negative inside. Also local x, y."""
    x = (R.S - s) * R.width
    y = (R.T - t) * R.height
    return np.maximum(np.abs(x) - half_width, np.abs(y) - half_height), x, y


def darken(R, mask, amount=0.4):
    R.color *= (1.0 - amount * np.clip(mask, 0.0, 1.0))[..., None]


def dirt(R, mask, color, rough=0.95):
    mask = np.clip(mask, 0.0, 1.0)
    R.tint(color, mask)
    R.rough[...] = R.rough * (1.0 - mask) + rough * mask


def grime(R, seed, amount=0.5):
    """Blotchy dirt 0..1."""
    patches = R.noise(8, seed) * 0.5 + 0.5
    breakup = R.noise(90, seed + 5) * 0.5 + 0.5
    return np.clip(smoothstep(0.45, 0.8, patches) * (0.5 + 0.5 * breakup) * amount, 0.0, 1.0)


def stain(R, s, t, radius, seed, color, strength=1.0, rough=0.5, spatter=True):
    """A soaked-in stain (blood, iodine, damp) round (s, t), ragged at the edge, with droplets beyond."""
    x = (R.S - s) * R.width
    y = (R.T - t) * R.height
    d = np.sqrt(x * x + y * y)
    ragged = d + radius * 0.55 * R.noise(40, seed) + radius * 0.25 * R.noise(160, seed + 1)
    core = smoothstep(radius, radius * 0.35, ragged)
    mask = core
    if spatter:
        drops = smoothstep(0.55, 0.75, R.noise(260, seed + 2, 2) * 0.5 + 0.5) * smoothstep(radius * 3.0, radius, d)
        mask = np.maximum(core, drops)
    mask = mask * strength
    # Darker toward the edge, where a stain dries last.
    rim = smoothstep(radius * 0.2, radius * 0.9, ragged) * core
    R.tint(np.asarray(color) * 0.75, mask)
    R.tint(np.asarray(color) * 0.45, rim * 0.6 * strength)
    R.rough[...] = R.rough * (1.0 - mask) + rough * mask
    return mask


def wrinkles(R, centre_t, spread, depth, seed, frequency=110.0, across=True, mask=1.0):
    """Compression folds round a bend: ridges across the limb, strongest at `centre_t`."""
    wobble = R.noise(30, seed) * 1.8 + R.noise(120, seed + 1) * 0.6
    coordinate = R.Y if across else R.X
    ridges = np.sin(coordinate * frequency + wobble * 2.0)
    falloff = np.exp(-((R.T - centre_t) / spread) ** 2) * mask
    R.relief += depth * ridges * falloff
    darken(R, (0.5 - 0.5 * ridges) * falloff, 0.18)


def leather(R, color, seed, rough=0.5):
    R.fill(color, rough)
    big, fine = R.noise(14, seed), R.noise(300, seed + 1, 3)
    R.color *= (1.0 + 0.16 * big + 0.10 * fine)[..., None]
    creases = np.abs(R.noise(70, seed + 2))
    R.relief += -0.0004 * smoothstep(0.08, 0.0, creases) + 0.00012 * fine
    R.rough += 0.12 * big + 0.06 * fine


def skin(R, color, seed, rough=0.55):
    R.fill(color, rough)
    big, fine = R.noise(12, seed), R.noise(260, seed + 1, 3)
    red = np.asarray(color) * np.array([1.12, 0.86, 0.82])
    R.tint(red, np.clip(big * 0.5 + 0.2, 0.0, 1.0) * 0.5)
    R.color *= (1.0 + 0.05 * fine)[..., None]
    R.relief += 0.00010 * fine
    R.rough += 0.08 * fine


# --------------------------------------------------------------------------------------
# Protagonist
# --------------------------------------------------------------------------------------
PROTAGONIST = {
    "jacket": rgb("#62502f"), "jacket_dark": rgb("#3f3020"), "rib": rgb("#33302c"), "zip": rgb("#8d8a80"),
    "denim": rgb("#2d3a4c"), "denim_worn": rgb("#5d6e82"), "boot": rgb("#3a2618"), "sole": rgb("#1b1a19"),
    "skin": rgb("#a98267"), "hair": rgb("#2a1e17"), "beanie": rgb("#3b4046"), "scarf": rgb("#662824"),
    "pack": rgb("#4c5345"), "strap": rgb("#2c2d2a"), "mud": rgb("#4b4034"), "dust": rgb("#7d7466"),
    "lining": rgb("#1e1b18"),
}


def pr_jacket(R, P):
    fabric(R, P["jacket"], 0.86, "twill", 0.007, 0.0003, seed=11, mottle=0.12)
    armpit = R.marks["armpit"]
    front = wrap_distance(R, 0.0)
    # Zip down the front: a placket, then the teeth.
    placket = smoothstep(0.022, 0.018, front)
    darken(R, placket, 0.12)
    seam(R, front - 0.020, R.Y)
    teeth = smoothstep(0.0055, 0.004, front)
    tooth = 0.5 + 0.5 * np.sin(R.Y * TWO_PI / 0.006)
    R.tint(P["zip"] * (0.55 + 0.45 * tooth)[..., None], teeth)
    R.metal[...] = np.maximum(R.metal, teeth * 0.9)
    R.rough[...] = R.rough * (1.0 - teeth) + 0.38 * teeth
    R.relief += teeth * (0.0005 + 0.0005 * tooth)
    # Centre back, the sides, and a yoke across the shoulders.
    seam(R, wrap_distance(R, 0.5), R.Y)
    seam(R, wrap_distance(R, 0.25), R.Y, stitch=0)
    seam(R, wrap_distance(R, 0.75), R.Y, stitch=0)
    yoke_t = armpit + (1.0 - armpit) * 0.42
    back = np.abs(R.S - 0.5) < 0.2
    seam(R, np.where(back, (R.T - yoke_t) * R.height, 1.0), R.X)
    # Chest pockets with flaps, and slanted hand pockets lower down.
    for s in (0.105, 1.0 - 0.105):
        d, x, y = box(R, s, armpit * 0.86, 0.052, 0.058)
        seam(R, d, x + y, width=0.0025)
        flap = (d < 0.0) & (y > 0.022)
        R.relief += 0.0012 * smoothstep(0.0, -0.004, d) * flap
        darken(R, smoothstep(0.004, 0.0, np.abs(y - 0.022)) * (np.abs(x) < 0.052), 0.45)
        button = np.sqrt(x * x + (y - 0.034) ** 2)
        dot = smoothstep(0.007, 0.0055, button)
        R.tint(P["rib"], dot)
        R.relief += 0.001 * dot
        R.rough[...] = R.rough * (1.0 - dot) + 0.4 * dot
        side = 1.0 if s < 0.5 else -1.0
        xs = (R.S - (0.14 if s < 0.5 else 0.86)) * R.width * side
        ys = (R.T - armpit * 0.33) * R.height
        welt = np.abs(xs - ys * 0.45)
        on = np.abs(ys) < 0.075
        seam(R, np.where(on, welt, 1.0), ys, width=0.004, depth=0.0012, dark=0.55)
    # Folds where the body bends, wear along the hem and at the shoulders, dust and grime.
    wrinkles(R, armpit * 0.36, 0.2, 0.0006, 21, frequency=80.0, mask=np.abs(np.sin(R.S * TWO_PI)) ** 0.6)
    worn = smoothstep(0.55, 0.9, R.noise(45, 23) * 0.5 + 0.5) * 0.3
    R.tint(P["jacket"] * 1.35, worn * smoothstep(0.75, 1.0, R.T))
    dirt(R, grime(R, 25, 0.55) * (0.35 + 0.65 * smoothstep(0.5, 0.0, R.T)), P["mud"])
    darken(R, smoothstep(0.05, 0.0, R.T), 0.3)


def pr_sleeve(R, P):
    fabric(R, P["jacket"], 0.86, "twill", 0.007, 0.0003, seed=12, mottle=0.12)
    elbow = R.marks["elbow"]
    seam(R, wrap_distance(R, 0.0), R.Y)
    seam(R, (R.T - 0.035) * R.height, R.X)          # where the sleeve is set into the body
    wrinkles(R, elbow, 0.13, 0.0009, 31, frequency=120.0, mask=0.4 + 0.6 * smoothstep(0.5, 0.2, np.abs(R.S - 0.25)))
    wrinkles(R, 0.93, 0.07, 0.0007, 33, frequency=150.0)
    # A patch over the elbow, on the back of the arm.
    x = (R.S - 0.75) * R.width
    y = (R.T - elbow - 0.02) * R.height
    d = np.sqrt((x / 0.050) ** 2 + (y / 0.072) ** 2) - 1.0
    inside = smoothstep(0.02, -0.02, d)
    R.tint(P["jacket_dark"] * (1.0 + 0.12 * R.noise(200, 35, 2))[..., None], inside)
    R.rough[...] = R.rough * (1.0 - inside) + 0.95 * inside
    R.relief += 0.0008 * inside
    seam(R, np.where(d < 0.0, -d, 1.0) * 0.05 - 0.004, np.arctan2(y, x) * 0.05, width=0.002, depth=0.0003, dark=0.3, stitch=0.003)
    dirt(R, grime(R, 37, 0.5) * (0.3 + 0.7 * smoothstep(0.6, 1.0, R.T)), P["mud"])


def pr_rib(R, P):
    fabric(R, P["rib"], 0.9, "rib", 0.0065, 0.0009, seed=41, mottle=0.08)
    R.color *= (0.82 + 0.3 * (0.5 + 0.5 * np.cos(R.X * TWO_PI / 0.0065)))[..., None]


def pr_trousers(R, P):
    fabric(R, P["denim"], 0.88, "twill", 0.0055, 0.00028, seed=51, mottle=0.10)
    crotch, knee = R.marks["crotch"], R.marks["knee"]
    front = np.clip(np.sin(R.S * TWO_PI), 0.0, 1.0)      # S 0.25 faces forward
    back = np.clip(-np.sin(R.S * TWO_PI), 0.0, 1.0)
    # Denim fades where it rubs: the front of the thigh, the knee, the seat.
    thigh = np.exp(-((R.T - (crotch + 0.12)) / 0.14) ** 2) * front ** 1.5
    knee_fade = np.exp(-((R.T - knee) / 0.07) ** 2) * front ** 1.2
    seat = np.exp(-((R.T - crotch * 0.75) / 0.08) ** 2) * back ** 2
    fade = np.clip(thigh * 0.55 + knee_fade * 0.7 + seat * 0.45, 0.0, 1.0) * (0.75 + 0.25 * R.noise(120, 53))
    R.tint(P["denim_worn"], fade * 0.6)
    whisk = np.sin((R.T - crotch) * R.height * 160.0 + R.S * 30.0 + 2.0 * R.noise(20, 54))
    R.tint(P["denim_worn"], np.clip(whisk, 0.0, 1.0) * np.exp(-((R.T - crotch - 0.04) / 0.05) ** 2) * front * 0.25)
    # Out seam, in seam, hem.
    seam(R, wrap_distance(R, 0.5), R.Y, depth=0.001)
    seam(R, wrap_distance(R, 0.0), R.Y, depth=0.001)
    seam(R, (R.T - 0.975) * R.height, R.X, depth=0.0006)
    # Back pocket on the seat.
    d, x, y = box(R, 0.70, crotch * 0.62, 0.062, 0.070)
    point = np.maximum(d, (np.abs(x) * 0.5 + y) - 0.085)
    seam(R, np.where(R.T < crotch, point, 1.0), x - y, width=0.0025)
    wrinkles(R, knee, 0.07, 0.0008, 55, frequency=140.0, mask=0.35 + 0.65 * back)
    wrinkles(R, 0.92, 0.06, 0.0009, 57, frequency=170.0)
    mud = smoothstep(0.62, 1.0, R.T) * (0.45 + 0.55 * (R.noise(30, 58) * 0.5 + 0.5))
    dirt(R, mud * mud * 0.55 + grime(R, 59, 0.35), P["mud"])


def pr_boot(R, P):
    leather(R, P["boot"], 61, 0.5)
    ankle = R.marks["ankle"]
    front = wrap_distance(R, 0.5)
    laced = (R.T > 0.02) & (R.T < ankle + 0.26)
    # Tongue, eyelets and laces up the front.
    tongue = smoothstep(0.022, 0.016, front) * laced
    darken(R, tongue, 0.35)
    R.relief -= 0.001 * tongue
    y = R.Y / 0.021
    row = np.abs(y - np.round(y))
    lace = smoothstep(0.16, 0.08, np.abs(row - np.abs(R.S - 0.5) * R.width / 0.021 * 0.5)) * (front < 0.024) * laced
    R.tint(P["dust"] * 0.7, lace)
    R.relief += 0.0014 * lace
    R.rough[...] = R.rough * (1.0 - lace) + 0.9 * lace
    eyelet = smoothstep(0.005, 0.0035, np.sqrt((front - 0.026) ** 2 + (row * 0.021) ** 2)) * laced
    R.tint(P["zip"], eyelet)
    R.metal[...] = np.maximum(R.metal, eyelet)
    R.rough[...] = R.rough * (1.0 - eyelet) + 0.35 * eyelet
    # Stitched toe cap and heel counter.
    seam(R, (R.T - 0.80) * R.height, R.X, depth=0.0009)
    heel = np.where((R.T > ankle) & (R.T < ankle + 0.3), wrap_distance(R, 0.0) - 0.035, 1.0)
    seam(R, heel, R.Y, depth=0.0007)
    scuff = smoothstep(0.5, 0.85, R.noise(80, 63) * 0.5 + 0.5) * smoothstep(0.55, 0.95, R.T)
    R.tint(P["dust"], scuff * 0.5)
    R.rough += 0.3 * scuff
    dirt(R, grime(R, 65, 0.5) * smoothstep(0.3, 0.8, R.T), P["mud"])


def pr_sole(R, P):
    R.fill(P["sole"], 0.92)
    R.color *= (1.0 + 0.2 * R.noise(200, 67, 2))[..., None]
    side = (R.T > 0.25) & (R.T < 0.8)
    grooves = smoothstep(0.3, 0.0, np.abs(np.sin(R.X * TWO_PI / 0.016))) * side
    R.relief -= 0.0012 * grooves
    darken(R, grooves, 0.4)
    welt = smoothstep(0.03, 0.0, np.abs(R.T - 0.82))
    R.tint(P["mud"], welt * 0.5 + grime(R, 69, 0.6))


def pr_skin(R, P):
    skin(R, P["skin"], 71)


def pr_face(R, P):
    skin(R, P["skin"], 73)
    front = (R.S - 0.5) * TWO_PI                      # angle from the front of the face
    eyes, brow = R.marks["eyes"], R.marks["brow"]
    for side in (-1.0, 1.0):
        x = (front - side * 0.36) * 0.078             # metres across the face
        y = (R.T - eyes) * R.height
        socket = np.exp(-(x / 0.022) ** 2 - (y / 0.014) ** 2)
        darken(R, socket, 0.45)
        almond = (x / 0.0125) ** 2 + (y / 0.0048) ** 2
        white = smoothstep(1.0, 0.7, almond)
        R.tint(rgb("#b9b2a8"), white)
        iris = smoothstep(0.0048, 0.0036, np.sqrt((x + side * 0.001) ** 2 + y ** 2))
        R.tint(rgb("#1c1410"), iris * white)
        lid = smoothstep(0.0022, 0.0006, np.abs(y - 0.0042 * (1.0 - (x / 0.0125) ** 2))) * (np.abs(x) < 0.014)
        R.tint(P["hair"], lid * 0.8)
        R.rough[...] = R.rough * (1.0 - white) + 0.25 * white
        yb = (R.T - brow) * R.height + 0.004 * (x / 0.02) ** 2
        eyebrow = smoothstep(0.004, 0.0015, np.abs(yb)) * smoothstep(0.024, 0.016, np.abs(x + side * 0.002))
        R.tint(P["hair"], eyebrow * (0.6 + 0.4 * (R.noise(400, 75, 2) * 0.5 + 0.5)))
    # Short hair below the beanie at the sides and the back, stubble on the jaw.
    back = smoothstep(0.75, 1.3, np.abs(front))
    hairline = smoothstep(R.marks["nape"] - 0.02, R.marks["nape"] + 0.03, R.T)
    strands = 0.75 + 0.25 * np.sin(R.X * TWO_PI / 0.0035 + 3.0 * R.noise(40, 77))
    R.tint(P["hair"] * strands[..., None], back * hairline)
    R.rough[...] = R.rough * (1.0 - back * hairline) + 0.7 * back * hairline
    R.relief += 0.0005 * strands * back * hairline
    jaw = np.exp(-((R.T - R.marks["jaw"]) / 0.06) ** 2) * (1.0 - back)
    R.tint(P["hair"], jaw * 0.22 * (0.6 + 0.4 * R.noise(500, 79, 2)))


def pr_beanie(R, P):
    cuff = R.T < 0.36
    fabric(R, P["beanie"], 0.93, "rib", 0.0075, 0.0010, seed=81, mottle=0.08)
    R.color *= (0.85 + 0.25 * (0.5 + 0.5 * np.cos(R.X * TWO_PI / 0.0075)))[..., None]
    purl = 0.5 + 0.5 * np.cos(R.Y * TWO_PI / 0.005)
    R.relief += 0.0003 * purl
    darken(R, smoothstep(0.02, 0.0, np.abs(R.T - 0.36)), 0.45)
    R.relief += 0.0012 * cuff
    stripe = smoothstep(0.012, 0.008, np.abs((R.T - 0.52) * R.height)) + smoothstep(0.005, 0.003, np.abs((R.T - 0.62) * R.height))
    R.tint(P["dust"] * 0.9, np.clip(stripe, 0, 1) * 0.55)


def pr_scarf(R, P):
    fabric(R, P["scarf"], 0.95, "knit", 0.008, 0.0011, seed=83, mottle=0.14)
    # The wrap is one long strip: lines along it read as its folds.
    fold = np.sin(R.T * TWO_PI * 3.0 + 1.5 * R.noise(12, 85))
    R.relief += 0.0012 * fold
    darken(R, 0.5 - 0.5 * fold, 0.28)
    dirt(R, grime(R, 87, 0.35), P["mud"])


def pr_pack(R, P):
    fabric(R, P["pack"], 0.82, "plain", 0.006, 0.0003, seed=91, mottle=0.12)
    outer = np.clip(-np.cos(R.S * TWO_PI), 0.0, 1.0)     # S 0.5 faces away from the body
    x = (R.S - 0.5) * R.width
    # Main zip arching over the top, a front pocket with its own zip, a loop on top.
    arch = (R.T - 0.80 + (x / 0.25) ** 2) * R.height
    for line, half in ((arch, 0.16), ((R.T - 0.50) * R.height, 0.085)):
        on = np.abs(x) < half
        zipline = smoothstep(0.004, 0.0025, np.abs(line)) * on
        tooth = 0.5 + 0.5 * np.sin(R.X * TWO_PI / 0.006)
        R.tint(P["zip"] * (0.5 + 0.5 * tooth)[..., None], zipline)
        R.metal[...] = np.maximum(R.metal, zipline * 0.9)
        R.rough[...] = R.rough * (1.0 - zipline) + 0.4 * zipline
        seam(R, np.where(on, np.abs(line) - 0.009, 1.0), R.X, stitch=0)
    d, px, py = box(R, 0.5, 0.33, 0.085, 0.07)
    seam(R, d, px + py)
    R.relief += 0.0015 * smoothstep(0.006, -0.006, d)
    label = box(R, 0.5, 0.64, 0.022, 0.010)[0]
    R.tint(P["rib"], smoothstep(0.001, -0.001, label))
    seam(R, wrap_distance(R, 0.25), R.Y)
    seam(R, wrap_distance(R, 0.75), R.Y)
    dirt(R, grime(R, 93, 0.6) * (0.4 + 0.6 * smoothstep(0.4, 0.0, R.T)), P["mud"])
    darken(R, 1.0 - outer, 0.25)


def pr_strap(R, P):
    fabric(R, P["strap"], 0.8, "rib_across", 0.004, 0.0004, seed=95, mottle=0.06)
    edge = np.minimum(R.S, 1.0 - R.S) * R.width
    seam(R, edge - 0.012, R.Y, width=0.002, depth=0.0003, dark=0.25, stitch=0.0)
    darken(R, smoothstep(0.012, 0.0, edge), 0.3)


def pr_inside(R, P):
    R.fill(P["lining"], 0.95)
    R.shade[...] = 0.45


PROTAGONIST["paint"] = {
    "jacket": pr_jacket, "sleeve": pr_sleeve, "rib": pr_rib, "trousers": pr_trousers, "boot": pr_boot, "sole": pr_sole,
    "skin": pr_skin, "face": pr_face, "beanie": pr_beanie, "scarf": pr_scarf, "pack": pr_pack, "strap": pr_strap,
    "inside": pr_inside,
}


# --------------------------------------------------------------------------------------
# Patient
# --------------------------------------------------------------------------------------
PATIENT = {
    "gown": rgb("#84958a"), "print": rgb("#5a7680"), "skin": rgb("#8f8576"), "bruise": rgb("#4d3f52"),
    "vein": rgb("#4a5a58"), "bandage": rgb("#aaa089"), "blood": rgb("#3d0d0a"), "old_blood": rgb("#33180f"),
    "stain": rgb("#6e5a2c"), "filth": rgb("#2d2820"), "nail": rgb("#5d5546"),
}


def bandage(R, pitch=0.042, slope=0.42, seed=0):
    """Wrapped gauze: turn after turn, each lying over the last, frayed and stained."""
    P = PATIENT
    fabric(R, P["bandage"], 0.93, "plain", 0.005, 0.00025, seed=seed, mottle=0.10)
    turn = (R.Y + (R.S * R.width) * slope) / pitch + 0.25 * R.noise(18, seed + 1)
    f = turn - np.floor(turn)
    R.relief += 0.0011 * f                               # each turn rises, then drops onto the next
    edge = smoothstep(0.10, 0.0, np.minimum(f, 1.0 - f))
    darken(R, edge, 0.45)
    fray = smoothstep(0.86, 1.0, f) * (R.noise(300, seed + 2, 2) * 0.5 + 0.5)
    R.tint(P["bandage"] * 1.2, fray * 0.5)
    R.tint(P["stain"], grime(R, seed + 3, 0.75))
    R.tint(P["filth"], smoothstep(0.5, 0.9, R.noise(25, seed + 4) * 0.5 + 0.5) * 0.35)


def patient_skin(R, seed, dirt_mask=0.0):
    P = PATIENT
    R.fill(P["skin"], 0.6)
    big, mid, fine = R.noise(9, seed), R.noise(45, seed + 1), R.noise(280, seed + 2, 3)
    R.color *= (1.0 + 0.08 * big + 0.05 * fine)[..., None]
    R.tint(P["bruise"], smoothstep(0.35, 0.8, mid * 0.5 + 0.5 + 0.2 * big) * 0.42)
    veins = smoothstep(0.035, 0.0, np.abs(R.noise(22, seed + 3, 3))) * (0.5 + 0.5 * big)
    R.tint(P["vein"], veins * 0.45)
    R.relief += 0.0003 * veins + 0.00012 * fine
    R.rough += 0.1 * fine
    R.tint(P["filth"], np.clip(dirt_mask, 0.0, 1.0) * (0.55 + 0.45 * (mid * 0.5 + 0.5)))


def pt_gown(R, P):
    fabric(R, P["gown"], 0.9, "plain", 0.006, 0.00022, seed=101, mottle=0.10)
    armpit = R.marks["armpit"]
    # A small print, washed nearly out.
    gx, gy = R.X / 0.030, R.Y / 0.030
    cell = np.abs(gx - np.round(gx)) + np.abs(gy - np.round(gy) + 0.5 * (np.round(gx) % 2) - 0.25)
    R.tint(P["print"], smoothstep(0.16, 0.09, cell) * 0.5)
    # Bound edges down the back and round the hem; a seam under each arm.
    edge = np.minimum(R.S, 1.0 - R.S) * R.width
    seam(R, edge - 0.012, R.Y, depth=0.0005)
    seam(R, (R.T - 0.012) * R.height, R.X, depth=0.0005)
    seam(R, wrap_distance(R, 0.25), R.Y, stitch=0)
    seam(R, wrap_distance(R, 0.75), R.Y, stitch=0)
    # What has soaked into it.
    rising = smoothstep(0.42, 0.0, R.T) * (0.4 + 0.6 * (R.noise(14, 103) * 0.5 + 0.5))
    R.tint(P["stain"], rising * 0.75)
    R.tint(P["filth"], smoothstep(0.12, 0.0, R.T) * 0.6 + grime(R, 105, 0.4))
    stain(R, 0.455, armpit * 0.80, 0.075, 107, P["blood"], 0.95, 0.5)
    stain(R, 0.60, armpit * 0.47, 0.040, 109, P["old_blood"], 0.85, 0.7)
    stain(R, 0.52, armpit * 0.20, 0.10, 111, P["stain"], 0.7, 0.8, spatter=False)
    stain(R, 0.83, armpit * 0.62, 0.05, 113, P["old_blood"], 0.7, 0.75)
    drip = smoothstep(0.012, 0.004, np.abs((R.S - 0.455) * R.width + 0.01 * R.noise(30, 115))) * (R.T < armpit * 0.8) * (R.T > armpit * 0.52)
    R.tint(P["blood"] * 0.8, drip * 0.8)
    wrinkles(R, armpit * 0.9, 0.2, 0.0004, 117, frequency=90.0, across=False)


def pt_gown_sleeve(R, P):
    fabric(R, P["gown"], 0.9, "plain", 0.006, 0.00022, seed=121, mottle=0.10)
    gx, gy = R.X / 0.030, R.Y / 0.030
    cell = np.abs(gx - np.round(gx)) + np.abs(gy - np.round(gy) + 0.5 * (np.round(gx) % 2) - 0.25)
    R.tint(P["print"], smoothstep(0.16, 0.09, cell) * 0.5)
    seam(R, (R.T - 0.95) * R.height, R.X, depth=0.0005)
    seam(R, (R.T - 0.06) * R.height, R.X, depth=0.0005)
    R.tint(P["stain"], grime(R, 123, 0.8) + smoothstep(0.7, 1.0, R.T) * 0.3)


def pt_gown_inside(R, P):
    R.fill(P["gown"] * 0.5, 0.92)
    R.color *= (1.0 + 0.15 * R.noise(20, 125))[..., None]
    R.shade[...] = 0.6


def pt_tie(R, P):
    fabric(R, P["gown"] * 0.85, 0.9, "plain", 0.005, 0.0002, seed=127, mottle=0.12)
    R.tint(P["stain"], 0.3)


def pt_back(R, P):
    patient_skin(R, 131)
    # The spine and the ribs under thin skin.
    centre = (R.S - 0.5) * R.width
    knobs = np.exp(-(centre / 0.014) ** 2) * (0.5 + 0.5 * np.cos(R.Y * TWO_PI / 0.034))
    R.relief += 0.0025 * knobs
    darken(R, np.exp(-((np.abs(centre) - 0.022) / 0.01) ** 2) * 0.6, 0.3)
    ribs = np.cos((R.Y + np.abs(centre) * 0.35) * TWO_PI / 0.04) * smoothstep(0.02, 0.07, np.abs(centre)) * (R.T > 0.55)
    R.relief += 0.0012 * ribs
    darken(R, 0.5 - 0.5 * ribs, 0.14)
    stain(R, 0.62, 0.7, 0.03, 133, P["bruise"], 0.6, 0.6, spatter=False)


def pt_arm(R, P):
    patient_skin(R, 141, smoothstep(0.6, 1.0, R.T) * 0.5)
    for offset in (0.0, 0.012, 0.026):
        line = smoothstep(0.0022, 0.0006, np.abs((R.S - 0.3) * R.width - offset - (R.T - 0.6) * 0.05 + 0.004 * R.noise(60, 143)))
        R.tint(P["old_blood"], line * (R.T > 0.45 + offset) * (R.T < 0.85) * 0.8)
    darken(R, np.exp(-((R.T - R.marks["elbow"]) / 0.03) ** 2) * 0.5, 0.25)


def pt_arm_bandaged(R, P):
    patient_skin(R, 151, 0.0)
    wrapped = smoothstep(R.marks["elbow"] - 0.10, R.marks["elbow"] - 0.07, R.T + 0.02 * R.noise(40, 153))
    keep = R.color.copy(), R.relief.copy(), R.rough.copy()
    bandage(R, 0.040, 0.45, seed=155)
    stain(R, 0.55, 0.72, 0.035, 157, P["blood"], 0.9, 0.55)
    stain(R, 0.2, 0.9, 0.03, 159, P["old_blood"], 0.8, 0.7)
    m = wrapped[..., None]
    R.color[...] = keep[0] * (1.0 - m) + R.color * m
    R.relief[...] = keep[1] * (1.0 - wrapped) + (R.relief + 0.0012) * wrapped
    R.rough[...] = keep[2] * (1.0 - wrapped) + R.rough * wrapped


def pt_hand(R, P):
    patient_skin(R, 161, 0.25 + 0.5 * smoothstep(0.5, 1.0, R.T))
    R.tint(P["nail"], smoothstep(0.8, 0.95, R.T) * 0.3)


def pt_hand_bandaged(R, P):
    bandage(R, 0.022, 0.5, seed=165)
    R.tint(P["filth"], smoothstep(0.55, 1.0, R.T) * 0.45)
    stain(R, 0.5, 0.5, 0.012, 167, P["old_blood"], 0.7, 0.7, spatter=False)


def pt_leg(R, P):
    ankle = R.marks["ankle"]
    grimy = smoothstep(ankle - 0.25, ankle + 0.05, R.T) * 0.55 + smoothstep(ankle + 0.1, 1.0, R.T) * 0.4
    patient_skin(R, 171, grimy)
    # The sole is black with dirt (S 0 is the back of the leg and the underside of the foot).
    under = smoothstep(0.16, 0.08, np.minimum(R.S, 1.0 - R.S)) * (R.T > ankle + 0.06)
    R.tint(P["filth"] * 0.7, under * 0.9)
    knee = np.exp(-((R.T - R.marks["knee"]) / 0.03) ** 2) * np.exp(-((R.S - 0.5) / 0.12) ** 2)
    R.tint(P["bruise"], knee * 0.5)
    stain(R, 0.48, R.marks["knee"] + 0.01, 0.018, 173, P["old_blood"], 0.8, 0.7)
    stain(R, 0.56, R.marks["knee"] + 0.16, 0.014, 175, P["old_blood"], 0.7, 0.7)
    R.tint(P["old_blood"], smoothstep(0.006, 0.002, np.abs((R.S - 0.56) * R.width)) * (R.T > R.marks["knee"] + 0.16) * (R.T < ankle) * 0.4)


def pt_toe(R, P):
    patient_skin(R, 177, 0.5)
    R.tint(P["nail"], smoothstep(0.55, 0.75, R.T) * (np.abs(R.S - 0.25) < 0.16) * 0.7)


def pt_head(R, P):
    jaw = R.marks["jaw"]
    patient_skin(R, 181, 0.15)
    skin_color, skin_relief, skin_rough = R.color.copy(), R.relief.copy(), R.rough.copy()
    # Tendons down the neck.
    front = (R.S - 0.5) * TWO_PI
    cords = np.exp(-((np.abs(front) - 0.55) / 0.16) ** 2)
    skin_relief = skin_relief + 0.002 * cords
    bandage(R, 0.034, 0.30, seed=183)
    cross = (R.Y - (R.S * R.width) * 0.55) / 0.05
    g = cross - np.floor(cross)
    second = (R.noise(7, 185) > 0.0)
    R.relief += 0.0009 * g * second
    darken(R, smoothstep(0.10, 0.0, np.minimum(g, 1.0 - g)) * second, 0.4)
    # Hollows where the eyes are, one of them bled through; a dark patch at the mouth.
    for side, color, strength in ((-1.0, P["filth"], 0.55), (1.0, P["blood"], 0.95)):
        x = (front - side * 0.36) * 0.075
        y = (R.T - R.marks["eyes"]) * R.height
        hollow = np.exp(-(x / 0.020) ** 2 - (y / 0.014) ** 2)
        darken(R, hollow, 0.5)
        R.relief -= 0.0012 * hollow
        if side > 0:
            stain(R, 0.5 + side * 0.36 / TWO_PI, R.marks["eyes"] - 0.01, 0.024, 187, color, strength, 0.45)
            run = smoothstep(0.006, 0.002, np.abs(x + 0.004 * R.noise(50, 189))) * (y < 0.0) * (y > -0.075)
            R.tint(P["blood"] * 0.8, run * 0.85)
    xm = front * 0.075
    ym = (R.T - R.marks["mouth"]) * R.height
    mouth = np.exp(-(xm / 0.022) ** 2 - (ym / 0.008) ** 2)
    R.tint(P["old_blood"], mouth * 0.8)
    R.relief -= 0.0015 * mouth
    stain(R, 0.5, R.marks["mouth"] - 0.03, 0.02, 191, P["stain"], 0.7, 0.8, spatter=False)
    # Below the jaw the neck is bare.
    bare = smoothstep(jaw - 0.035, jaw - 0.075, R.T + 0.015 * R.noise(30, 193))
    m = bare[..., None]
    R.color[...] = R.color * (1.0 - m) + skin_color * m
    R.relief[...] = (R.relief + 0.0015) * (1.0 - bare) + skin_relief * bare
    R.rough[...] = R.rough * (1.0 - bare) + skin_rough * bare
    darken(R, smoothstep(0.03, 0.0, np.abs(R.T - (jaw - 0.055))) * 0.6, 0.4)


def pt_wrap(R, P):
    bandage(R, 0.06, 0.1, seed=195)
    R.tint(P["filth"], 0.12)


PATIENT["paint"] = {
    "gown": pt_gown, "gown_sleeve": pt_gown_sleeve, "gown_inside": pt_gown_inside, "tie": pt_tie, "back": pt_back,
    "arm": pt_arm, "arm_bandaged": pt_arm_bandaged, "hand": pt_hand, "hand_bandaged": pt_hand_bandaged, "leg": pt_leg,
    "toe": pt_toe, "head": pt_head, "wrap": pt_wrap,
}


# --------------------------------------------------------------------------------------
# Orderly
# --------------------------------------------------------------------------------------
ORDERLY = {
    "scrub": rgb("#46535d"), "scrub_dark": rgb("#303a42"), "sweat": rgb("#262e35"), "skin": rgb("#96705c"),
    "hair": rgb("#231a15"), "cap": rgb("#55626b"), "mask": rgb("#7d8a8c"), "rubber": rgb("#2a2822"),
    "rubber_worn": rgb("#4b483c"), "glove": rgb("#5c4d27"), "clog": rgb("#8f8a7b"), "blood": rgb("#2e0806"),
    "old_blood": rgb("#2c130c"), "filth": rgb("#2a251d"), "stain": rgb("#5c4a28"), "strap": rgb("#161616"),
    "lining": rgb("#1b2026"),
}


def scrubs(R, P, seed):
    fabric(R, P["scrub"], 0.88, "plain", 0.006, 0.00022, seed=seed, mottle=0.10)
    R.tint(P["filth"], grime(R, seed + 7, 0.5))


def od_tunic(R, P):
    scrubs(R, P, 201)
    armpit = R.marks["armpit"]
    front = wrap_distance(R, 0.5)                       # the seam is down the back: S 0.5 is the front
    seam(R, wrap_distance(R, 0.25), R.Y, stitch=0)
    seam(R, wrap_distance(R, 0.75), R.Y, stitch=0)
    seam(R, (R.T - 0.03) * R.height, R.X, depth=0.0005)
    # V neck binding, as far as the panel shows it.
    top = (1.0 - R.T) * R.height
    seam(R, np.where(front < 0.14, top - 0.018, 1.0), R.X, depth=0.0006)
    # Sweat under the arms, down the chest and the back.
    sweat = np.exp(-((R.T - armpit) / 0.14) ** 2) * (np.exp(-(wrap_distance(R, 0.25) / 0.09) ** 2) + np.exp(-(wrap_distance(R, 0.75) / 0.09) ** 2))
    sweat = sweat + np.exp(-(front / 0.07) ** 2) * smoothstep(armpit * 0.75, 1.0, R.T) * 0.7
    sweat = sweat + np.exp(-(wrap_distance(R, 0.0) / 0.06) ** 2) * smoothstep(armpit * 0.5, 0.95, R.T) * 0.7
    sweat = np.clip(sweat * (0.7 + 0.3 * R.noise(30, 203)), 0.0, 1.0)
    R.tint(P["sweat"], sweat * 0.7)
    R.rough -= 0.12 * sweat
    stain(R, 0.34, armpit * 0.9, 0.05, 205, P["blood"], 0.9, 0.5)
    stain(R, 0.78, armpit * 0.3, 0.06, 207, P["old_blood"], 0.8, 0.7)
    wrinkles(R, armpit * 0.75, 0.2, 0.0006, 209, frequency=70.0, mask=np.abs(np.sin(R.S * TWO_PI)))


def od_tunic_sleeve(R, P):
    scrubs(R, P, 211)
    seam(R, (R.T - 0.93) * R.height, R.X, depth=0.0005)
    seam(R, (R.T - 0.08) * R.height, R.X, depth=0.0005)
    seam(R, wrap_distance(R, 0.0), R.Y, stitch=0)
    stain(R, 0.3, 0.6, 0.04, 213, P["blood"], 0.9, 0.5)


def od_trousers(R, P):
    scrubs(R, P, 221)
    seam(R, wrap_distance(R, 0.5), R.Y, depth=0.0007)
    seam(R, wrap_distance(R, 0.0), R.Y, depth=0.0007)
    seam(R, (R.T - 0.975) * R.height, R.X, depth=0.0005)
    wrinkles(R, R.marks["knee"], 0.08, 0.0007, 223, frequency=120.0)
    low = smoothstep(0.6, 1.0, R.T)
    R.tint(P["old_blood"], low * low * (0.35 + 0.65 * (R.noise(26, 225) * 0.5 + 0.5)) * 0.75)
    stain(R, 0.3, 0.72, 0.05, 227, P["blood"], 0.9, 0.5)
    R.tint(P["filth"], smoothstep(0.9, 1.0, R.T) * 0.6)


def od_inside(R, P):
    R.fill(P["lining"], 0.95)
    R.shade[...] = 0.45


def orderly_skin(R, P, seed):
    skin(R, P["skin"], seed, 0.45)
    R.tint(P["skin"] * np.array([1.15, 0.8, 0.75]), smoothstep(0.3, 0.8, R.noise(20, seed + 9) * 0.5 + 0.5) * 0.35)


def od_arm(R, P):
    orderly_skin(R, P, 231)
    hair = smoothstep(0.55, 0.9, np.sin(R.X * TWO_PI / 0.004 + 5.0 * R.noise(70, 233)) * 0.5 + 0.5 + 0.3 * R.noise(300, 235, 2))
    R.tint(P["hair"], hair * 0.4 * np.clip(-np.cos(R.S * TWO_PI) * 0.5 + 0.7, 0.0, 1.0))
    stain(R, 0.6, 0.75, 0.03, 237, P["old_blood"], 0.7, 0.6)


def od_skin(R, P):
    orderly_skin(R, P, 241)


def od_face(R, P):
    orderly_skin(R, P, 243)
    front = (R.S - 0.5) * TWO_PI
    back = smoothstep(1.2, 1.9, np.abs(front))
    # Folds of the neck at the back, a shaved nape.
    rolls = np.cos(R.Y * TWO_PI / 0.03) * back * np.exp(-((R.T - R.marks["nape"] + 0.08) / 0.1) ** 2)
    R.relief += 0.0018 * rolls
    darken(R, 0.5 - 0.5 * rolls, 0.25)
    stubble = back * smoothstep(R.marks["nape"] - 0.03, R.marks["nape"] + 0.04, R.T)
    R.tint(P["hair"], stubble * 0.5 * (0.6 + 0.4 * R.noise(500, 245, 2)))
    R.rough -= 0.1 * smoothstep(0.4, 0.9, R.noise(40, 247) * 0.5 + 0.5)


def od_cap(R, P):
    fabric(R, P["cap"], 0.88, "plain", 0.005, 0.0002, seed=251, mottle=0.10)
    seam(R, (R.T - 0.22) * R.height, R.X, depth=0.0006)
    gather = np.cos(R.X * TWO_PI / 0.014 + 2.0 * R.noise(30, 253)) * smoothstep(0.2, 0.5, R.T)
    R.relief += 0.0006 * gather
    darken(R, 0.5 - 0.5 * gather, 0.12)
    R.tint(P["sweat"], smoothstep(0.3, 0.0, R.T) * 0.55 * (0.6 + 0.4 * R.noise(20, 255)))
    stain(R, 0.62, 0.55, 0.02, 257, P["old_blood"], 0.8, 0.7)


def od_mask(R, P):
    fabric(R, P["mask"], 0.85, "plain", 0.004, 0.00018, seed=261, mottle=0.08)
    for t in (0.3, 0.5, 0.7):
        pleat = (R.T - t) * R.height
        R.relief += 0.0012 * smoothstep(0.006, 0.0, pleat) * (pleat > -0.001)
        darken(R, smoothstep(0.004, 0.0, np.abs(pleat)), 0.35)
    edge = np.minimum(np.minimum(R.S, 1.0 - R.S) * R.width, np.minimum(R.T, 1.0 - R.T) * R.height)
    seam(R, edge - 0.012, R.X + R.Y, depth=0.0005)
    stain(R, 0.5, 0.42, 0.028, 263, P["stain"], 0.8, 0.8, spatter=False)
    stain(R, 0.36, 0.6, 0.012, 265, P["blood"], 0.9, 0.5)


def rubber(R, color, seed, rough=0.34):
    R.fill(color, rough)
    big, streak = R.noise(8, seed), R.noise((60, 6), seed + 1, 3)
    R.color *= (1.0 + 0.2 * big + 0.12 * streak)[..., None]
    R.rough += 0.14 * streak + 0.1 * big
    R.relief += 0.0002 * R.noise(120, seed + 2, 2)


def od_apron(R, P):
    rubber(R, P["rubber"], 271)
    outer = R.S < 0.5
    s = np.where(outer, R.S * 2.0, (1.0 - R.S) * 2.0)      # 0..1 across either side of the sheet
    edge = np.minimum(np.minimum(s, 1.0 - s) * R.width * 0.5, np.minimum(R.T, 1.0 - R.T) * R.height)
    seam(R, edge - 0.014, R.X + R.Y, depth=0.0007, dark=0.3)
    wear = smoothstep(0.5, 0.9, R.noise(35, 273) * 0.5 + 0.5) * 0.5
    R.tint(P["rubber_worn"], wear)
    R.rough += 0.25 * wear
    # Blood: wiped across the belly, run down from it, dried along the hem.
    for index, (cs, ct, radius) in enumerate(((0.52, 0.42, 0.11), (0.3, 0.62, 0.07), (0.68, 0.72, 0.05), (0.45, 0.2, 0.045))):
        x = (s - cs) * R.width * 0.5
        y = (R.T - ct) * R.height
        d = np.sqrt((x * 0.6) ** 2 + y * y)
        ragged = d + radius * 0.5 * R.noise(30, 275 + index) + radius * 0.25 * R.noise(150, 280 + index)
        blot = smoothstep(radius, radius * 0.3, ragged) * outer
        runs = smoothstep(0.55, 0.8, R.noise((90, 3), 285 + index, 3) * 0.5 + 0.5) * smoothstep(radius * 1.2, 0.0, np.abs(x)) * (y > 0.0) * (y < 0.28) * outer
        mask = np.clip(blot + runs * 0.8, 0.0, 1.0)
        R.tint(P["blood"] * (0.75 + 0.25 * R.noise(60, 297 + index))[..., None], mask * 0.92)
        R.rough[...] = R.rough * (1.0 - mask) + 0.22 * mask
    hem = smoothstep(0.82, 1.0, R.T) * (0.5 + 0.5 * R.noise(22, 291))
    R.tint(P["old_blood"], hem * 0.7)
    smear = smoothstep(0.6, 0.85, R.noise((5, 50), 293, 3) * 0.5 + 0.5) * np.exp(-((R.T - 0.45) / 0.2) ** 2) * outer
    R.tint(P["old_blood"], smear * 0.5)


def od_apron_strap(R, P):
    rubber(R, P["rubber"] * 0.8, 295, 0.45)
    edge = np.minimum(R.S, 1.0 - R.S) * R.width
    darken(R, smoothstep(0.006, 0.0, edge), 0.3)


def od_glove(R, P):
    rubber(R, P["glove"], 301, 0.36)
    R.tint(P["old_blood"], smoothstep(0.35, 1.0, R.T) * (0.4 + 0.6 * (R.noise(24, 303) * 0.5 + 0.5)) * 0.8)
    wrinkles(R, 0.85, 0.12, 0.0006, 305, frequency=160.0)


def od_glove_hand(R, P):
    rubber(R, P["glove"], 307, 0.34)
    wet = np.clip(0.55 + 0.6 * R.noise(30, 309), 0.0, 1.0)
    R.tint(P["blood"], wet * 0.85)
    R.rough[...] = R.rough * (1.0 - wet) + 0.2 * wet


def od_clog(R, P):
    rubber(R, P["clog"], 311, 0.5)
    ankle = R.marks["ankle"]
    top = smoothstep(0.2, 0.1, np.abs(R.S - 0.5)) * (R.T > ankle + 0.3) * (R.T < 0.9)
    holes = smoothstep(0.0045, 0.003, np.sqrt((np.mod(R.X, 0.018) - 0.009) ** 2 + (np.mod(R.Y, 0.018) - 0.009) ** 2)) * top
    darken(R, holes, 0.85)
    R.relief -= 0.001 * holes
    R.tint(P["filth"], grime(R, 313, 0.7) + smoothstep(0.75, 1.0, R.T) * 0.25)
    stain(R, 0.42, 0.7, 0.02, 315, P["blood"], 0.9, 0.3)
    stain(R, 0.62, 0.82, 0.014, 317, P["blood"], 0.9, 0.3)
    darken(R, R.T < ankle, 0.75)       # the sock, in the shadow of the trouser leg


def od_clog_sole(R, P):
    rubber(R, P["clog"] * 0.75, 319, 0.6)
    R.tint(P["filth"], 0.35 + grime(R, 321, 0.6))
    R.tint(P["old_blood"], smoothstep(0.5, 0.2, R.T) * 0.5)


def od_goggle(R, P):
    # The barrel of the eyepiece, then the lens across its end.
    R.fill(P["strap"] * 2.2, 0.5)
    lens = R.T > 0.52
    R.color[lens] = np.array([0.012, 0.014, 0.013])
    R.rough[lens] = 0.06
    glint = smoothstep(0.7, 0.9, R.noise(10, 331) * 0.5 + 0.5) * lens
    R.tint(np.array([0.08, 0.09, 0.08]), glint * 0.6)
    R.rough += 0.3 * glint * lens
    darken(R, smoothstep(0.04, 0.0, np.abs(R.T - 0.52)), 0.6)


def od_goggle_strap(R, P):
    fabric(R, P["strap"], 0.8, "rib_across", 0.004, 0.0003, seed=333, mottle=0.05)


ORDERLY["paint"] = {
    "tunic": od_tunic, "tunic_sleeve": od_tunic_sleeve, "trousers": od_trousers, "inside": od_inside, "arm": od_arm,
    "skin": od_skin, "face": od_face, "cap": od_cap, "mask": od_mask, "apron": od_apron, "apron_strap": od_apron_strap,
    "glove": od_glove, "glove_hand": od_glove_hand, "clog": od_clog, "clog_sole": od_clog_sole, "goggle": od_goggle,
    "goggle_strap": od_goggle_strap,
}
