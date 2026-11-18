#pragma once

#include "Gameplay/ACharacter.hpp"
#include "Gameplay/TWeakObjectPtr.hpp"
#include "Gameplay/UHealthComponent.hpp"

#include "LeonHorror/FLeonHorrorInputState.hpp"

#include <optional>

namespace Leon {

    class ALeonHorrorFlashlight;
    class ALeonHorrorInteractable;
    class ULeonHorrorAnimInstance;
    class ULeonHorrorCombatComponent;
    class ULeonHorrorInventoryComponent;

    /** Movement and camera numbers for the player, in metres and seconds. */
    struct FLeonHorrorPlayerTuning {
        float WalkSpeed = 2.3f;
        float SprintSpeed = 4.6f;
        float AimSpeed = 1.3f;
        float StaminaMax = 100.0f;
        float StaminaDrainPerSecond = 24.0f;
        float StaminaRegenPerSecond = 16.0f;
        /** Seconds without sprinting before stamina starts to come back. */
        float StaminaRegenDelay = 1.0f;
        /** After running dry, sprinting is locked until stamina climbs back to this. */
        float StaminaToSprintAgain = 25.0f;

        float ArmLength = 2.1f;
        glm::vec3 ShoulderOffset{0.55f, 0.15f, 0.0f};
        /** How far the player can reach to use something, from the eye, in metres. */
        float InteractReach = 2.4f;
        /** Half-angle of the cone around the view direction in which things can be targeted. */
        float InteractConeDegrees = 28.0f;

        float AimArmLength = 1.2f;
        glm::vec3 AimShoulderOffset{0.62f, 0.1f, 0.0f};
        /** How fast the camera rig moves between the two (1/seconds). */
        float CameraBlendRate = 10.0f;
    };

    /**
     * The player: slow third-person character seen over the right shoulder, body always facing where
     * the camera looks (so sideways input strafes), with a flashlight, limited sprint and health.
     */
    class ALeonHorrorCharacter : public ACharacter {
    public:
        ALeonHorrorCharacter() = default;
        ALeonHorrorCharacter(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "LeonHorrorCharacter");

        void PostInitializeComponents() override;
        void BeginPlay() override;
        void EndPlay() override;
        void Tick(float DeltaSeconds) override;
        void SetupPlayerInputComponent(float DeltaSeconds) override;
        void UpdateAnimInstance(UAnimInstance& InAnim) const override;

        /** Acts on one frame of intent. Hardware input and tests both end up here. */
        void ApplyInputState(const FLeonHorrorInputState& InState, float DeltaSeconds);
        /** Replaces hardware sampling with a fixed state (tests, cutscenes). Empty restores hardware. */
        void SetInjectedInput(const std::optional<FLeonHorrorInputState>& InState) { InjectedInput = InState; }

        UHealthComponent* GetHealth() const { return Health.get(); }
        bool IsDead() const { return bDead; }

        float GetStamina() const { return Stamina; }
        void SetStamina(float InStamina);
        bool IsSprinting() const { return bSprinting; }
        bool IsAiming() const { return bAiming; }

        ALeonHorrorFlashlight* GetFlashlight() const { return Flashlight.Get(); }
        bool IsFlashlightOn() const;

        ULeonHorrorAnimInstance* GetHorrorAnim() const { return Anim.get(); }

        ULeonHorrorInventoryComponent* GetInventory() const { return Inventory.get(); }
        ULeonHorrorCombatComponent* GetCombat() const { return Combat.get(); }

        /** The thing the interact key would act on right now, or null. Refreshed every tick. */
        ALeonHorrorInteractable* GetFocusedInteractable() const { return Focused.Get(); }
        /** Prompt for the HUD; empty when nothing is in focus. */
        std::string GetInteractPrompt() const;
        /** Picks the best target for the current view. Public so tests can query without ticking. */
        ALeonHorrorInteractable* FindInteractable() const;
        /** Uses the focused interactable. Returns true when something happened. */
        bool TryInteract();

        FLeonHorrorPlayerTuning Tuning;

    protected:
        bool ShouldApplyControlYawToActor() const override { return !bDead; }

    private:
        void SetupMesh();
        void UpdateStamina(bool bInWantsSprint, bool bInMoving, float DeltaSeconds);
        void UpdateCameraRig(float DeltaSeconds);
        void UpdateFlashlight();
        void HandleDeath(const FDamageInfo& InInfo);

        TRef<UHealthComponent> Health;
        TRef<ULeonHorrorAnimInstance> Anim;
        TRef<ULeonHorrorInventoryComponent> Inventory;
        TRef<ULeonHorrorCombatComponent> Combat;
        TWeakObjectPtr<ALeonHorrorInteractable> Focused;
        TWeakObjectPtr<ALeonHorrorFlashlight> Flashlight;
        FLeonHorrorInputSampler Sampler;
        std::optional<FLeonHorrorInputState> InjectedInput;

        float Stamina = 100.0f;
        float SecondsSinceSprint = 100.0f;
        bool bSprinting = false;
        bool bExhausted = false;
        bool bAiming = false;
        bool bDead = false;
    };

} // namespace Leon
