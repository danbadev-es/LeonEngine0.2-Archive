#include "LeonHorror/ALeonHorrorCharacter.hpp"

#include "LeonHorror/ALeonHorrorFlashlight.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorAnimInstance.hpp"

#include "Assets/USkeletalMesh.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/UCharacterMovementComponent.hpp"
#include "Gameplay/USkeletalMeshComponent.hpp"
#include "Gameplay/USpringArmComponent.hpp"

#include <algorithm>
#include <cmath>

namespace Leon {

    namespace {
        constexpr const char* kPlayerMeshPath = "/Game/SkeletalMeshes/YBot.lskeletalmesh";
        constexpr float kPlayerHeightMetres = 1.78f;
    } // namespace

    ALeonHorrorCharacter::ALeonHorrorCharacter(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ACharacter(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorCharacter");
    }

    void ALeonHorrorCharacter::PostInitializeComponents() {
        ACharacter::PostInitializeComponents();

        if (!Health) {
            Health = AddActorComponent<UHealthComponent>("Health");
            Health->SetMaxHealth(100.0f);
            Health->SetHealth(100.0f);
        }

        if (auto movement = GetCharacterMovement()) {
            movement->SetMaxWalkSpeed(Tuning.WalkSpeed);
            movement->SetJumpMaxCount(1); // no jump input is bound; this only disables the double jump
        }

        SetThirdPerson(true);
        if (auto arm = GetSpringArm()) {
            arm->TargetArmLength = Tuning.ArmLength;
            arm->SocketOffset = Tuning.ShoulderOffset;
        }

        Stamina = Tuning.StaminaMax;
        SetupMesh();
    }

    void ALeonHorrorCharacter::SetupMesh() {
        auto mesh = GetMesh();
        if (!mesh)
            return;
        if (!Anim)
            Anim = MakeRef<ULeonHorrorAnimInstance>("PlayerAnim", ELeonHorrorAnimProfile::Player);
        mesh->SetMeshAssetPath(kPlayerMeshPath);
        mesh->SetAnimInstance(Anim);

        // Imported meshes come at arbitrary scale: normalise to a design height and stand the soles
        // on the bottom of the capsule.
        float scale = 1.0f;
        float feetY = 0.0f;
        if (auto asset = mesh->GetSkeletalMesh()) {
            const float meshHeight = asset->GetBoundsMax().y - asset->GetBoundsMin().y;
            if (meshHeight > 1e-3f)
                scale = kPlayerHeightMetres / meshHeight;
            feetY = asset->GetBoundsMin().y;
        }
        mesh->SetRelativeScale3D(glm::vec3(scale));
        mesh->SetRelativeLocation(glm::vec3(0.0f, -GetCapsuleHalfHeight() - feetY * scale, 0.0f));
    }

    void ALeonHorrorCharacter::BeginPlay() {
        ACharacter::BeginPlay();

        if (Health) {
            Health->OnDeath.push_back([this](const FDamageInfo& InInfo) { HandleDeath(InInfo); });
        }

        if (World && !Flashlight.IsValid()) {
            auto* light = World->SpawnActor<ALeonHorrorFlashlight>(GetName() + "_Flashlight");
            Flashlight = light;
            UpdateFlashlight();
        }

        // Look comes from raw mouse motion: capture the cursor.
        if (auto* pc = dynamic_cast<APlayerController*>(GetController()))
            pc->SetInputModeGameOnly();
    }

    void ALeonHorrorCharacter::EndPlay() {
        if (ALeonHorrorFlashlight* light = Flashlight.Get())
            light->Destroy();
        Flashlight = nullptr;
        ACharacter::EndPlay();
    }

    bool ALeonHorrorCharacter::IsFlashlightOn() const {
        const ALeonHorrorFlashlight* light = Flashlight.Get();
        return light && light->IsOn();
    }

    void ALeonHorrorCharacter::SetupPlayerInputComponent(float DeltaSeconds) {
        if (InjectedInput) {
            ApplyInputState(*InjectedInput, DeltaSeconds);
            return;
        }
        if (auto* pc = dynamic_cast<APlayerController*>(GetController())) {
            if (!pc->IsGameInputAllowed())
                return;
        }
        // Mouse and right-stick look go through the engine helper (it owns the mouse delta state);
        // everything else is sampled into an input state.
        ApplyLookInput(DeltaSeconds, false);
        ApplyInputState(Sampler.Sample(), DeltaSeconds);
    }

    void ALeonHorrorCharacter::ApplyInputState(const FLeonHorrorInputState& InState, float DeltaSeconds) {
        if (std::abs(InState.LookDelta.x) > 0.0f)
            SetControlYaw(GetControlYaw() + InState.LookDelta.x);
        if (std::abs(InState.LookDelta.y) > 0.0f)
            SetControlPitch(GetControlPitch() + InState.LookDelta.y);

        if (bDead) {
            bSprinting = false;
            bAiming = false;
            return;
        }

        if (InState.bFlashlightPressed) {
            if (ALeonHorrorFlashlight* light = Flashlight.Get())
                light->Toggle();
        }

        bAiming = InState.bAimHeld;

        glm::vec2 move = InState.Move;
        const float moveLength = glm::length(move);
        if (moveLength > 1.0f)
            move /= moveLength;
        const bool bMoving = moveLength > 0.05f;

        // Sprint only forward-ish, never while aiming, and not while winded.
        const bool bWantsSprint = InState.bSprintHeld && bMoving && move.y > 0.3f && !bAiming;
        UpdateStamina(bWantsSprint, bMoving, DeltaSeconds);

        float speed = Tuning.WalkSpeed;
        if (bAiming)
            speed = Tuning.AimSpeed;
        else if (bSprinting)
            speed = Tuning.SprintSpeed;

        if (auto movement = GetCharacterMovement()) {
            movement->SetMaxWalkSpeed(speed);
            if (bMoving) {
                const glm::vec3 forward = GetControlPlanarForward();
                const glm::vec3 right(-forward.z, 0.0f, forward.x);
                // The movement component treats the input vector's length as the wished speed.
                movement->AddInputVector((forward * move.y + right * move.x) * speed);
            }
        }
    }

    void ALeonHorrorCharacter::UpdateStamina(bool bInWantsSprint, bool bInMoving, float DeltaSeconds) {
        (void)bInMoving;
        if (bExhausted && Stamina >= Tuning.StaminaToSprintAgain)
            bExhausted = false;

        bSprinting = bInWantsSprint && !bExhausted && Stamina > 0.0f;
        if (bSprinting) {
            Stamina = std::max(0.0f, Stamina - Tuning.StaminaDrainPerSecond * DeltaSeconds);
            SecondsSinceSprint = 0.0f;
            if (Stamina <= 0.0f) {
                bExhausted = true;
                bSprinting = false;
            }
            return;
        }

        SecondsSinceSprint += DeltaSeconds;
        if (SecondsSinceSprint >= Tuning.StaminaRegenDelay)
            Stamina = std::min(Tuning.StaminaMax, Stamina + Tuning.StaminaRegenPerSecond * DeltaSeconds);
    }

    void ALeonHorrorCharacter::Tick(float DeltaSeconds) {
        ACharacter::Tick(DeltaSeconds);
        UpdateCameraRig(DeltaSeconds);
        UpdateFlashlight();
        if (Anim)
            Anim->SetAiming(bAiming && !bDead);
    }

    void ALeonHorrorCharacter::UpdateCameraRig(float DeltaSeconds) {
        auto arm = GetSpringArm();
        if (!arm)
            return;
        const float targetLength = bAiming ? Tuning.AimArmLength : Tuning.ArmLength;
        const glm::vec3 targetOffset = bAiming ? Tuning.AimShoulderOffset : Tuning.ShoulderOffset;
        const float alpha = std::clamp(Tuning.CameraBlendRate * DeltaSeconds, 0.0f, 1.0f);
        arm->TargetArmLength += (targetLength - arm->TargetArmLength) * alpha;
        arm->SocketOffset += (targetOffset - arm->SocketOffset) * alpha;
    }

    void ALeonHorrorCharacter::UpdateFlashlight() {
        ALeonHorrorFlashlight* light = Flashlight.Get();
        if (!light)
            return;
        light->UpdateFromView(GetPawnViewLocation(), GetControlLookDirection());
    }

    void ALeonHorrorCharacter::UpdateAnimInstance(UAnimInstance& InAnim) const {
        ACharacter::UpdateAnimInstance(InAnim);
        InAnim.SetBool("bIsDead", bDead);
    }

    void ALeonHorrorCharacter::HandleDeath(const FDamageInfo& InInfo) {
        (void)InInfo;
        if (bDead)
            return;
        bDead = true;
        bSprinting = false;
        bAiming = false;
        if (auto movement = GetCharacterMovement()) {
            movement->StopMovementImmediately();
            movement->SetMovementMode(EMovementMode::None);
        }
        if (auto* gameMode = World ? dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode()) : nullptr)
            gameMode->NotifyPlayerDied(*this);
    }

} // namespace Leon
