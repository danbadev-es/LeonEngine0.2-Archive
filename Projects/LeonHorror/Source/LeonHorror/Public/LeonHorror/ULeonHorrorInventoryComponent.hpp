#pragma once

#include "Gameplay/UActorComponent.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Leon {

    /** Well-known item ids. Keys are "Key.<Name>", matching a door's RequiredKey property. */
    namespace LeonHorrorItems {
        inline constexpr const char* Pipe = "Weapon.Pipe";
        inline constexpr const char* Pistol = "Weapon.Pistol";
        inline constexpr const char* PistolAmmo = "Ammo.Pistol";
        inline constexpr const char* Medkit = "Medkit";
        inline constexpr const char* KeyPrefix = "Key.";

        /** Text shown to the player for an item id. */
        std::string DisplayName(const std::string& InItemId);
    } // namespace LeonHorrorItems

    /**
     * What the player carries: item id -> count. Plain data, no actors, so it can be saved and restored
     * as text and survives the pawn being replaced.
     */
    class ULeonHorrorInventoryComponent : public UActorComponent {
    public:
        explicit ULeonHorrorInventoryComponent(const std::string& InName = "Inventory");

        /** Adds InCount (>= 1) of an item and returns the new total. */
        int32_t AddItem(const std::string& InItemId, int32_t InCount = 1);
        /** Removes InCount if that many are held; otherwise changes nothing and returns false. */
        bool RemoveItem(const std::string& InItemId, int32_t InCount = 1);
        int32_t GetCount(const std::string& InItemId) const;
        bool HasItem(const std::string& InItemId) const { return GetCount(InItemId) > 0; }
        void SetCount(const std::string& InItemId, int32_t InCount);
        void Clear();

        const std::map<std::string, int32_t>& GetItems() const { return Items; }

        /** Fired after any change, with the item id that changed. */
        std::vector<std::function<void(const std::string&)>> OnChanged;

    private:
        void Broadcast(const std::string& InItemId);

        std::map<std::string, int32_t> Items;
    };

} // namespace Leon
