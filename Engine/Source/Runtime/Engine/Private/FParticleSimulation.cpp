#include "Engine/FParticleSimulation.hpp"

#include <algorithm>
#include <cmath>

namespace Leon {

    float ParticleRng01(uint32_t& InOutState) {
        InOutState = InOutState * 1664525u + 1013904223u;
        return static_cast<float>(InOutState & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
    }

    glm::vec3 ParticleRandomInRange(uint32_t& InOutState, const glm::vec3& InMin, const glm::vec3& InMax) {
        return {InMin.x + (InMax.x - InMin.x) * ParticleRng01(InOutState),
                InMin.y + (InMax.y - InMin.y) * ParticleRng01(InOutState),
                InMin.z + (InMax.z - InMin.z) * ParticleRng01(InOutState)};
    }

    int32_t AppendBurstParticles(std::vector<FSimulatedParticle>& InOutParticles,
                                 const FParticleEmitterSettings& InSettings, const glm::vec3& InOrigin,
                                 uint32_t& InOutRngState, int32_t InMaxParticles) {
        const int32_t room = std::max(0, InMaxParticles - static_cast<int32_t>(InOutParticles.size()));
        if (room <= 0)
            return 0;
        const int32_t spawnCount = std::min(std::clamp(InSettings.BurstCount, 1, kMaxBurstParticles), room);
        InOutParticles.reserve(InOutParticles.size() + static_cast<size_t>(spawnCount));
        for (int32_t i = 0; i < spawnCount; ++i) {
            FSimulatedParticle p;
            p.Kind = EParticleKind::SpriteBurst;
            p.Location = InOrigin;
            p.Velocity = ParticleRandomInRange(InOutRngState, InSettings.VelocityMin, InSettings.VelocityMax);
            p.Color = InSettings.Color;
            p.ColorEnd = InSettings.ColorEnd;
            p.Lifetime = std::max(0.01f, InSettings.Lifetime);
            p.Size = InSettings.Size;
            p.SizeEnd = InSettings.SizeEnd;
            p.Gravity = InSettings.Gravity;
            InOutParticles.push_back(p);
        }
        return spawnCount;
    }

    bool AppendBeamParticle(std::vector<FSimulatedParticle>& InOutParticles, const FParticleEmitterSettings& InSettings,
                            const glm::vec3& InStart, int32_t InMaxParticles) {
        if (static_cast<int32_t>(InOutParticles.size()) >= InMaxParticles)
            return false;
        FSimulatedParticle p;
        p.Kind = EParticleKind::Beam;
        p.Location = InStart;
        p.BeamEnd = InSettings.BeamEnd;
        p.Color = InSettings.Color;
        p.ColorEnd = InSettings.ColorEnd;
        p.Lifetime = std::max(0.01f, InSettings.Lifetime);
        p.Size = InSettings.BeamThickness;
        p.SizeEnd = InSettings.BeamThickness * 0.35f;
        InOutParticles.push_back(p);
        return true;
    }

    void SimulateParticles(std::vector<FSimulatedParticle>& InOutParticles, float InDeltaSeconds) {
        for (FSimulatedParticle& p : InOutParticles) {
            p.Age += InDeltaSeconds;
            if (p.Kind == EParticleKind::SpriteBurst) {
                p.Velocity += p.Gravity * InDeltaSeconds;
                p.Location += p.Velocity * InDeltaSeconds;
            }
        }
        InOutParticles.erase(std::remove_if(InOutParticles.begin(), InOutParticles.end(),
                                            [](const FSimulatedParticle& p) { return p.Age >= p.Lifetime; }),
                             InOutParticles.end());
    }

    void CopyParticlesForRender(const std::vector<FSimulatedParticle>& InParticles,
                                std::vector<FParticleInstance>& OutRenderParticles) {
        OutRenderParticles.clear();
        OutRenderParticles.reserve(InParticles.size());
        for (const FSimulatedParticle& p : InParticles)
            OutRenderParticles.push_back(p);
    }

    namespace {

        bool IsFinite(const glm::vec3& InValue) {
            return std::isfinite(InValue.x) && std::isfinite(InValue.y) && std::isfinite(InValue.z);
        }

        /** -1..1 */
        float SignedRandom(uint32_t& InOutState) {
            return ParticleRng01(InOutState) * 2.0f - 1.0f;
        }

        void SpawnSprite(FSpriteEmitterComponent& InOutEmitter, const glm::mat4& InWorld) {
            const FSpriteEmitterSettings& settings = InOutEmitter.Settings;
            uint32_t& random = InOutEmitter.RandomState;
            glm::vec3 local(0.0f);
            if (settings.SpawnShape == ESpriteSpawnShape::Sphere) {
                // Uniform in the ball: the first point of the cube that falls inside it.
                for (int attempt = 0; attempt < 16; ++attempt) {
                    local = glm::vec3(SignedRandom(random), SignedRandom(random), SignedRandom(random));
                    if (glm::dot(local, local) <= 1.0f)
                        break;
                    local = glm::vec3(0.0f);
                }
                local *= std::max(settings.SpawnExtent.x, 0.0f);
            } else {
                local = glm::vec3(SignedRandom(random), SignedRandom(random), SignedRandom(random)) *
                        glm::max(settings.SpawnExtent, glm::vec3(0.0f));
            }
            FSpriteParticle particle;
            particle.Position = glm::vec3(InWorld * glm::vec4(local, 1.0f));
            particle.Velocity = settings.Velocity + glm::vec3(SignedRandom(random), SignedRandom(random), SignedRandom(random)) *
                                                        glm::max(settings.VelocitySpread, glm::vec3(0.0f));
            const float spread = std::clamp(settings.LifetimeSpread, 0.0f, 1.0f);
            particle.Lifetime = std::max(settings.Lifetime * (1.0f + spread * SignedRandom(random)), 0.01f);
            particle.Rotation = settings.bRandomRotation ? ParticleRng01(random) * 6.2831853f : 0.0f;
            if (IsFinite(particle.Position) && IsFinite(particle.Velocity) && std::isfinite(particle.Lifetime))
                InOutEmitter.Particles.push_back(particle);
        }

    } // namespace

    void TickSpriteEmitter(FSpriteEmitterComponent& InOutEmitter, const glm::mat4& InWorld, float InDeltaSeconds,
                           uint32_t InSeed) {
        const FSpriteEmitterSettings& settings = InOutEmitter.Settings;
        if (!settings.bEnabled) {
            InOutEmitter.Particles.clear();
            InOutEmitter.SpawnCarry = 0.0f;
            InOutEmitter.bStarted = false;
            return;
        }
        const int32_t maxParticles = std::clamp(settings.MaxParticles, 0, kMaxSpriteParticlesPerEmitter);
        const auto room = [&]() { return maxParticles - static_cast<int32_t>(InOutEmitter.Particles.size()); };
        if (!InOutEmitter.bStarted) {
            InOutEmitter.bStarted = true;
            InOutEmitter.RandomState = InSeed * 2654435761u + 1u;
            for (int32_t i = 0; i < settings.Burst && room() > 0; ++i)
                SpawnSprite(InOutEmitter, InWorld);
        }
        const float dt = std::isfinite(InDeltaSeconds) ? std::clamp(InDeltaSeconds, 0.0f, 0.25f) : 0.0f;

        // Move first, then spawn: a particle born this tick starts where it was spawned.
        const float keep = std::exp(-std::max(settings.Drag, 0.0f) * dt);
        for (FSpriteParticle& particle : InOutEmitter.Particles) {
            particle.Velocity = (particle.Velocity + settings.Gravity * dt) * keep;
            particle.Position += particle.Velocity * dt;
            particle.Age += dt;
        }
        InOutEmitter.Particles.erase(
            std::remove_if(InOutEmitter.Particles.begin(), InOutEmitter.Particles.end(),
                           [](const FSpriteParticle& InParticle) {
                               return InParticle.Age >= InParticle.Lifetime || !IsFinite(InParticle.Position) ||
                                      !IsFinite(InParticle.Velocity);
                           }),
            InOutEmitter.Particles.end());
        // A maximum lowered while playing holds at once: the oldest go (they are first in the list).
        if (room() < 0)
            InOutEmitter.Particles.erase(InOutEmitter.Particles.begin(), InOutEmitter.Particles.begin() - room());

        const float rate = std::isfinite(settings.SpawnRate) ? std::max(settings.SpawnRate, 0.0f) : 0.0f;
        InOutEmitter.SpawnCarry += rate * dt;
        const float whole = std::floor(InOutEmitter.SpawnCarry);
        InOutEmitter.SpawnCarry -= whole;
        const int32_t owed = static_cast<int32_t>(std::min(whole, static_cast<float>(std::max(room(), 0))));
        for (int32_t i = 0; i < owed && room() > 0; ++i)
            SpawnSprite(InOutEmitter, InWorld);
        if (room() <= 0)
            InOutEmitter.SpawnCarry = 0.0f; // full: the owed particles are not stored up for later
    }

} // namespace Leon
