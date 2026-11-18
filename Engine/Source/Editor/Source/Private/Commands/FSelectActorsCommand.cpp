#include "Editor/Commands/FSelectActorsCommand.hpp"
#include "Editor/Context/FEditorSelection.hpp"

namespace Leon::Editor {

    FSelectActorsCommand::FSelectActorsCommand(FEditorSelection* InSelection, std::vector<AActor*> InBefore,
                                               std::vector<AActor*> InAfter)
        : Selection(InSelection), Before(MakeActorRefs(InBefore)), After(MakeActorRefs(InAfter)) {
        if (InAfter.empty())
            Description = "Select None";
        else if (InAfter.size() == 1 && InAfter[0])
            Description = "Select " + InAfter[0]->GetName();
        else
            Description = "Select " + std::to_string(InAfter.size()) + " Actors";
    }

    // Actors deleted since the command was recorded are simply left out of the restored selection.
    void FSelectActorsCommand::Execute() {
        if (Selection)
            Selection->SetSelectedActors(ResolveActorRefs(After));
    }

    void FSelectActorsCommand::Undo() {
        if (Selection)
            Selection->SetSelectedActors(ResolveActorRefs(Before));
    }

} // namespace Leon::Editor
