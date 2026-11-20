#pragma once

#include "Scene/FParticleTypes.hpp"

#include <cstdint>
#include <vector>

namespace Leon {

    float ParticleRng01(uint32_t& InOutState);
    glm::vec3 ParticleRandomInRange(uint32_t& InOutState, const glm::vec3& InMin, const glm::vec3& InMax);

    int32_t AppendBurstParticles(std::vector<FSimulatedParticle>& InOutParticles,
                                 const FParticleEmitterSettings& InSettings, const glm::vec3& InOrigin,
                                 uint32_t& InOutRngState, int32_t InMaxParticles);

    bool AppendBeamParticle(std::vector<FSimulatedParticle>& InOutParticles, const FParticleEmitterSettings& InSettings,
                            const glm::vec3& InStart, int32_t InMaxParticles);

    void SimulateParticles(std::vector<FSimulatedParticle>& InOutParticles, float InDeltaSeconds);

    void CopyParticlesForRender(const std::vector<FSimulatedParticle>& InParticles,
                                std::vector<FParticleInstance>& OutRenderParticles);

    /**
     * Advances a sprite emitter by InDeltaSeconds (at most a quarter of a second at a time): on its
     * first tick it starts its random numbers from InSeed and releases its burst; then it spawns at its
     * rate inside its volume (placed by InWorld, the actor's matrix), moves its particles (gravity, then
     * drag, then velocity) and drops those whose life is over. A disabled emitter holds nothing.
     */
    void TickSpriteEmitter(FSpriteEmitterComponent& InOutEmitter, const glm::mat4& InWorld, float InDeltaSeconds,
                           uint32_t InSeed);

} // namespace Leon
