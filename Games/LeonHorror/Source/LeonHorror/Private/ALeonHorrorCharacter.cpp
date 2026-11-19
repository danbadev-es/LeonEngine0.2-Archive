#include "LeonHorror/ALeonHorrorCharacter.hpp"

#include "LeonHorror/FLeonHorrorArchetypes.hpp"
#include "Core/Cast.hpp"
#include "Gameplay/IInteractable.hpp"

#include "LeonHorror/ALeonHorrorFlashlight.hpp"
#include "LeonHorror/ALeonHorrorHeldItem.hpp"
#include "LeonHorror/FLeonHorrorSound.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorAnimInstance.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

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

        if (!Inventory)
            Inventory = AddActorComponent<ULeonHorrorInventoryComponent>("Inventory");
        if (!Combat)
            Combat = AddActorComponent<ULeonHorrorCombatComponent>("Combat");

        if (auto movement = GetCharacterMovement()) {
            movement->SetMaxWalkSpeed(Tuning.WalkSpeed);
            movement->SetJumpMaxCount(1); // no jump input is bound; this only disables the double jump
        }

        SetThirdPerson(true);
        if (auto arm = GetSpringArm()) {
            arm->TargetArmLength = Tuning.ArmLength;
            arm->SocketOffset = Tuning.ShoulderOffset;
            arm->bSmoothRecovery = true;
            arm->RecoverySpeed = Tuning.CameraRecoverySpeed;
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
        mesh->SetMeshAssetPath(LeonHorrorArchetypes::PlayerMeshPath());
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
            Health->OnDeath.Add([this](const FDamageInfo& InInfo) { HandleDeath(InInfo); });
            Health->OnDamage.Add([this](const FDamageInfo&) {
                if (!bDead)
                    LeonHorrorSound::Play(LeonHorrorSound::Variation("SFX_Player_Hurt", 3), 0.9f);
            });
        }

        if (World && !Flashlight.IsValid()) {
            auto* light = World->SpawnActor<ALeonHorrorFlashlight>(GetName() + "_Flashlight");
            Flashlight = light;
            UpdateFlashlight();
        }
        if (World && !HeldLight.IsValid()) {
            auto* light = World->SpawnActor<ALeonHorrorHeldItem>(GetName() + "_HeldLight");
            light->SetMesh(LeonHorrorArchetypes::ExistingAsset("/Game/Meshes/SM_Held_Flashlight.lmesh", ""));
            // Across the palm, a little towards the fingers.
            light->SetGrip({0.0f, 0.085f, 0.03f}, {0.0f, 0.0f, 0.0f});
            HeldLight = light;
            HeldWeapon = World->SpawnActor<ALeonHorrorHeldItem>(GetName() + "_HeldWeapon");
            UpdateHeldItems();
        }

        // Look comes from raw mouse motion: capture the cursor.
        if (auto* pc = dynamic_cast<APlayerController*>(GetController()))
            pc->SetInputModeGameOnly();
    }

    void ALeonHorrorCharacter::EndPlay() {
        if (ALeonHorrorFlashlight* light = Flashlight.Get())
            light->Destroy();
        Flashlight = nullptr;
        if (ALeonHorrorHeldItem* held = HeldLight.Get())
            held->Destroy();
        HeldLight = nullptr;
        if (ALeonHorrorHeldItem* held = HeldWeapon.Get())
            held->Destroy();
        HeldWeapon = nullptr;
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
            if (ALeonHorrorFlashlight* light = Flashlight.Get()) {
                light->Toggle();
                LeonHorrorSound::Play(light->IsOn() ? "SFX_Flashlight_On" : "SFX_Flashlight_Off", 0.7f);
            }
        }

        WishDirection = glm::vec3(0.0f);

        if (InState.bInteractPressed) {
            Focused = FindInteractable();
            if (TryInteract())
                FaceViewRemaining = Tuning.FaceViewSeconds;
        }

        bAiming = InState.bAimHeld;

        if (Combat) {
            if (InState.bSelectMeleePressed)
                Combat->Equip(ELeonHorrorWeapon::Pipe);
            if (InState.bSelectPistolPressed)
                Combat->Equip(ELeonHorrorWeapon::Pistol);
            if (InState.bReloadPressed)
                Combat->StartReload();
            if (InState.bAttackPressed && Combat->StartAttack(bAiming))
                FaceViewRemaining = Tuning.FaceViewSeconds; // strike where the camera looks
        }

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
                WishDirection = forward * move.y + right * move.x;
                // The movement component treats the input vector's length as the wished speed.
                movement->AddInputVector(WishDirection * speed);
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

    void ALeonHorrorCharacter::SetStamina(float InStamina) {
        Stamina = std::clamp(InStamina, 0.0f, Tuning.StaminaMax);
        bExhausted = Stamina < Tuning.StaminaToSprintAgain && Stamina <= 0.0f;
    }

    void ALeonHorrorCharacter::Tick(float DeltaSeconds) {
        ACharacter::Tick(DeltaSeconds);
        UpdateBodyFacing(DeltaSeconds);
        UpdateCameraRig(DeltaSeconds);
        UpdateFlashlight();
        UpdateHeldItems();
        UpdateSounds(DeltaSeconds);
        Focused = bDead ? nullptr : FindInteractable();
        if (Anim)
            Anim->SetAiming(bAiming && !bDead);
    }

    void ALeonHorrorCharacter::UpdateBodyFacing(float DeltaSeconds) {
        if (!bBodyYawInitialised) {
            BodyYaw = GetControlYaw();
            bBodyYawInitialised = true;
        }
        if (bDead)
            return;
        if (ShouldApplyControlYawToActor()) {
            // The engine has already turned the actor to the view this frame.
            BodyYaw = GetControlYaw();
            FaceViewRemaining = std::max(0.0f, FaceViewRemaining - DeltaSeconds);
            return;
        }
        if (glm::dot(WishDirection, WishDirection) > 0.01f) {
            const float target = glm::degrees(std::atan2(WishDirection.z, WishDirection.x));
            float difference = std::fmod(target - BodyYaw + 540.0f, 360.0f) - 180.0f;
            const float step = Tuning.TurnRateDegrees * DeltaSeconds;
            BodyYaw += std::clamp(difference, -step, step);
        }
        // Same mapping the engine uses for the view: a control yaw of -90 faces -Z.
        GetTransform().Rotation = glm::vec3(0.0f, -BodyYaw - 90.0f, 0.0f);
    }

    void ALeonHorrorCharacter::UpdateCameraRig(float DeltaSeconds) {
        auto arm = GetSpringArm();
        if (!arm)
            return;
        arm->AddRecoveryTime(DeltaSeconds);
        const float targetLength = bAiming ? Tuning.AimArmLength : Tuning.ArmLength;
        const glm::vec3 targetOffset = bAiming ? Tuning.AimShoulderOffset : Tuning.ShoulderOffset;
        const float alpha = std::clamp(Tuning.CameraBlendRate * DeltaSeconds, 0.0f, 1.0f);
        arm->TargetArmLength += (targetLength - arm->TargetArmLength) * alpha;
        arm->SocketOffset += (targetOffset - arm->SocketOffset) * alpha;

        // Backed against a wall the camera has nowhere to go but into the player's head. Seeing the
        // room past a body that is not there beats seeing nothing but the inside of a jacket. A little
        // slack between hiding and showing keeps it from flickering at the threshold.
        const float length = arm->GetCurrentArmLength();
        if (bBodyHidden ? length > Tuning.HideBodyArmLength + 0.15f : length < Tuning.HideBodyArmLength)
            bBodyHidden = !bBodyHidden;
        if (auto mesh = GetMesh())
            mesh->SetHiddenInGame(bBodyHidden);
    }

    void ALeonHorrorCharacter::UpdateFlashlight() {
        ALeonHorrorFlashlight* light = Flashlight.Get();
        if (!light)
            return;
        light->UpdateFromView(GetPawnViewLocation(), GetControlLookDirection());
    }

    void ALeonHorrorCharacter::UpdateSounds(float DeltaSeconds) {
        const glm::vec3 location = GetActorLocation();
        glm::vec3 step = location - LastStepLocation;
        step.y = 0.0f;
        const float moved = glm::length(step);
        LastStepLocation = location;
        if (bDead)
            return;

        // A stride at a time; being placed somewhere (a spawn, a loaded save) is not walking.
        if (bStepStarted && moved < 0.5f) {
            StepDistance += moved;
            const float stride = bSprinting ? 1.1f : 0.8f;
            if (StepDistance >= stride && World) {
                StepDistance = 0.0f;
                const glm::vec3 feet = location - glm::vec3(0.0f, GetCapsuleHalfHeight(), 0.0f);
                const std::string surface = LeonHorrorSound::StepSurface(*World, feet, this);
                LeonHorrorSound::PlayAt(LeonHorrorSound::Variation(surface, 6), feet, bSprinting ? 0.6f : 0.38f, 12.0f);
            }
        }
        bStepStarted = true;

        if (bExhausted && !bWasExhausted)
            LeonHorrorSound::Play("SFX_Breath_Tired", 0.7f);
        bWasExhausted = bExhausted;

        // Under a third of a life the heart is heard, faster the less there is left.
        const float fraction = Health && Health->GetMaxHealth() > 0.0f ? Health->GetHealth() / Health->GetMaxHealth() : 1.0f;
        if (fraction < 0.34f) {
            HeartbeatTimer -= DeltaSeconds;
            if (HeartbeatTimer <= 0.0f) {
                HeartbeatTimer = 0.65f + fraction * 1.2f;
                LeonHorrorSound::Play("SFX_Heartbeat", 0.75f);
            }
        } else {
            HeartbeatTimer = 0.0f;
        }
    }

    void ALeonHorrorCharacter::UpdateHeldItems() {
        auto mesh = GetMesh();
        if (!mesh)
            return;
        glm::mat4 hand;
        if (ALeonHorrorHeldItem* light = HeldLight.Get()) {
            // The hand carries it, the wrist points it where the player looks.
            light->SetShown(!bBodyHidden);
            if (mesh->GetBoneMatrix("mixamorig:LeftHand", hand))
                light->FollowBoneAimed(hand, bDead ? GetActorForwardVector() : GetControlLookDirection());
        }
        if (ALeonHorrorHeldItem* weapon = HeldWeapon.Get()) {
            const ELeonHorrorWeapon equipped = Combat ? Combat->GetEquipped() : ELeonHorrorWeapon::None;
            if (equipped == ELeonHorrorWeapon::Pipe) {
                weapon->SetMesh(LeonHorrorArchetypes::ExistingAsset("/Game/Meshes/SM_Held_Pipe.lmesh", ""));
                weapon->SetGrip({0.0f, 0.09f, 0.03f}, {90.0f, 0.0f, 0.0f});
            } else if (equipped == ELeonHorrorWeapon::Pistol) {
                weapon->SetMesh(LeonHorrorArchetypes::ExistingAsset("/Game/Meshes/SM_Held_Pistol.lmesh", ""));
                weapon->SetGrip({0.0f, 0.09f, 0.03f}, {90.0f, 0.0f, 0.0f});
            }
            weapon->SetShown(equipped != ELeonHorrorWeapon::None && !bBodyHidden);
            if (weapon->IsShown() && mesh->GetBoneMatrix("mixamorig:RightHand", hand))
                weapon->FollowBone(hand);
        }
    }

    AActor* ALeonHorrorCharacter::FindInteractable() const {
        if (!World || bDead)
            return nullptr;
        const glm::vec3 eye = GetPawnViewLocation();
        const glm::vec3 look = GetControlLookDirection();
        const float minCos = std::cos(glm::radians(Tuning.InteractConeDegrees));

        AActor* best = nullptr;
        float bestScore = -1.0e9f;
        for (const auto& actorRef : World->GetAllActors()) {
            AActor* actor = actorRef.get();
            const auto* candidate = Cast<IInteractable>(actor);
            if (!candidate || actor == this || !candidate->CanInteract(*this))
                continue;
            const glm::vec3 to = candidate->GetInteractLocation() - eye;
            const float distance = glm::length(to);
            if (distance > Tuning.InteractReach)
                continue;
            // Something at arm's length underfoot is always reachable; otherwise it must be in view.
            const float alignment = distance > 0.35f ? glm::dot(to / distance, look) : 1.0f;
            if (alignment < minCos)
                continue;
            // Nothing solid in between. The candidate itself may be the thing that is hit, and so may
            // what it rests on: a key lying on a desk is reached where the desk begins.
            FHitResult hit;
            if (World->LineTraceSingleByChannel(eye, candidate->GetInteractLocation(), ECollisionChannel::WorldStatic,
                                                const_cast<ALeonHorrorCharacter*>(this), hit) &&
                hit.Actor && hit.Actor != actor && hit.Distance < distance - Tuning.InteractRestingSlack)
                continue;
            // Prefer what is closest to the centre of the view, then what is nearer.
            const float score = alignment * 10.0f - distance;
            if (score > bestScore) {
                bestScore = score;
                best = actor;
            }
        }
        return best;
    }

    std::string ALeonHorrorCharacter::GetInteractPrompt() const {
        const auto* target = Cast<IInteractable>(Focused.Get());
        return target ? target->GetInteractPrompt(*this) : std::string();
    }

    bool ALeonHorrorCharacter::TryInteract() {
        // Held for the call: using a pickup destroys it.
        const TRef<AActor> focused = Focused.Pin();
        auto* target = Cast<IInteractable>(focused);
        if (!target || bDead || !target->CanInteract(*this))
            return false;
        const bool bDone = target->Interact(*this);
        if (bDone && Anim)
            Anim->PlayAction(ELeonHorrorAnimAction::Interact);
        Focused = FindInteractable(); // the target may be gone (a pickup) or changed (a door)
        return bDone;
    }

    void ALeonHorrorCharacter::UpdateAnimInstance(UAnimInstance& InAnim) const {
        ACharacter::UpdateAnimInstance(InAnim);
        InAnim.SetBool("bIsDead", bDead);
        // The engine measures the walking direction against the view. This body turns on its own, so
        // measure against where it actually faces: walking away from the camera is still "forward".
        if (auto movement = GetCharacterMovement()) {
            const glm::vec3 velocity = movement->GetVelocity();
            const float yaw = glm::radians(BodyYaw);
            const glm::vec3 facing(std::cos(yaw), 0.0f, std::sin(yaw));
            InAnim.SetFloat("Direction", ComputeLocomotionDirection({velocity.x, 0.0f, velocity.z}, facing));
        }
    }

    void ALeonHorrorCharacter::HandleDeath(const FDamageInfo& InInfo) {
        (void)InInfo;
        if (bDead)
            return;
        bDead = true;
        bSprinting = false;
        bAiming = false;
        LeonHorrorSound::Play("SFX_Player_Death", 0.9f);
        LeonHorrorSound::Play("STG_Death", 0.8f);
        if (auto movement = GetCharacterMovement()) {
            movement->StopMovementImmediately();
            movement->SetMovementMode(EMovementMode::None);
        }
        if (auto* gameMode = World ? dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode()) : nullptr)
            gameMode->NotifyPlayerDied(*this);
    }

} // namespace Leon
