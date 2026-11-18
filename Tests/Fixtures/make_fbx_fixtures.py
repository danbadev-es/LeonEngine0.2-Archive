"""
Builds the FBX files the importer tests read. Run by Blender, not by the system Python:

    blender -b --factory-startup --python Tests/Fixtures/make_fbx_fixtures.py

TwoUVCube.fbx: a 2 m cube with flat faces, a texture unwrap in the first UV set and a lightmap
unwrap (six separate squares, nothing overlapping) in the second.
"""

import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))


def make_two_uv_cube():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.mesh.primitive_cube_add(size=2.0)
    cube = bpy.context.active_object
    cube.name = "TwoUVCube"
    mesh = cube.data
    mesh.uv_layers[0].name = "UVMap"
    lightmap = mesh.uv_layers.new(name="Lightmap")

    # Six faces in a 3 x 2 grid with a margin: what a lightmap unwrap must look like.
    corners = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    for poly in mesh.polygons:
        column, row = poly.index % 3, poly.index // 3
        for corner, loop_index in enumerate(poly.loop_indices):
            u, v = corners[corner]
            lightmap.data[loop_index].uv = ((column + 0.05 + 0.9 * u) / 3.0, (row + 0.05 + 0.9 * v) / 2.0)

    material = bpy.data.materials.new("FixtureGrey")
    mesh.materials.append(material)

    bpy.ops.export_scene.fbx(
        filepath=os.path.join(HERE, "TwoUVCube.fbx"),
        use_selection=False,
        object_types={"MESH"},
        axis_forward="-Z",
        axis_up="Y",
        apply_scale_options="FBX_SCALE_NONE",
        mesh_smooth_type="FACE",
        use_mesh_modifiers=True,
        bake_anim=False,
        path_mode="STRIP",
    )


make_two_uv_cube()
print("fixtures written to", HERE)
