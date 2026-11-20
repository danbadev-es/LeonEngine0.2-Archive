#include "Renderer/FSpriteParticleBatch.hpp"

#include "Scene/IRenderScene.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Leon {

    void FSpriteParticleBatch::Build(IRenderScene& InWorld, const glm::vec3& InCameraPosition, const glm::vec3& InRight,
                                     const glm::vec3& InUp, const glm::vec3& InForward,
                                     const glm::vec3& InSkyIrradiance) {
        Vertices.clear();
        Draws.clear();
        ParticleCount = 0;

        struct FEmitter {
            const FSpriteEmitterComponent* Component = nullptr;
            glm::vec3 Center{0.0f};
            float DistanceSq = 0.0f;
        };
        std::vector<FEmitter> emitters;
        auto& registry = InWorld.GetRegistry();
        for (auto entity : registry.view<FSpriteEmitterComponent>()) {
            const FSpriteEmitterComponent& component = registry.get<FSpriteEmitterComponent>(entity);
            if (!component.Settings.bEnabled || component.Particles.empty())
                continue;
            FEmitter emitter;
            emitter.Component = &component;
            for (const FSpriteParticle& particle : component.Particles)
                emitter.Center += particle.Position;
            emitter.Center /= static_cast<float>(component.Particles.size());
            const glm::vec3 offset = emitter.Center - InCameraPosition;
            emitter.DistanceSq = glm::dot(offset, offset);
            emitters.push_back(emitter);
        }
        // The farthest emitter first. Equal distances keep the registry's order, so a frame does not
        // depend on how the sort breaks ties.
        std::stable_sort(emitters.begin(), emitters.end(),
                         [](const FEmitter& InA, const FEmitter& InB) { return InA.DistanceSq > InB.DistanceSq; });

        std::vector<uint32_t> order;
        std::vector<float> depths;
        for (const FEmitter& emitter : emitters) {
            const FSpriteEmitterSettings& settings = emitter.Component->Settings;
            const std::vector<FSpriteParticle>& particles = emitter.Component->Particles;

            // Where it is lit from: the probes at the middle of the cloud, else the sky; -1 = unlit.
            glm::vec4 ambient(0.0f, 0.0f, 0.0f, -1.0f);
            if (settings.bLit) {
                FLightProbeSample sample;
                // The irradiance averaged over every direction (a sprite faces no way in particular):
                // the constant term of the probe's harmonics.
                const glm::vec3 irradiance =
                    InWorld.SampleLightProbes(emitter.Center, sample)
                        ? glm::max(glm::vec3(sample.Red.w, sample.Green.w, sample.Blue.w), glm::vec3(0.0f))
                        : InSkyIrradiance;
                // Held to what a half-float target can add up without overflowing.
                const float share = std::isfinite(settings.LightFraction) ? std::clamp(settings.LightFraction, 0.0f, 100.0f) : 0.0f;
                ambient = glm::vec4(glm::clamp(irradiance, glm::vec3(0.0f), glm::vec3(1.0e4f)), share);
            }

            order.resize(particles.size());
            depths.resize(particles.size());
            std::iota(order.begin(), order.end(), 0u);
            for (size_t index = 0; index < particles.size(); ++index)
                depths[index] = glm::dot(particles[index].Position - InCameraPosition, InForward);
            std::stable_sort(order.begin(), order.end(),
                             [&depths](uint32_t InA, uint32_t InB) { return depths[InA] > depths[InB]; });

            FSpriteDraw draw;
            draw.Settings = &settings;
            draw.FirstVertex = static_cast<uint32_t>(Vertices.size());
            const float fade = std::isfinite(settings.DepthFade) ? std::clamp(settings.DepthFade, 0.0f, 1.0e3f) : 0.0f;
            for (const uint32_t index : order) {
                if (ParticleCount >= static_cast<uint32_t>(kMaxSpriteParticlesPerFrame))
                    break;
                const FSpriteParticle& particle = particles[index];
                const float life = SpriteLifeFraction(particle);
                const glm::vec4 color = glm::clamp(SpriteColorOverLife(settings, life), glm::vec4(0.0f), glm::vec4(1.0e4f));
                const float half = std::min(SpriteSizeOverLife(settings, life), 1.0e3f) * 0.5f;
                // Behind the camera, nothing to see, or not a number.
                const bool bNumbers = std::isfinite(color.r + color.g + color.b + color.a + half + particle.Rotation) &&
                                      std::isfinite(particle.Position.x + particle.Position.y + particle.Position.z);
                if (!bNumbers || depths[index] < -half || color.a <= 0.0f || half <= 0.0f)
                    continue;
                const float c = std::cos(particle.Rotation);
                const float s = std::sin(particle.Rotation);
                const glm::vec3 right = (InRight * c + InUp * s) * half;
                const glm::vec3 up = (InUp * c - InRight * s) * half;
                const glm::vec3 corners[4] = {particle.Position - right + up, particle.Position + right + up,
                                              particle.Position + right - up, particle.Position - right - up};
                const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
                for (const int corner : {0, 1, 2, 0, 2, 3}) {
                    FSpriteVertex vertex;
                    vertex.Position = corners[corner];
                    vertex.UVFade = glm::vec3(uvs[corner], fade);
                    vertex.Color = color;
                    vertex.Ambient = ambient;
                    Vertices.push_back(vertex);
                }
                ++ParticleCount;
            }
            draw.VertexCount = static_cast<uint32_t>(Vertices.size()) - draw.FirstVertex;
            if (draw.VertexCount > 0)
                Draws.push_back(draw);
        }
    }

} // namespace Leon
