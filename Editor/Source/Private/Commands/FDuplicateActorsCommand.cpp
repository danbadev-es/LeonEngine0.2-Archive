#include "Editor/Commands/FDuplicateActorsCommand.hpp"
#include "Editor/Context/FEditorSelection.hpp"

namespace Leon::Editor {

    FDuplicateActorsCommand::FDuplicateActorsCommand(UWorld* InWorld, FEditorSelection* InSelection,
                                                     std::vector<AActor*> InSources, float InOffsetX)
        : World(InWorld), Selection(InSelection), OffsetX(InOffsetX) {
        if (Selection)
            PreviousSelection = MakeActorRefs(Selection->GetSelectedActors());

        for (AActor* src : InSources) {
            if (src && !src->IsPendingKill())
                SourceSnapshots.push_back(CaptureActorSnapshot(*src));
        }

        if (SourceSnapshots.size() == 1)
            Description = "Duplicate " + SourceSnapshots[0].Name;
        else
            Description = "Duplicate " + std::to_string(SourceSnapshots.size()) + " Actors";
    }

    void FDuplicateActorsCommand::Execute() {
        if (!World || SourceSnapshots.empty())
            return;

        // The duplicates get their identity once. A redo recreates them with the same GUIDs, so commands
        // recorded after the first duplicate (move, delete) keep referring to the right actors.
        if (DuplicateGuids.size() != SourceSnapshots.size()) {
            DuplicateGuids.clear();
            for (size_t i = 0; i < SourceSnapshots.size(); ++i)
                DuplicateGuids.push_back(FUUID::Generate());
        }

        Duplicates.clear();
        for (size_t i = 0; i < SourceSnapshots.size(); ++i) {
            FActorEditorSnapshot copy = SourceSnapshots[i];
            copy.Name = SourceSnapshots[i].Name + "_Copy";
            copy.Guid = DuplicateGuids[i];
            if (copy.bHasTransform)
                copy.Transform.Translation.x += OffsetX;

            if (AActor* dup = RestoreActorSnapshot(*World, copy))
                Duplicates.emplace_back(dup);
        }

        if (Selection && !Duplicates.empty())
            Selection->SetSelectedActors(ResolveActorRefs(Duplicates));
    }

    void FDuplicateActorsCommand::Undo() {
        if (!World)
            return;

        // Clear the selection first: it holds pointers to the actors about to be freed.
        if (Selection)
            Selection->ClearActorSelection();
        for (AActor* dup : ResolveActorRefs(Duplicates))
            World->DestroyActor(dup);

        if (Selection)
            Selection->SetSelectedActors(ResolveActorRefs(PreviousSelection));
    }

} // namespace Leon::Editor
