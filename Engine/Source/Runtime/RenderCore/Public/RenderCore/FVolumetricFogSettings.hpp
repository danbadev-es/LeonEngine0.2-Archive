#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Leon {

    /**
     * The air of a map as something light shows up in: the beam of a flashlight, the cone under a
     * ceiling lamp, the shafts an occluder cuts into them. Off by default; a map without it is drawn
     * exactly as before. How it is drawn is in VolumetricFog.hlsl and Docs/RENDERER.md.
     */
    struct FVolumetricFogSettings {
        bool bEnabled = false;
        /**
         * How much of the light is taken out of a ray per metre of air (extinction). 0.02 is clean
         * indoor air with a beam just visible, 0.1 a room full of dust. 0 draws nothing.
         */
        float Density = 0.03f;
        /**
         * How much of what the air takes from a beam it sends on rather than swallows. 1 is physical
         * for white haze; higher makes beams brighter without making the air thicker.
         */
        float Scattering = 1.0f;
        /**
         * Henyey-Greenstein g, -0.95..0.95: 0 scatters the same in every direction; towards 1 the
         * light keeps going forward, so a beam is brightest when looked into and faint from behind.
         */
        float Anisotropy = 0.3f;
        /**
         * Light the air has from everywhere, as radiance, times AmbientIntensity: what the air glows
         * with outside every beam. Not used while the map's distance fog is on (its FogColor is that).
         */
        glm::vec3 AmbientColor{1.0f};
        float AmbientIntensity = 0.0f;
        /** Metres from the camera beyond which the air is not looked at. */
        float MaxDistance = 30.0f;
        /** Samples along the part of a view ray a light reaches. 0: what the renderer's quality says. */
        uint32_t StepCount = 0;
    };

    /** The step counts a view ray may be marched with; what the map or the quality asks for is held to them. */
    inline constexpr uint32_t kMinVolumetricFogSteps = 4;
    inline constexpr uint32_t kMaxVolumetricFogSteps = 64;
    /** Lights of each kind whose light the air shows: the ones that have a shadow map in the frame. */
    inline constexpr uint32_t kMaxVolumetricFogSpotLights = 4;
    inline constexpr uint32_t kMaxVolumetricFogPointLights = 4;

} // namespace Leon
