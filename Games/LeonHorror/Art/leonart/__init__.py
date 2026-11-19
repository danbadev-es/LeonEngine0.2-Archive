"""
leonart - authoring of LeonHorror's art by script.

Everything here runs inside Blender's Python (it brings numpy and bpy); nothing is drawn by hand
and nothing is downloaded. The scripts are the source: textures and meshes are generated into an
ignored Intermediate folder, imported by AssetTool, and only the cooked assets are versioned.

    png        writes PNG files without any imaging library
    noise      repeatable, tileable noise and patterns (numpy)
    texture    a PBR texture set: colour, height, roughness, metalness, and what is derived from them
    materials  the material library
    mesh       building, unwrapping and exporting meshes (bpy)
    kit        the pieces of the level

Conventions
    Units are metres. In Blender Z is up and a character faces -Y; the exporter turns that into the
    engine's Y up / -Z forward.
    A texture set is `T_<Name>_Color.png` (sRGB), `T_<Name>_Normal.png` and `T_<Name>_ORM.png`
    (occlusion red, roughness green, metalness blue); the names tell the importer what each one is.
    Row 0 of every image is v = 0, which is how the engine reads a texture.
"""
