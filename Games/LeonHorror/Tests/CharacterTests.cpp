#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorFlashlight.hpp"
#include "LeonHorror/ULeonHorrorAnimInstance.hpp"

#include "Scene/Components.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/UCharacterMovementComponent.hpp"
#include "Gameplay/USpringArmComponent.hpp"

namespace Leon {

    namespace {

        struct FPlayerFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;

            FPlayerFixture() {
                Player = GM->GetPlayerCharacter();
                REQUIRE(Player);
                // Deterministic start: on the fallback floor plane, looking down -Z.
                Player->SetControlRotation({0.0f, -90.0f, 0.0f});
            }

            /** Holds the given input for InSeconds and returns the planar distance travelled. */
            float Drive(const FLeonHorrorInputState& InState, float InSeconds) {
                const glm::vec3 start = Player->GetActorLocation();
                Player->SetInjectedInput(InState);
                TickSeconds(InSeconds);
                glm::vec3 delta = Player->GetActorLocation() - start;
                delta.y = 0.0f;
                return glm::length(delta);
            }
        };

        FLeonHorrorInputState Forward() {
            FLeonHorrorInputState state;
            state.Move = {0.0f, 1.0f};
            return state;
        }

    } // namespace

    TEST_SUITE("LeonHorror player") {

        TEST_CASE("the game mode spawns the horror character in third person with full health and stamina") {
            FPlayerFixture f;
            CHECK(f.Player->GetClass() == "ALeonHorrorCharacter");
            CHECK(f.Player->IsThirdPerson());
            REQUIRE(f.Player->GetHealth());
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(100.0f));
            CHECK(f.Player->GetStamina() == doctest::Approx(f.Player->Tuning.StaminaMax));
            CHECK_FALSE(f.Player->IsDead());
            REQUIRE(f.Player->GetHorrorAnim());
            // Over-the-shoulder rig: short arm, offset to the right.
            CHECK(f.Player->GetSpringArm()->TargetArmLength == doctest::Approx(f.Player->Tuning.ArmLength));
            CHECK(f.Player->GetSpringArm()->SocketOffset.x > 0.3f);
        }

        TEST_CASE("forward input walks along the view direction at walking speed") {
            FPlayerFixture f;
            const glm::vec3 start = f.Player->GetActorLocation();
            const float distance = f.Drive(Forward(), 2.0f);
            const float expected = f.Player->Tuning.WalkSpeed * 2.0f;
            CHECK(distance > expected * 0.75f);
            CHECK(distance < expected * 1.1f);
            // Yaw -90 looks down -Z.
            const glm::vec3 delta = f.Player->GetActorLocation() - start;
            CHECK(delta.z < -1.0f);
            CHECK(std::abs(delta.x) < 0.2f);
            CHECK_FALSE(f.Player->IsSprinting());
        }

        TEST_CASE("while aiming, sideways input strafes: the body keeps facing the view direction") {
            FPlayerFixture f;
            FLeonHorrorInputState aim;
            aim.bAimHeld = true;
            f.Drive(aim, 0.1f); // let the body align with the view before measuring
            const glm::vec3 start = f.Player->GetActorLocation();
            const glm::vec3 rotationBefore = f.Player->GetActorRotation();
            FLeonHorrorInputState right = aim;
            right.Move = {1.0f, 0.0f};
            f.Drive(right, 1.5f);
            const glm::vec3 delta = f.Player->GetActorLocation() - start;
            CHECK(delta.x > 1.0f); // facing -Z, right is +X
            CHECK(std::abs(delta.z) < 0.2f);
            CHECK(f.Player->GetActorRotation().y == doctest::Approx(rotationBefore.y));
            CHECK(f.Player->GetBodyYaw() == doctest::Approx(f.Player->GetControlYaw()));
        }

        TEST_CASE("when not aiming, the body turns to face where it walks and the view stays put") {
            FPlayerFixture f;
            f.TickSeconds(0.05f);
            const float viewYaw = f.Player->GetControlYaw(); // -90: looking down -Z
            const glm::vec3 start = f.Player->GetActorLocation();
            FLeonHorrorInputState right;
            right.Move = {1.0f, 0.0f};
            f.Drive(right, 1.0f);
            const glm::vec3 delta = f.Player->GetActorLocation() - start;
            CHECK(delta.x > 1.0f); // still moves to the right of the view
            CHECK(f.Player->GetControlYaw() == doctest::Approx(viewYaw));
            CHECK(f.Player->GetBodyYaw() == doctest::Approx(0.0f).epsilon(0.01)); // facing +X
            // Standing still keeps the last facing.
            f.Drive(FLeonHorrorInputState{}, 0.5f);
            CHECK(f.Player->GetBodyYaw() == doctest::Approx(0.0f).epsilon(0.01));
            // Aiming snaps the body back to the view.
            FLeonHorrorInputState aim;
            aim.bAimHeld = true;
            f.Drive(aim, 0.1f);
            CHECK(f.Player->GetBodyYaw() == doctest::Approx(viewYaw));
        }

        TEST_CASE("no input means no movement") {
            FPlayerFixture f;
            CHECK(f.Drive(FLeonHorrorInputState{}, 1.0f) < 0.05f);
        }

        TEST_CASE("sprinting is faster, drains stamina and locks out when exhausted") {
            FPlayerFixture f;
            FLeonHorrorInputState sprint = Forward();
            sprint.bSprintHeld = true;

            const float sprinted = f.Drive(sprint, 1.0f);
            CHECK(f.Player->IsSprinting());
            CHECK(sprinted > f.Player->Tuning.WalkSpeed * 1.3f);
            CHECK(f.Player->GetStamina() < f.Player->Tuning.StaminaMax - 15.0f);

            // Run it dry: stamina 100 at 24 per second lasts a little over four seconds.
            f.Drive(sprint, 4.5f);
            CHECK(f.Player->GetStamina() < f.Player->Tuning.StaminaToSprintAgain);
            CHECK_FALSE(f.Player->IsSprinting());

            // Still holding sprint while winded: only walking pace.
            const float winded = f.Drive(sprint, 0.5f);
            CHECK(winded < f.Player->Tuning.WalkSpeed * 0.5f * 1.15f);

            // Resting brings it back and sprint works again.
            f.Drive(FLeonHorrorInputState{}, 4.0f);
            CHECK(f.Player->GetStamina() > f.Player->Tuning.StaminaToSprintAgain);
            f.Drive(sprint, 0.5f);
            CHECK(f.Player->IsSprinting());
        }

        TEST_CASE("stamina does not regenerate during the delay after sprinting") {
            FPlayerFixture f;
            FLeonHorrorInputState sprint = Forward();
            sprint.bSprintHeld = true;
            f.Drive(sprint, 1.0f);
            const float afterSprint = f.Player->GetStamina();
            f.Drive(FLeonHorrorInputState{}, f.Player->Tuning.StaminaRegenDelay * 0.5f);
            CHECK(f.Player->GetStamina() == doctest::Approx(afterSprint));
            f.Drive(FLeonHorrorInputState{}, f.Player->Tuning.StaminaRegenDelay + 0.5f);
            CHECK(f.Player->GetStamina() > afterSprint);
        }

        TEST_CASE("sprint needs forward movement and is not available while aiming") {
            FPlayerFixture f;
            FLeonHorrorInputState back;
            back.Move = {0.0f, -1.0f};
            back.bSprintHeld = true;
            f.Drive(back, 0.5f);
            CHECK_FALSE(f.Player->IsSprinting());

            FLeonHorrorInputState aimSprint = Forward();
            aimSprint.bSprintHeld = true;
            aimSprint.bAimHeld = true;
            const float distance = f.Drive(aimSprint, 1.0f);
            CHECK_FALSE(f.Player->IsSprinting());
            CHECK(f.Player->IsAiming());
            CHECK(distance < f.Player->Tuning.AimSpeed * 1.15f);
        }

        TEST_CASE("aiming pulls the camera in over the shoulder and releasing restores it") {
            FPlayerFixture f;
            FLeonHorrorInputState aim;
            aim.bAimHeld = true;
            f.Drive(aim, 1.0f);
            CHECK(f.Player->GetSpringArm()->TargetArmLength ==
                  doctest::Approx(f.Player->Tuning.AimArmLength).epsilon(0.02));
            CHECK(f.Player->GetHorrorAnim()->IsAiming());

            f.Drive(FLeonHorrorInputState{}, 1.0f);
            CHECK(f.Player->GetSpringArm()->TargetArmLength == doctest::Approx(f.Player->Tuning.ArmLength).epsilon(0.02));
            CHECK_FALSE(f.Player->GetHorrorAnim()->IsAiming());
        }

        TEST_CASE("look input turns the view and clamps the pitch") {
            FPlayerFixture f;
            FLeonHorrorInputState look;
            look.LookDelta = {30.0f, 20.0f};
            f.Player->ApplyInputState(look, 1.0f / 60.0f);
            CHECK(f.Player->GetControlYaw() == doctest::Approx(-60.0f));
            CHECK(f.Player->GetControlPitch() == doctest::Approx(20.0f));
            look.LookDelta = {0.0f, 500.0f};
            f.Player->ApplyInputState(look, 1.0f / 60.0f);
            CHECK(f.Player->GetControlPitch() <= 89.0f);
        }

        TEST_CASE("the flashlight follows the view and toggles on a press") {
            FPlayerFixture f;
            ALeonHorrorFlashlight* light = f.Player->GetFlashlight();
            REQUIRE(light);
            REQUIRE(light->HasComponent<FSpotLightComponent>());
            CHECK(f.Player->IsFlashlightOn());

            f.Player->SetControlRotation({-20.0f, 15.0f, 0.0f});
            f.TickSeconds(0.1f);
            const glm::vec3 look = f.Player->GetControlLookDirection();
            CHECK(glm::dot(glm::normalize(light->GetLightDirection()), look) > 0.999f);
            // Carried at the body, not left behind.
            CHECK(glm::length(light->GetLightPosition() - f.Player->GetPawnViewLocation()) < 1.0f); // just ahead of the body

            FLeonHorrorInputState press;
            press.bFlashlightPressed = true;
            f.Player->ApplyInputState(press, 1.0f / 60.0f);
            CHECK_FALSE(f.Player->IsFlashlightOn());
            CHECK_FALSE(light->GetComponent<FSpotLightComponent>().bEnabled);
            f.Player->ApplyInputState(press, 1.0f / 60.0f);
            CHECK(f.Player->IsFlashlightOn());

            // It moves with the player.
            const glm::vec3 before = light->GetLightPosition();
            f.Drive(Forward(), 1.0f);
            CHECK(glm::length(light->GetLightPosition() - before) > 1.0f);
        }

        TEST_CASE("the flashlight is the only spot light the character adds, and it is removed with it") {
            FPlayerFixture f;
            int spots = 0;
            for (const auto& actor : f.World->GetAllActors()) {
                if (actor && actor->HasComponent<FSpotLightComponent>())
                    ++spots;
            }
            CHECK(spots == 1);

            f.World->DestroyActor(f.Player);
            f.TickSeconds(0.05f);
            spots = 0;
            for (const auto& actor : f.World->GetAllActors()) {
                if (actor && actor->HasComponent<FSpotLightComponent>())
                    ++spots;
            }
            CHECK(spots == 0);
        }

        TEST_CASE("death stops the character, ignores movement input and tells the game mode") {
            FPlayerFixture f;
            f.Drive(Forward(), 0.5f);
            CHECK_FALSE(f.GM->HasPlayerDied());

            f.Player->GetHealth()->ApplyDamage(500.0f);
            CHECK(f.Player->IsDead());
            CHECK(f.GM->HasPlayerDied());

            const float moved = f.Drive(Forward(), 1.0f);
            CHECK(moved < 0.05f);
            CHECK_FALSE(f.Player->IsSprinting());
            CHECK(f.Player->GetHorrorAnim()->GetBool("bIsDead"));
        }
    }

} // namespace Leon
