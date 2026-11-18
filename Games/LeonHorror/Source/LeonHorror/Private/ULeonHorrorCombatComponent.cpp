#include "LeonHorror/ULeonHorrorCombatComponent.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ULeonHorrorAnimInstance.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UAIPerceptionComponent.hpp"
#include "Core/FApplication.hpp"
#include "Engine/FParticleTypes.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/FDamageInfo.hpp"
#include "Gameplay/UGameplayStatics.hpp"
#include "Gameplay/UHealthComponent.hpp"

#include <algorithm>
#include <cmath>

namespace Leon {

    namespace {
        const char* ItemFor(ELeonHorrorWeapon InWeapon) {
            switch (InWeapon) {
            case ELeonHorrorWeapon::Pipe:
                return LeonHorrorItems::Pipe;
            case ELeonHorrorWeapon::Pistol:
                return LeonHorrorItems::Pistol;
            default:
                return "";
            }
        }

        /** Sound and particles need a window and an audio device; headless runs skip them. */
        bool CanPresent() {
            return FApplication::HasInstance();
        }
    } // namespace

    ULeonHorrorCombatComponent::ULeonHorrorCombatComponent(const std::string& InName) : UActorComponent(InName) {}

    ALeonHorrorCharacter* ULeonHorrorCombatComponent::GetPlayer() const {
        return dynamic_cast<ALeonHorrorCharacter*>(Owner);
    }

    bool ULeonHorrorCombatComponent::OwnerHas(ELeonHorrorWeapon InWeapon) const {
        const ALeonHorrorCharacter* player = GetPlayer();
        const auto* inventory = player ? player->GetInventory() : nullptr;
        return inventory && InWeapon != ELeonHorrorWeapon::None && inventory->HasItem(ItemFor(InWeapon));
    }

    void ULeonHorrorCombatComponent::BeginPlay() {
        ALeonHorrorCharacter* player = GetPlayer();
        if (auto* inventory = player ? player->GetInventory() : nullptr)
            inventory->OnChanged.push_back([this](const std::string& InItem) { OnInventoryChanged(InItem); });
    }

    void ULeonHorrorCombatComponent::OnInventoryChanged(const std::string& InItemId) {
        // The first weapon found is drawn automatically; losing the equipped one empties the hands.
        if (Equipped == ELeonHorrorWeapon::None) {
            if (InItemId == LeonHorrorItems::Pipe)
                Equip(ELeonHorrorWeapon::Pipe);
            else if (InItemId == LeonHorrorItems::Pistol)
                Equip(ELeonHorrorWeapon::Pistol);
        } else if (!OwnerHas(Equipped)) {
            Equipped = ELeonHorrorWeapon::None;
        }
    }

    bool ULeonHorrorCombatComponent::Equip(ELeonHorrorWeapon InWeapon) {
        if (InWeapon == Equipped)
            return true;
        if (InWeapon != ELeonHorrorWeapon::None && !OwnerHas(InWeapon))
            return false;
        Equipped = InWeapon;
        // Switching cancels a reload or a swing in progress.
        ReloadRemaining = 0.0f;
        SwingRemaining = 0.0f;
        return true;
    }

    int32_t ULeonHorrorCombatComponent::GetSpareRounds() const {
        const ALeonHorrorCharacter* player = GetPlayer();
        const auto* inventory = player ? player->GetInventory() : nullptr;
        return inventory ? inventory->GetCount(LeonHorrorItems::PistolAmmo) : 0;
    }

    void ULeonHorrorCombatComponent::SetLoadedRounds(int32_t InRounds) {
        LoadedRounds = std::clamp(InRounds, 0, Tuning.PistolClipSize);
    }

    bool ULeonHorrorCombatComponent::StartAttack(bool bInAiming) {
        ALeonHorrorCharacter* player = GetPlayer();
        if (!player || player->IsDead() || !CanAttack())
            return false;

        if (Equipped == ELeonHorrorWeapon::Pipe) {
            SwingRemaining = Tuning.MeleeWindup;
            Cooldown = Tuning.MeleeCooldown;
            if (auto* anim = player->GetHorrorAnim())
                anim->PlayAction(ELeonHorrorAnimAction::MeleeSwing);
            return true;
        }

        if (Equipped == ELeonHorrorWeapon::Pistol) {
            if (!bInAiming)
                return false; // the gun is only raised while aiming
            if (LoadedRounds <= 0) {
                ++DryFires;
                Cooldown = 0.25f;
                if (CanPresent())
                    UGameplayStatics::PlaySound2D("/Game/Audio/SFX_EmptyClick", 0.8f);
                return false;
            }
            FirePistol();
            return true;
        }
        return false;
    }

