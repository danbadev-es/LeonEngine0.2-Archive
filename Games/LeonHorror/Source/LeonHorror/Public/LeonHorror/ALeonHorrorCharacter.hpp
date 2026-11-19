#pragma once

#include "Gameplay/ACharacter.hpp"
#include "Gameplay/TWeakObjectPtr.hpp"
#include "Gameplay/UHealthComponent.hpp"

#include "LeonHorror/FLeonHorrorInputState.hpp"

#include <optional>

namespace Leon {

    class ALeonHorrorFlashlight;
    class ULeonHorrorAnimInstance;
    class ULeonHorrorCombatComponent;
    class ULeonHorrorInventoryComponent;

    /** Movement and camera numbers for the player, in metres and seconds. */
    struct FLeonHorrorPlayerTuning {
        float WalkSpeed = 2.6f;
        float SprintSpeed = 5.0f;
        float AimSpeed = 1.4f;
        /** How fast the body turns toward where it is walking, degrees per second. */
        float TurnRateDegrees = 540.0f;
        /** After attacking or using something the body keeps facing the view this long. */
        float FaceViewSeconds = 0.8f;
        float StaminaMax = 100.0f;
        float StaminaDrainPerSecond = 24.0f;
        float StaminaRegenPerSecond = 16.0f;
        /** Seconds without sprinting before stamina starts to come back. */
        float StaminaRegenDelay = 1.0f;
        /** After running dry, sprinting is locked until stamina climbs back to this. */
        float StaminaToSprintAgain = 25.0f;

        float ArmLength = 1.9f;
        glm::vec3 ShoulderOffset{0.62f, 0.02f, 0.0f};
        /** How far the player can reach to use something, from the eye, in metres. */
        float InteractReach = 2.4f;
        /** How far inside a surface something may lie and still be reached (things rest on furniture). */
        float InteractRestingSlack = 0.12f;
        /** Half-angle of the cone around the view direction in which things can be targeted. */
        float InteractConeDegrees = 28.0f;

        float AimArmLength = 1.15f;
        glm::vec3 AimShoulderOffset{0.58f, 0.0f, 0.0f};
        /** Metres per second the camera regains after a wall pushed it in. */
        float CameraRecoverySpeed = 2.5f;
        /** How fast the camera rig moves between the two (1/seconds). */
        float CameraBlendRate = 10.0f;
    };

    /**
     * The player: slow third-person character seen over the right shoulder, with a flashlight, limited
     * sprint and health. The camera orbits freely and the body turns toward where it walks; while
     * aiming, and briefly after attacking or using something, the body faces the view and sideways
     * input strafes.
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
        /** Where the body faces, as a control yaw in degrees. */
        float GetBodyYaw() const { return BodyYaw; }

        ALeonHorrorFlashlight* GetFlashlight() const { return Flashlight.Get(); }
        bool IsFlashlightOn() const;

        ULeonHorrorAnimInstance* GetHorrorAnim() const { return Anim.get(); }

        ULeonHorrorInventoryComponent* GetInventory() const { return Inventory.get(); }
        ULeonHorrorCombatComponent* GetCombat() const { return Combat.get(); }

        /** The actor the interact key would act on right now, or null. Refreshed every tick. */
        AActor* GetFocusedInteractable() const { return Focused.Get(); }
        /** Prompt for the HUD; empty when nothing is in focus. */
        std::string GetInteractPrompt() const;
        /**
         * Picks the best target for the current view among the actors that implement IInteractable.
         * Public so tests can query without ticking.
         */
        AActor* FindInteractable() const;
        /** Uses the focused interactable. Returns true when something happened. */
        bool TryInteract();

        FLeonHorrorPlayerTuning Tuning;

    protected:
        /** True while the body is locked to the view: aiming, or just after an action. */
        bool ShouldApplyControlYawToActor() const override { return !bDead && (bAiming || FaceViewRemaining > 0.0f); }

    private:
        void SetupMesh();
        void UpdateStamina(bool bInWantsSprint, bool bInMoving, float DeltaSeconds);
        void UpdateCameraRig(float DeltaSeconds);
        void UpdateBodyFacing(float DeltaSeconds);
        void UpdateFlashlight();
        void HandleDeath(const FDamageInfo& InInfo);

        TRef<UHealthComponent> Health;
        TRef<ULeonHorrorAnimInstance> Anim;
        TRef<ULeonHorrorInventoryComponent> Inventory;
        TRef<ULeonHorrorCombatComponent> Combat;
        TWeakObjectPtr<AActor> Focused;
        TWeakObjectPtr<ALeonHorrorFlashlight> Flashlight;
        FLeonHorrorInputSampler Sampler;
        std::optional<FLeonHorrorInputState> InjectedInput;

        float Stamina = 100.0f;
        float SecondsSinceSprint = 100.0f;
        bool bSprinting = false;
        bool bExhausted = false;
        bool bAiming = false;
        bool bDead = false;

        /** Facing of the body in the control-yaw convention (-90 looks down -Z). */
        float BodyYaw = -90.0f;
        bool bBodyYawInitialised = false;
        float FaceViewRemaining = 0.0f;
        /** Planar direction the player asked to move in this frame; zero when idle. */
        glm::vec3 WishDirection{0.0f};
    };

} // namespace Leon
