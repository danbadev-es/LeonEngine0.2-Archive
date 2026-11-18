#include "Editor/Commands/FSpawnActorsCommand.hpp"
#include "Editor/Context/FEditorSelection.hpp"

namespace Leon::Editor {

    FSpawnActorsCommand::FSpawnActorsCommand(UWorld* InWorld, FEditorSelection* InSelection,
                                             std::vector<AActor*> InActors)
        : World(InWorld), Selection(InSelection) {
        if (Selection)
            PreviousSelection = MakeActorRefs(Selection->GetSelectedActors());

        for (AActor* actor : InActors) {
            if (!actor || actor->IsPendingKill())
                continue;
            Snapshots.push_back(CaptureActorSnapshot(*actor));
            LiveActors.emplace_back(actor);
        }

        if (Snapshots.size() == 1)
            Description = "Spawn " + Snapshots.front().Name;
        else
            Description = "Spawn " + std::to_string(Snapshots.size()) + " Actors";
    }

    void FSpawnActorsCommand::Execute() {
        if (!World || Snapshots.empty())
            return;

        // The snapshot carries the GUID, so the restored actor keeps the identity older commands refer to.
        LiveActors.clear();
        for (const FActorEditorSnapshot& snap : Snapshots) {
            if (AActor* restored = RestoreActorSnapshot(*World, snap))
                LiveActors.emplace_back(restored);
        }

        if (Selection && !LiveActors.empty())
            Selection->SetSelectedActors(ResolveActorRefs(LiveActors));
    }

    void FSpawnActorsCommand::Undo() {
        if (!World)
            return;

        // Clear the selection first: it holds pointers to the actors about to be freed.
        if (Selection)
            Selection->ClearActorSelection();
        for (AActor* actor : ResolveActorRefs(LiveActors))
            World->DestroyActor(actor);

        if (Selection)
            Selection->SetSelectedActors(ResolveActorRefs(PreviousSelection));
    }

    std::string FSpawnActorsCommand::GetDescription() const {
        return Description;
    }

} // namespace Leon::Editor
