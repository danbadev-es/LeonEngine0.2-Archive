#pragma once

#include "Scene/FParticleTypes.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Leon {

    class IRenderScene;

    /** One corner of a sprite, as SpriteParticles.hlsl reads it. */
    struct FSpriteVertex {
        glm::vec3 Position{0.0f};
        /** xy = texture coordinates, z = metres over which it fades out near the surface behind it. */
        glm::vec3 UVFade{0.0f};
        /** Colour and alpha at this point of the particle's life. */
        glm::vec4 Color{1.0f};
        /**
         * rgb = the ambient irradiance where the emitter is (probes, else the sky); w = how much of the
         * lamps' light it takes, or -1: not lit, the colour is what it gives off.
         */
        glm::vec4 Ambient{0.0f};
    };

    /** One emitter's sprites: a run of the vertices, drawn with one texture. */
    struct FSpriteDraw {
        const FSpriteEmitterSettings* Settings = nullptr;
        uint32_t FirstVertex = 0;
        uint32_t VertexCount = 0;
    };

    /**
     * The sprite particles of a world, ready to draw for one camera: emitters from the farthest to the
     * nearest, and in each the particles from the farthest to the nearest, so that blending them one
     * after the other puts the nearer over the farther. Six vertices a particle (two triangles).
     */
    struct FSpriteParticleBatch {
        std::vector<FSpriteVertex> Vertices;
        std::vector<FSpriteDraw> Draws;
        uint32_t ParticleCount = 0;

        /**
         * InRight / InUp / InForward: the camera's axes. InSkyIrradiance: the ambient irradiance, averaged
         * over every direction, for emitters where the world has no light probes.
         */
        void Build(IRenderScene& InWorld, const glm::vec3& InCameraPosition, const glm::vec3& InRight,
                   const glm::vec3& InUp, const glm::vec3& InForward, const glm::vec3& InSkyIrradiance);
    };

} // namespace Leon
