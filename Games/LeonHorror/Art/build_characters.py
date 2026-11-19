"""
LeonHorror characters. Run by Blender, not by the system Python:

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
        --python Games/LeonHorror/Art/build_characters.py -- [--only Protagonist,Patient,Orderly] [--sheets]

What it writes
    <out>/Raw/Characters/SK_<Name>.fbx            armature + one skinned mesh, for AssetTool
    <out>/Raw/Characters/T_<Name>_{Color,Normal,ORM}.png
    <content>/Materials/M_<Name>.lmat             the material the mesh slot points at
    <out>/Sheets/Character_<Name>_*.png           with --sheets: turnaround and posed stills

<out> defaults to Games/LeonHorror/Intermediate/Art and <content> to Games/LeonHorror/Content.
Then import and check:

    out/build/ci/bin/AssetTool.exe import --raw Games/LeonHorror/Intermediate/Art/Raw/Characters --content Games/LeonHorror/Content
    python Games/LeonHorror/Art/inspect_character.py Games/LeonHorror/Content SK_Protagonist SK_Patient SK_Orderly

Other options
    --clay        skip the textures: grey shapes only (for working on a silhouette), implies --sheets
    --no-export   pictures only
    --quick       fewer samples in the pictures and no baked occlusion
    --sheet A,B   only these sheets (Turnaround, Rest, Poses, Clips, Closeups)

The characters share the mannequin's skeleton (Raw/YBot.fbx); see leonart/charrig.py for how the
FBX is made to import with exactly its bones, and leonart/characters.py for the characters.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from leonart import characters, charproduce, charrig  # noqa: E402


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    project = os.path.dirname(HERE)
    args = {"only": "", "out": os.path.join(project, "Intermediate", "Art"), "content": os.path.join(project, "Content"),
            "rig": os.path.join(project, "Raw", "YBot.fbx"), "anims": os.path.join(project, "Raw", "Anim"),
            "sheet": "", "sheets": False, "clay": False, "no-export": False, "quick": False}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip("-")
        if key in args and isinstance(args[key], bool):
            args[key] = True
            i += 1
        elif key in args and i + 1 < len(argv):
            args[key] = argv[i + 1]
            i += 2
        else:
            raise SystemExit("unknown option " + argv[i])
    args["only"] = [name for name in args["only"].split(",") if name] or list(characters.BUILDERS)
    return args


def main():
    args = parse_args()
    for name in args["only"]:
        if name not in characters.BUILDERS:
            raise SystemExit("unknown character '%s'; known: %s" % (name, ", ".join(characters.BUILDERS)))
        started = time.time()
        charrig.reset_scene()
        rig = charrig.Rig(args["rig"])
        character = characters.BUILDERS[name](rig)
        charproduce.produce(rig, character, args)
        print("CHARACTER %s done in %.1f s" % (name, time.time() - started))


main()
