#pragma once

#include "Gameplay/UActorComponent.hpp"
#include "Core/TMulticastDelegate.hpp"
#include "Gameplay/FDamageInfo.hpp"

#include <functional>
#include <vector>

namespace Leon {

    /**
     * @brief Generic vitality. Knows only health, healing, damage, and death — no scoring or teams.
     */
    class UHealthComponent : public UActorComponent {
    public:
        UHealthComponent(const std::string& InName = "HealthComponent");

        void EndPlay() override;

        float GetHealth() const { return Health; }
        float GetMaxHealth() const { return MaxHealth; }
        void SetMaxHealth(float InMax);
        void SetHealth(float InHealth);
        void ResetHealth();

        void ApplyDamage(float InAmount);
        void ApplyDamage(const FDamageInfo& InInfo);
        void Heal(float InAmount);
        bool IsDead() const { return bIsDead; }

        TMulticastDelegate<void(float InOldHealth, float InNewHealth, float InMaxHealth)> OnHealthChanged;
        TMulticastDelegate<void(const FDamageInfo& InInfo)> OnDamage;
        TMulticastDelegate<void(float InAmount, float InNewHealth)> OnHealed;
        TMulticastDelegate<void(const FDamageInfo& InInfo)> OnDeath;

    private:
        void BroadcastHealthChanged(float InOld);
        void BecomeDead(const FDamageInfo& InInfo);

        float MaxHealth = 100.0f;
        float Health = 100.0f;
        bool bIsDead = false;
    };

} // namespace Leon
