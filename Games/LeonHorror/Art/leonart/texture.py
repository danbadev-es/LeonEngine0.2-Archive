"""
A PBR texture set and what is derived from it.

A material function fills in the maps it knows about (colour, height, roughness, metalness) and the
set works out the rest: the normal map from the height, the occlusion from the cavities. Heights
are in metres and `tile_size` says how large the texture is in the world, so the slopes of the
normal map are the true slopes of the surface.
"""

import os

import numpy as np

from . import noise, png


class TextureSet:
    def __init__(self, name, size=1024, tile_size=1.0):
        self.name = name
        self.size = int(size)
        self.tile_size = float(tile_size)  # metres covered by one repeat of the texture
        shape = (self.size, self.size)
        self.color = np.full(shape + (3,), 0.5, dtype=np.float32)  # linear albedo
        self.height = np.zeros(shape, dtype=np.float32)  # metres
        self.roughness = np.full(shape, 0.5, dtype=np.float32)
        self.metallic = np.zeros(shape, dtype=np.float32)
        self.occlusion = None  # derived from the height unless a material sets it
        self.alpha = None  # optional, for decals and cut-outs
        self.emissive = None  # optional linear colour
        self.normal_strength = 1.0
        self.occlusion_strength = 1.0
        # Written into the .lmat next to the maps.
        self.material = {}

    # ------------------------------------------------------------------ derived maps
    def normal_map(self):
        """
        Tangent-space normal, shape (size, size, 3), components -1..1. x follows +u and y follows +v
        (the direction of rising row index), which is the basis the engine builds from the UVs.
        """
        texel = self.tile_size / self.size
        height = self.height * self.normal_strength
        du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) / (2.0 * texel)
        dv = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) / (2.0 * texel)
        normal = np.stack([-du, -dv, np.ones_like(du)], axis=-1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        return normal.astype(np.float32)

    def occlusion_map(self):
        """Darkens what lies below its surroundings: joints, pores, dents."""
        if self.occlusion is not None:
            return np.clip(self.occlusion, 0.0, 1.0)
        span = float(self.height.max() - self.height.min())
        if span < 1.0e-7:
            return np.ones_like(self.height)
        radius = max(2, self.size // 96)
        around = noise.blur(noise.blur(self.height, radius), radius)
        cavity = np.clip((around - self.height) / span, 0.0, 1.0)
        return np.clip(1.0 - cavity * 2.2 * self.occlusion_strength, 0.0, 1.0).astype(np.float32)

    # ------------------------------------------------------------------ files
    def save(self, folder):
        """Writes the PNG files and returns their paths by role."""
        os.makedirs(folder, exist_ok=True)
        paths = {}

        color = png.to_bytes(png.srgb_encode(self.color))
        if self.alpha is not None:
            color = np.concatenate([color, png.to_bytes(self.alpha)[:, :, None]], axis=-1)
        paths["Color"] = os.path.join(folder, "T_%s_Color.png" % self.name)
        png.write(paths["Color"], color)

        paths["Normal"] = os.path.join(folder, "T_%s_Normal.png" % self.name)
        png.write(paths["Normal"], png.to_bytes(self.normal_map() * 0.5 + 0.5))

        orm = np.stack([self.occlusion_map(), np.clip(self.roughness, 0.04, 1.0), np.clip(self.metallic, 0.0, 1.0)], axis=-1)
        paths["ORM"] = os.path.join(folder, "T_%s_ORM.png" % self.name)
        png.write(paths["ORM"], png.to_bytes(orm))

        if self.emissive is not None:
            paths["Emissive"] = os.path.join(folder, "T_%s_Emissive.png" % self.name)
            png.write(paths["Emissive"], png.to_bytes(png.srgb_encode(self.emissive)))
        return paths

    def write_material(self, folder):
        """The .lmat that uses the set. Scalars are 1 so the maps are taken as they are."""
        values = {
            "AlbedoColor": "[1, 1, 1]",
            "Metallic": "1",
            "Roughness": "1",
            "AO": "1",
            "NormalScale": "1",
            "OcclusionStrength": "1",
            "AlphaMode": '"Opaque"',
            "AlphaCutoff": "0.5",
            "DoubleSided": "false",
            "UVTiling": "[1, 1]",
            "UVOffset": "[0, 0]",
            "UsePlanarReflection": "false",
            "AlbedoMap": '"/Game/Textures/T_%s_Color.ltex"' % self.name,
            "NormalMap": '"/Game/Textures/T_%s_Normal.ltex"' % self.name,
            "ORMMap": '"/Game/Textures/T_%s_ORM.ltex"' % self.name,
        }
        if self.emissive is not None:
            values["EmissiveMap"] = '"/Game/Textures/T_%s_Emissive.ltex"' % self.name
            values["EmissiveColor"] = "[1, 1, 1]"
            values["EmissiveIntensity"] = "1"
        if self.alpha is not None:
            values["AlphaMode"] = '"Blend"'
        values.update(self.material)

        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, "M_%s.lmat" % self.name)
        lines = ["# LeonEngine2 Material Asset File (.lmat)", "# Written by Games/LeonHorror/Art (leonart). Edit the script, not this file.",
                 "Material:", '  Type: "PBR_Lit"']
        lines += ["  %s: %s" % (key, value) for key, value in values.items()]
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
        return path

    # ------------------------------------------------------------------ review
    def preview(self, light=(0.45, 0.6, 0.66), size=256):
        """
        A lit thumbnail (uint8, size x size x 3) so a material can be judged without the engine:
        the colour shaded by the normal map under one light, with occlusion and a simple highlight.
        """
        step = max(1, self.size // size)

        def shrink(values):
            # The mean of each block, as a mip level would be: skipping texels makes fine detail
            # look far harsher than it is on screen.
            count = (values.shape[0] // step) * step
            trimmed = values[:count, :count]
            shape = (count // step, step, count // step, step) + trimmed.shape[2:]
            return trimmed.reshape(shape).mean(axis=(1, 3))

        color = shrink(self.color)
        normal = shrink(self.normal_map())
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        occlusion = shrink(self.occlusion_map())
        roughness = shrink(self.roughness)
        metallic = shrink(self.metallic)

        direction = np.array(light, dtype=np.float32)
        direction /= np.linalg.norm(direction)
        diffuse = np.clip((normal * direction).sum(axis=-1), 0.0, 1.0)
        half = direction + np.array([0.0, 0.0, 1.0], dtype=np.float32)
        half /= np.linalg.norm(half)
        shine = np.clip((normal * half).sum(axis=-1), 0.0, 1.0) ** (2.0 / np.maximum(roughness, 0.05) ** 2)
        shine *= (1.0 - roughness) * 0.6

        base = color * (1.0 - metallic[:, :, None])
        specular = 0.04 * (1.0 - metallic[:, :, None]) + color * metallic[:, :, None]
        lit = base * (0.18 + 0.95 * diffuse[:, :, None]) * occlusion[:, :, None] + specular * (shine[:, :, None] * 6.0 + 0.25 * metallic[:, :, None])
        return png.to_bytes(png.srgb_encode(lit))
