#include <doctest/doctest.h>

#include "Engine/FParticleSimulation.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Renderer/FSpriteParticleBatch.hpp"
#include "Scene/FParticleTypes.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>

// Sprite particles on the CPU: what an emitter spawns and how its particles move (FParticleSimulation),
// their colour, alpha and size over their life (FParticleTypes.hpp), and the order they are drawn in
// (FSpriteParticleBatch).

namespace Leon {

    namespace {

        constexpr float kStep = 1.0f / 60.0f;

        FSpriteEmitterSettings StillSettings() {
            FSpriteEmitterSettings settings;
            settings.SpawnRate = 0.0f;
            settings.Burst = 0;
            settings.Lifetime = 100.0f;
            settings.LifetimeSpread = 0.0f;
            settings.SpawnExtent = glm::vec3(0.0f);
            return settings;
        }

        void Advance(FSpriteEmitterComponent& InOutEmitter, float InSeconds, const glm::mat4& InWorld = glm::mat4(1.0f)) {
            const int steps = static_cast<int>(std::lround(InSeconds / kStep));
            for (int step = 0; step < steps; ++step)
                TickSpriteEmitter(InOutEmitter, InWorld, kStep, 7);
        }

        bool Finite(const glm::vec3& InValue) {
            return std::isfinite(InValue.x) && std::isfinite(InValue.y) && std::isfinite(InValue.z);
        }

    } // namespace