    void ULeonHorrorCombatComponent::FirePistol() {
        ALeonHorrorCharacter* player = GetPlayer();
        UWorld* world = player ? player->GetWorld() : nullptr;
        if (!world)
            return;

        --LoadedRounds;
        ++ShotsFired;
        Cooldown = Tuning.PistolCooldown;
        LastHitActor = nullptr;

        const glm::vec3 start = player->GetPawnViewLocation();
        const glm::vec3 direction = player->GetControlLookDirection();
        const glm::vec3 end = start + direction * Tuning.PistolRange;

        FHitResult hit;
        glm::vec3 impact = end;
        if (world->LineTraceSingleByChannel(start, end, ECollisionChannel::Visibility, player, hit) && hit.Actor) {
            impact = hit.ImpactPoint;
            LastHitActor = hit.Actor;
            if (auto* health = hit.Actor->FindComponentByClass<UHealthComponent>()) {
                FDamageInfo info;
                info.DamageAmount = Tuning.PistolDamage;
                info.DamageType = EDamageType::Point;
                info.Instigator = player;
                info.Causer = player;
                info.HitActor = hit.Actor;
                info.HitLocation = hit.ImpactPoint;
                info.HitNormal = hit.ImpactNormal;
                info.Impulse = direction * 2.0f;
                health->ApplyDamage(info);
            }
        }

        // A gunshot carries: everything within earshot learns where it came from.
        UAIPerceptionComponent::ReportNoiseEvent(*world, player->GetActorLocation(), Tuning.PistolNoiseRadius, player);

        if (auto* anim = player->GetHorrorAnim())
            anim->PlayAction(ELeonHorrorAnimAction::PistolFire);

        if (CanPresent()) {
            UGameplayStatics::PlaySound2D("/Game/Audio/SFX_RifleFire", 0.9f);
            FParticleEmitterSettings tracer;
            tracer.Kind = EParticleKind::Beam;
            tracer.Lifetime = 0.05f;
            tracer.BeamEnd = impact;
            tracer.BeamThickness = 0.012f;
            tracer.Color = glm::vec4(1.0f, 0.9f, 0.6f, 0.8f);
            UGameplayStatics::SpawnEmitterAtLocation(world, tracer, start + direction * 0.6f);
            FParticleEmitterSettings sparks;
            sparks.BurstCount = 8;
            sparks.Lifetime = 0.18f;
            sparks.Color = glm::vec4(1.0f, 0.8f, 0.5f, 1.0f);
            UGameplayStatics::SpawnEmitterAtLocation(world, sparks, impact);
        }
    }

    void ULeonHorrorCombatComponent::ResolveMeleeHit() {
        ALeonHorrorCharacter* player = GetPlayer();
        UWorld* world = player ? player->GetWorld() : nullptr;
        if (!world || player->IsDead())
            return;

        const glm::vec3 origin = player->GetActorLocation();
        const glm::vec3 forward = player->GetControlPlanarForward();
        const float minCos = std::cos(glm::radians(Tuning.MeleeArcDegrees));
        LastHitActor = nullptr;

        // A swing hits everything alive in the arc. Snapshot: a death callback may spawn or destroy.
        const std::vector<TRef<AActor>> actors = world->GetAllActors();
        for (const auto& actorRef : actors) {
            AActor* target = actorRef.get();
            if (!target || target == player || target->IsPendingKill())
                continue;
            auto* health = target->FindComponentByClass<UHealthComponent>();
            if (!health || health->IsDead())
                continue;
            glm::vec3 to = target->GetActorLocation() - origin;
            to.y = 0.0f;
            const float distance = glm::length(to);
            if (distance > Tuning.MeleeRange)
                continue;
            if (distance > 0.3f && glm::dot(to / distance, forward) < minCos)
                continue;
            // Not through a wall or a closed door.
            FHitResult block;
            if (world->LineTraceSingleByChannel(player->GetPawnViewLocation(), target->GetActorLocation(),
                                                ECollisionChannel::WorldStatic, player, block) &&
                block.Actor && block.Actor != target)
                continue;

            FDamageInfo info;
            info.DamageAmount = Tuning.MeleeDamage;
            info.DamageType = EDamageType::Point;
            info.Instigator = player;
            info.Causer = player;
            info.HitActor = target;
            info.HitLocation = target->GetActorLocation();
            info.HitNormal = distance > 1e-4f ? -(to / distance) : -forward;
            info.Impulse = forward * 3.0f;
            health->ApplyDamage(info);
            ++MeleeHits;
            LastHitActor = target;
        }

        UAIPerceptionComponent::ReportNoiseEvent(*world, origin, Tuning.MeleeNoiseRadius, player);
        if (CanPresent() && LastHitActor)
            UGameplayStatics::PlaySoundAtLocation("/Game/Audio/SFX_HitConfirm", origin + forward, 0.9f, 20.0f);
    }

    bool ULeonHorrorCombatComponent::StartReload() {
        if (Equipped != ELeonHorrorWeapon::Pistol || IsReloading() || IsSwinging())
            return false;
        if (LoadedRounds >= Tuning.PistolClipSize || GetSpareRounds() <= 0)
            return false;
        ReloadRemaining = Tuning.PistolReloadSeconds;
        if (CanPresent())
            UGameplayStatics::PlaySound2D("/Game/Audio/SFX_RifleReload", 0.8f);
        return true;
    }

    void ULeonHorrorCombatComponent::FinishReload() {
        ALeonHorrorCharacter* player = GetPlayer();
        auto* inventory = player ? player->GetInventory() : nullptr;
        if (!inventory)
            return;
        // Only as many rounds as are actually spare: no free ammunition.
        const int32_t wanted = Tuning.PistolClipSize - LoadedRounds;
        const int32_t taken = std::min(wanted, inventory->GetCount(LeonHorrorItems::PistolAmmo));
        if (taken > 0 && inventory->RemoveItem(LeonHorrorItems::PistolAmmo, taken))
            LoadedRounds += taken;
    }

    void ULeonHorrorCombatComponent::Tick(float DeltaSeconds) {
        if (Cooldown > 0.0f)
            Cooldown = std::max(0.0f, Cooldown - DeltaSeconds);

        if (SwingRemaining > 0.0f) {
            SwingRemaining -= DeltaSeconds;
            if (SwingRemaining <= 0.0f) {
                SwingRemaining = 0.0f;
                ResolveMeleeHit();
            }
        }

        if (ReloadRemaining > 0.0f) {
            ReloadRemaining -= DeltaSeconds;
            if (ReloadRemaining <= 0.0f) {
                ReloadRemaining = 0.0f;
                FinishReload();
            }
        }
    }

} // namespace Leon
