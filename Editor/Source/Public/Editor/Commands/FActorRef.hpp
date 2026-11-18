#pragma once

#include "Assets/FAssetTypes.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"

#include <vector>

namespace Leon::Editor {

    /**
     * @brief Identity of an actor for the undo history: world + GUID, resolved on use.
     *
     * Commands outlive the actors they refer to. Deleting an actor frees it, and undoing the delete creates
     * a new object with the same GUID, so a raw pointer stored in an older command would point at freed
     * memory. Resolving by GUID finds the current object, or null when the actor does not exist right now.
     *
     * The history is cleared whenever the editor world is replaced, so the world pointer never goes stale.
     */
    struct FActorRef {
        UWorld* World = nullptr;
        FUUID Guid;

        FActorRef() = default;
        explicit FActorRef(AActor* InActor) {
            if (InActor && !InActor->IsPendingKill()) {
                World = InActor->GetWorld();
                Guid = InActor->GetActorGuid();
            }
        }
        FActorRef(UWorld* InWorld, const FUUID& InGuid) : World(InWorld), Guid(InGuid) {}

        [[nodiscard]] bool IsSet() const { return World != nullptr && Guid.IsValid(); }

        /** The live actor, or null if it is deleted (or pending-kill) at the moment. */
        [[nodiscard]] AActor* Resolve() const { return IsSet() ? World->FindActorByGuid(Guid) : nullptr; }

        bool operator==(const FActorRef& InOther) const { return World == InOther.World && Guid == InOther.Guid; }
        bool operator!=(const FActorRef& InOther) const { return !(*this == InOther); }
    };

    inline std::vector<FActorRef> MakeActorRefs(const std::vector<AActor*>& InActors) {
        std::vector<FActorRef> refs;
        refs.reserve(InActors.size());
        for (AActor* actor : InActors) {
            FActorRef ref(actor);
            if (ref.IsSet())
                refs.push_back(ref);
        }
        return refs;
    }

    /** Live actors only; refs whose actor is currently deleted are skipped. */
    inline std::vector<AActor*> ResolveActorRefs(const std::vector<FActorRef>& InRefs) {
        std::vector<AActor*> actors;
        actors.reserve(InRefs.size());
        for (const FActorRef& ref : InRefs) {
            if (AActor* actor = ref.Resolve())
                actors.push_back(actor);
        }
        return actors;
    }

} // namespace Leon::Editor
