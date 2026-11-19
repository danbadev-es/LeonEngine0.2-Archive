"""
LeonHorror art build. Run by Blender, not by the system Python (Tools/build_art.py does it for you):

    blender -b --factory-startup --python Games/LeonHorror/Art/build.py -- materials [--only Name,...]
    blender -b --factory-startup --python Games/LeonHorror/Art/build.py -- meshes    [--only Name,...]

Outputs
    <out>/Raw/Textures/T_<Name>_*.png      source images for AssetTool (not versioned)
    <out>/Raw/Meshes/SM_<Name>.fbx         source meshes for AssetTool (not versioned)
    <out>/Sheets/*.png                     contact sheets to review a batch at a glance
    <content>/Materials/M_<Name>.lmat      the materials themselves (versioned)

<out> defaults to Games/LeonHorror/Intermediate/Art and <content> to Games/LeonHorror/Content.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

from leonart import materials, png  # noqa: E402


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    project = os.path.dirname(HERE)
    args = {"command": argv[0] if argv else "", "only": "", "out": os.path.join(project, "Intermediate", "Art"),
            "content": os.path.join(project, "Content"), "size": ""}
    i = 1
    while i < len(argv):
        key = argv[i].lstrip("-")
        if key in args and i + 1 < len(argv):
            args[key] = argv[i + 1]
            i += 2
        else:
            i += 1
    args["only"] = [name for name in args["only"].split(",") if name]
    return args


def contact_sheet(path, tiles, columns=4, label_height=0):
    """tiles: uint8 images of the same size. Laid out left to right, top to bottom."""
    if not tiles:
        return
    size = max(max(tile.shape[0], tile.shape[1]) for tile in tiles)
    rows = (len(tiles) + columns - 1) // columns
    gap = 8
    sheet = np.full((rows * (size + gap) + gap, columns * (size + gap) + gap, 3), 24, dtype=np.uint8)
    for index, tile in enumerate(tiles):
        row, column = divmod(index, columns)
        y, x = gap + row * (size + gap), gap + column * (size + gap)
        sheet[y:y + tile.shape[0], x:x + tile.shape[1]] = tile[:, :, :3]
    png.write(path, sheet)


def run_materials(args):
    names = args["only"] or sorted(materials.LIBRARY)
    textures = os.path.join(args["out"], "Raw", "Textures")
    material_folder = os.path.join(args["content"], "Materials")
    previews = []
    for name in names:
        if name not in materials.LIBRARY:
            raise SystemExit("unknown material '%s'; known: %s" % (name, ", ".join(sorted(materials.LIBRARY))))
        started = time.time()
        result = materials.build(name, int(args["size"]) if args["size"] else None)
        result.save(textures)
        result.write_material(material_folder)
        # Two views for the sheet: the surface under a light, and the same seen four times over to
        # show how it repeats.
        lit = result.preview(size=384)
        tiled = np.tile(result.preview(size=192), (2, 2, 1))
        previews += [lit, tiled]
        print("MATERIAL %-24s %4d px  %.1f m  %.1f s" % (name, result.size, result.tile_size, time.time() - started))
    if not args["only"]:
        for name in sorted(materials.FLAT):
            materials.write_flat(name, material_folder)
            print("MATERIAL %-24s flat" % name)
    contact_sheet(os.path.join(args["out"], "Sheets", "Materials.png"), previews, columns=4)
    print("SHEET", os.path.join(args["out"], "Sheets", "Materials.png"))


def main():
    args = parse_args()
    if args["command"] == "materials":
        run_materials(args)
    elif args["command"] == "meshes":
        from leonart import kit
        kit.run(args)
    else:
        raise SystemExit("usage: build.py -- materials|meshes [--only A,B] [--out dir] [--content dir]")


main()
