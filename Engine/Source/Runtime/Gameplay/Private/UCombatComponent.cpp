#include "Gameplay/UCombatComponent.hpp"

#include <algorithm>

namespace Leon {

    UCombatComponent::UCombatComponent(const std::string& InName) : UActorComponent(InName) {}

    void UCombatComponent::Tick(float DeltaSeconds) {
        if (CooldownRemaining > 0.0f)
            CooldownRemaining = std::max(0.0f, CooldownRemaining - DeltaSeconds);
    }

    bool UCombatComponent::CanAttack() const {
        return CooldownRemaining <= 0.0f;
    }

    bool UCombatComponent::Attack() {
        if (!CanAttack())
            return false;
        CooldownRemaining = AttackCooldown;
        OnAttack.Broadcast();
        return true;
    }

} // namespace Leon