    TEST_SUITE("Sprite particles") {

        TEST_CASE("an emitter releases its burst, then spawns at its rate up to its maximum") {
            FSpriteEmitterSettings settings = StillSettings();
            settings.Burst = 5;
            settings.SpawnRate = 10.0f;
            FSpriteEmitterComponent emitter(settings);
            TickSpriteEmitter(emitter, glm::mat4(1.0f), 0.0f, 1);
            CHECK(emitter.Particles.size() == 5);
            Advance(emitter, 1.0f);
            CHECK(emitter.Particles.size() >= 14);
            CHECK(emitter.Particles.size() <= 16);

            emitter.Settings.MaxParticles = 12;
            Advance(emitter, 1.0f);
            CHECK(emitter.Particles.size() == 12);
            // Full, the rate is not stored up: freed room fills at the rate again, not all at once.
            emitter.Particles.resize(2);
            TickSpriteEmitter(emitter, glm::mat4(1.0f), kStep, 1);
            CHECK(emitter.Particles.size() <= 3);

            // Switched off: nothing; on again: a fresh start, with its burst.
            emitter.Settings.bEnabled = false;
            TickSpriteEmitter(emitter, glm::mat4(1.0f), kStep, 1);
            CHECK(emitter.Particles.empty());
            emitter.Settings.bEnabled = true;
            TickSpriteEmitter(emitter, glm::mat4(1.0f), 0.0f, 1);
            CHECK(emitter.Particles.size() == 5);
        }

        TEST_CASE("particles live their lifetime and move under gravity and drag") {
            FSpriteEmitterSettings settings = StillSettings();
            settings.Burst = 1;
            settings.Lifetime = 2.0f;
            settings.Velocity = {1.0f, 0.0f, 0.0f};
            settings.Gravity = {0.0f, -2.0f, 0.0f};
            settings.Drag = 0.5f;
            FSpriteEmitterComponent emitter(settings);
            TickSpriteEmitter(emitter, glm::mat4(1.0f), 0.0f, 1);
            Advance(emitter, 1.0f);
            REQUIRE(emitter.Particles.size() == 1);
            // dv/dt = g - k v: v(t) = g / k + (v0 - g / k) e^(-k t).
            const glm::vec3 g = settings.Gravity;
            const float k = settings.Drag;
            const glm::vec3 expected = g / k + (settings.Velocity - g / k) * std::exp(-k * 1.0f);
            const glm::vec3 velocity = emitter.Particles[0].Velocity;
            CHECK(velocity.x == doctest::Approx(expected.x).epsilon(0.02));
            CHECK(velocity.y == doctest::Approx(expected.y).epsilon(0.02));
            CHECK(emitter.Particles[0].Position.x > 0.7f);
            CHECK(emitter.Particles[0].Position.y < -0.7f);
            CHECK(emitter.Particles[0].Age == doctest::Approx(1.0f).epsilon(0.01));
            Advance(emitter, 1.05f);
            CHECK(emitter.Particles.empty());
        }

        TEST_CASE("particles appear inside the spawn volume: a box turned with the actor, or a ball") {
            const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, -2.0f)),
                                                glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            FSpriteEmitterSettings settings = StillSettings();
            settings.Burst = 400;
            settings.MaxParticles = 400;
            settings.SpawnExtent = {1.0f, 0.2f, 0.5f};
            FSpriteEmitterComponent box(settings);
            TickSpriteEmitter(box, world, 0.0f, 3);
            REQUIRE(box.Particles.size() == 400);
            glm::vec3 reached(0.0f);
            for (const FSpriteParticle& particle : box.Particles) {
                const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(particle.Position, 1.0f));
                CHECK(std::abs(local.x) <= 1.0f + 1e-4f);
                CHECK(std::abs(local.y) <= 0.2f + 1e-4f);
                CHECK(std::abs(local.z) <= 0.5f + 1e-4f);
                reached = glm::max(reached, glm::abs(local));
            }
            // And it fills it, not a corner of it.
            CHECK(reached.x > 0.9f);
            CHECK(reached.z > 0.45f);

            settings.SpawnShape = ESpriteSpawnShape::Sphere;
            settings.SpawnExtent = {0.7f, 0.0f, 0.0f};
            FSpriteEmitterComponent ball(settings);
            TickSpriteEmitter(ball, world, 0.0f, 3);
            float farthest = 0.0f;
            for (const FSpriteParticle& particle : ball.Particles) {
                const float distance = glm::length(particle.Position - glm::vec3(3.0f, 1.0f, -2.0f));
                CHECK(distance <= 0.7f + 1e-4f);
                farthest = std::max(farthest, distance);
            }
            CHECK(farthest > 0.6f);
        }

        TEST_CASE("colour, alpha and size over life: mixed from start to end, alpha faded in and out") {
            FSpriteEmitterSettings settings;
            settings.ColorStart = {1.0f, 0.0f, 0.0f, 1.0f};
            settings.ColorEnd = {0.0f, 0.0f, 1.0f, 0.5f};
            settings.FadeIn = 0.2f;
            settings.FadeOut = 0.3f;
            settings.SizeStart = 0.1f;
            settings.SizeEnd = 0.5f;
            CHECK(SpriteColorOverLife(settings, 0.0f).a == 0.0f);
            CHECK(SpriteColorOverLife(settings, 0.1f).a == doctest::Approx(0.5f * 0.95f));
            const glm::vec4 middle = SpriteColorOverLife(settings, 0.5f);
            CHECK(middle.r == doctest::Approx(0.5f));
            CHECK(middle.b == doctest::Approx(0.5f));
            CHECK(middle.a == doctest::Approx(0.75f));
            CHECK(SpriteColorOverLife(settings, 0.85f).a == doctest::Approx(0.575f * 0.5f));
            CHECK(SpriteColorOverLife(settings, 1.0f).a == 0.0f);
            CHECK(SpriteSizeOverLife(settings, 0.25f) == doctest::Approx(0.2f));
            // No fading asked for: the alpha is the colours' own.
            settings.FadeIn = settings.FadeOut = 0.0f;
            CHECK(SpriteColorOverLife(settings, 0.0f).a == 1.0f);
            CHECK(SpriteColorOverLife(settings, 1.0f).a == 0.5f);
        }

        TEST_CASE("nothing that is not a number comes out of an emitter, whatever it is given") {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float inf = std::numeric_limits<float>::infinity();
            FSpriteEmitterSettings settings;
            settings.Burst = 20;
            settings.SpawnRate = 1.0e9f;
            settings.MaxParticles = 1 << 30;
            settings.Velocity = {inf, 0.0f, 1.0e30f};
            settings.VelocitySpread = {nan, 1.0f, 1.0f};
            settings.Drag = -5.0f;
            settings.Gravity = {0.0f, -1.0e30f, 0.0f};
            settings.Lifetime = nan;
            settings.LifetimeSpread = 7.0f;
            settings.SpawnExtent = {inf, -1.0f, 1.0f};
            FSpriteEmitterComponent emitter(settings);
            for (const float dt : {0.0f, kStep, nan, -1.0f, inf, 10.0f, kStep})
                TickSpriteEmitter(emitter, glm::mat4(1.0f), dt, 9);
            CHECK(emitter.Particles.size() <= static_cast<size_t>(kMaxSpriteParticlesPerEmitter));
            for (const FSpriteParticle& particle : emitter.Particles) {
                CHECK(Finite(particle.Position));
                CHECK(Finite(particle.Velocity));
                CHECK(std::isfinite(particle.Age));
            }
            // A fade that is not a number is no fade.
            settings.FadeIn = nan;
            CHECK(std::isfinite(SpriteColorOverLife(settings, 0.5f).a));
        }

        TEST_CASE("the batch draws emitters, and the particles of each, from the farthest to the nearest") {
            TRef<UWorld> world = UWorld::Create("SpriteOrder");
            const auto addEmitter = [&](const char* InName, const glm::vec3& InPosition, float InDepthSpread) {
                AActor* actor = world->SpawnActor<AActor>(InName);
                actor->SetActorLocation(InPosition);
                FSpriteEmitterSettings settings = StillSettings();
                settings.Burst = 30;
                settings.FadeIn = settings.FadeOut = 0.0f;
                settings.SpawnExtent = {0.2f, 0.2f, InDepthSpread};
                auto& emitter = actor->AddComponent<FSpriteEmitterComponent>(settings);
                TickSpriteEmitter(emitter, actor->GetActorWorldMatrix(), 0.0f, 5);
                return &emitter;
            };
            // Made nearest first: the order of the registry is the wrong one.
            const FSpriteEmitterComponent* nearEmitter = addEmitter("Near", {0.0f, 0.0f, -2.0f}, 0.5f);
            const FSpriteEmitterComponent* farEmitter = addEmitter("Far", {0.5f, 0.0f, -8.0f}, 1.0f);
            FSpriteParticleBatch batch;
            batch.Build(*world, glm::vec3(0.0f), {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, glm::vec3(0.3f));
            REQUIRE(batch.Draws.size() == 2);
            CHECK(batch.Draws[0].Settings == &farEmitter->Settings);
            CHECK(batch.Draws[1].Settings == &nearEmitter->Settings);
            CHECK(batch.ParticleCount == 60);
            CHECK(batch.Vertices.size() == 360);
            // Within each, every quad no nearer than the one before it.
            for (const FSpriteDraw& draw : batch.Draws) {
                float previous = std::numeric_limits<float>::max();
                for (uint32_t quad = 0; quad < draw.VertexCount / 6; ++quad) {
                    glm::vec3 center(0.0f);
                    for (uint32_t corner = 0; corner < 6; ++corner)
                        center += batch.Vertices[draw.FirstVertex + quad * 6 + corner].Position;
                    const float depth = -(center / 6.0f).z;
                    CHECK(depth <= previous + 1e-4f);
                    previous = depth;
                }
            }
            // Unlit, a sprite says so; lit, it carries the sky's light where there are no probes.
            CHECK(batch.Vertices[0].Ambient == glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
        }
    }

} // namespace Leon
