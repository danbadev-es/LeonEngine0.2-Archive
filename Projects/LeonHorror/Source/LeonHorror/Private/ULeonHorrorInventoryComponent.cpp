#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include <algorithm>

namespace Leon {

    std::string LeonHorrorItems::DisplayName(const std::string& InItemId) {
        if (InItemId == Pipe)
            return "Steel pipe";
        if (InItemId == Pistol)
            return "Handgun";
        if (InItemId == PistolAmmo)
            return "Handgun bullets";
        if (InItemId == Medkit)
            return "First aid kit";
        const std::string prefix = KeyPrefix;
        if (InItemId.rfind(prefix, 0) == 0)
            return InItemId.substr(prefix.size()) + " key";
        return InItemId;
    }

    ULeonHorrorInventoryComponent::ULeonHorrorInventoryComponent(const std::string& InName) : UActorComponent(InName) {
        SetComponentTickEnabled(false);
    }

    int32_t ULeonHorrorInventoryComponent::AddItem(const std::string& InItemId, int32_t InCount) {
        if (InItemId.empty() || InCount <= 0)
            return GetCount(InItemId);
        const int32_t total = (Items[InItemId] += InCount);
        Broadcast(InItemId);
        return total;
    }

    bool ULeonHorrorInventoryComponent::RemoveItem(const std::string& InItemId, int32_t InCount) {
        const auto it = Items.find(InItemId);
        if (InCount <= 0 || it == Items.end() || it->second < InCount)
            return false;
        it->second -= InCount;
        if (it->second == 0)
            Items.erase(it);
        Broadcast(InItemId);
        return true;
    }

    int32_t ULeonHorrorInventoryComponent::GetCount(const std::string& InItemId) const {
        const auto it = Items.find(InItemId);
        return it != Items.end() ? it->second : 0;
    }

    void ULeonHorrorInventoryComponent::SetCount(const std::string& InItemId, int32_t InCount) {
        if (InItemId.empty())
            return;
        if (InCount <= 0)
            Items.erase(InItemId);
        else
            Items[InItemId] = InCount;
        Broadcast(InItemId);
    }

    void ULeonHorrorInventoryComponent::Clear() {
        Items.clear();
        Broadcast("");
    }

    void ULeonHorrorInventoryComponent::Broadcast(const std::string& InItemId) {
        const auto callbacks = OnChanged; // a listener may add or remove listeners
        for (const auto& callback : callbacks) {
            if (callback)
                callback(InItemId);
        }
    }

} // namespace Leon
