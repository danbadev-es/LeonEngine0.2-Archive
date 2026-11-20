#pragma once

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Leon {

    enum class EParticleKind : uint8_t { SpriteBurst = 0, Beam = 1 };

    /**
     * Generic one-shot / short-lived emitter description.
     * Gameplay meaning (muzzle, tracer, impact) belongs in game code.
     */
    struct FParticleEmitterSettings {
        EParticleKind Kind = EParticleKind::SpriteBurst;
        int32_t BurstCount = 10;
        float Lifetime = 0.08f;
        float Size = 0.06f;
        float SizeEnd = 0.015f;
        glm::vec4 Color{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 ColorEnd{1.0f, 1.0f, 1.0f, 0.0f};
        glm::vec3 VelocityMin{-0.5f, 0.1f, -0.5f};
        glm::vec3 VelocityMax{0.5f, 1.4f, 0.5f};
        glm::vec3 Gravity{0.0f, -6.0f, 0.0f};
        glm::vec3 BeamEnd{0.0f};
        float BeamThickness = 0.025f;
        bool bOneShot = true;
    };

    static constexpr int32_t kMaxBurstParticles = 24;
    static constexpr int32_t kMaxLiveParticleEmitters = 48;
    static constexpr int32_t kMaxTransientParticles = 512;
    static constexpr int32_t kMaxRenderedParticles = 320;
    static constexpr float kParticleCullDistance = 55.0f;

    struct FParticleInstance {
        glm::vec3 Location{0.0f};
        glm::vec3 Velocity{0.0f};
        glm::vec3 BeamEnd{0.0f};
        glm::vec4 Color{1.0f};
        glm::vec4 ColorEnd{1.0f, 1.0f, 1.0f, 0.0f};
        float Age = 0.0f;
        float Lifetime = 0.08f;
        float Size = 0.06f;
        float SizeEnd = 0.015f;
        EParticleKind Kind = EParticleKind::SpriteBurst;
    };

    /** Simulated particle with per-instance gravity (world pool). */
    struct FSimulatedParticle : FParticleInstance {
        glm::vec3 Gravity{0.0f};
    };

    /** EnTT render mirror written by UParticleComponent. */
    struct FParticleRenderComponent {
        std::vector<FParticleInstance> Particles;
        bool bVisible = true;
    };

    // --- Sprite particles: blended, textured, lit -------------------------------------------------

    /** The sprite drawn when an emitter names no texture. */
    enum class ESpriteShape : uint8_t {
        /** A soft round puff: steam, smoke, mist. */
        Puff = 0,
        /** A small sharp dot: dust motes, sparks of light. */
        Speck = 1
    };

    /** Where new particles appear, around the emitter's actor. */
    enum class ESpriteSpawnShape : uint8_t { Box = 0, Sphere = 1 };

    /** Particles one emitter may hold, and all emitters of a frame may draw. */
    inline constexpr int32_t kMaxSpriteParticlesPerEmitter = 1024;
    inline constexpr int32_t kMaxSpriteParticlesPerFrame = 8192;

    /**
     * A cloud of camera-facing sprites blended over the scene, back to front (a ParticleEmitter: block
     * on an actor): dust in a lamp's beam, steam, smoke. Lit by the light probes where the emitter is
     * and by the lamps lit this frame, soft where it meets the scene.
     */
    struct FSpriteEmitterSettings {
        bool bEnabled = true;
        /** A texture asset; its alpha is the sprite's shape and its colour tints it. Empty: Shape. */
        std::string Texture;
        ESpriteShape Shape = ESpriteShape::Puff;
        /** Colour and alpha at birth and at death, mixed linearly in between. */
        glm::vec4 ColorStart{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 ColorEnd{1.0f, 1.0f, 1.0f, 1.0f};
        /** Fractions of the life over which the alpha rises from nothing and falls back to it. */
        float FadeIn = 0.15f;
        float FadeOut = 0.35f;
        /** Width of a sprite in metres, at birth and at death. */
        float SizeStart = 0.1f;
        float SizeEnd = 0.1f;
        /** New particles a second, and how many appear at once when the emitter starts. */
        float SpawnRate = 10.0f;
        int32_t Burst = 0;
        /** Seconds a particle lives, give or take this fraction of it. */
        float Lifetime = 3.0f;
        float LifetimeSpread = 0.25f;
        /** Velocity at birth in world space (m/s), plus up to this much either way on each axis. */
        glm::vec3 Velocity{0.0f};
        glm::vec3 VelocitySpread{0.0f};
        /** The fraction of its velocity a particle loses a second; and the acceleration on it (m/s2). */
        float Drag = 0.0f;
        glm::vec3 Gravity{0.0f};
        /** Box: half-sizes around the actor, along its axes. Sphere: radius = SpawnExtent.x. */
        ESpriteSpawnShape SpawnShape = ESpriteSpawnShape::Box;
        glm::vec3 SpawnExtent{0.5f};
        int32_t MaxParticles = 128;
        /** Lit by probes and lamps. Off: the colour is what the sprite gives off (HDR, may exceed 1). */
        bool bLit = true;
        /** How much of the lamps' light a lit sprite takes (0 = the probes' ambient light only). */
        float LightFraction = 1.0f;
        /** Metres over which a sprite fades out as it nears the surface behind it. */
        float DepthFade = 0.25f;
        /** Each particle turned by a random angle, so that puffs do not all look alike. */
        bool bRandomRotation = true;
    };

    struct FSpriteParticle {
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        float Age = 0.0f;
        float Lifetime = 1.0f;
        /** Radians, in the plane of the screen. */
        float Rotation = 0.0f;
    };

    /** An emitter's settings (saved with the map) and its particles (not saved). */
    struct FSpriteEmitterComponent {
        FSpriteEmitterSettings Settings;
        std::vector<FSpriteParticle> Particles;
        /** Fraction of a particle owed to the spawn rate since the last one. */
        float SpawnCarry = 0.0f;
        uint32_t RandomState = 0;
        bool bStarted = false;

        FSpriteEmitterComponent() = default;
        FSpriteEmitterComponent(const FSpriteEmitterComponent&) = default;
        explicit FSpriteEmitterComponent(const FSpriteEmitterSettings& InSettings) : Settings(InSettings) {}
    };

    /** Fraction of a particle's life gone, 0 at birth, 1 at death. */
    inline float SpriteLifeFraction(const FSpriteParticle& InParticle) {
        return InParticle.Lifetime > 1e-5f ? std::clamp(InParticle.Age / InParticle.Lifetime, 0.0f, 1.0f) : 1.0f;
    }

    /** Colour and alpha at a fraction of the life: the two colours mixed, the alpha faded in and out. */
    inline glm::vec4 SpriteColorOverLife(const FSpriteEmitterSettings& InSettings, float InLife) {
        const float life = std::clamp(InLife, 0.0f, 1.0f);
        glm::vec4 color = InSettings.ColorStart + (InSettings.ColorEnd - InSettings.ColorStart) * life;
        const float fadeIn = InSettings.FadeIn > 1e-5f ? std::clamp(life / InSettings.FadeIn, 0.0f, 1.0f) : 1.0f;
        const float fadeOut =
            InSettings.FadeOut > 1e-5f ? std::clamp((1.0f - life) / InSettings.FadeOut, 0.0f, 1.0f) : 1.0f;
        color.a *= fadeIn * fadeOut;
        return color;
    }

    inline float SpriteSizeOverLife(const FSpriteEmitterSettings& InSettings, float InLife) {
        const float life = std::clamp(InLife, 0.0f, 1.0f);
        return std::max(InSettings.SizeStart + (InSettings.SizeEnd - InSettings.SizeStart) * life, 0.0f);
    }

} // namespace Leon
