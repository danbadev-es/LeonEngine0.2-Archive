#include "Editor/Commands/FTransformActorsCommand.hpp"
#include "Engine/Components.hpp"

namespace Leon::Editor {

    FActorTransformState FTransformActorsCommand::Capture(AActor* InActor) {
        FActorTransformState state;
        state.Actor = InActor;
        state.Ref = FActorRef(InActor);
        if (!InActor || !InActor->HasComponent<FTransformComponent>())
            return state;
        const auto& tc = InActor->GetComponent<FTransformComponent>();
        state.Location = tc.Translation;
        state.Rotation = tc.Rotation;
        state.Scale = tc.Scale;
        return state;
    }

    void FTransformActorsCommand::Apply(const FActorTransformState& InState) {
        // Resolve by GUID: the actor may have been deleted and restored (a new object) since the capture,
        // or be deleted right now, in which case there is nothing to move.
        AActor* actor = InState.Ref.Resolve();
        if (!actor)
            return;
        actor->SetActorLocation(InState.Location);
        actor->SetActorRotation(InState.Rotation);
        actor->SetActorScale(InState.Scale);
    }

    bool FTransformActorsCommand::Differ(const FActorTransformState& InA, const FActorTransformState& InB) {
        constexpr float kEps = 1e-5f;
        auto ne = [](const glm::vec3& a, const glm::vec3& b) {
            return glm::abs(a.x - b.x) > kEps || glm::abs(a.y - b.y) > kEps || glm::abs(a.z - b.z) > kEps;
        };
        return InA.Ref != InB.Ref || ne(InA.Location, InB.Location) || ne(InA.Rotation, InB.Rotation) ||
               ne(InA.Scale, InB.Scale);
    }

    FTransformActorsCommand::FTransformActorsCommand(std::vector<FActorTransformState> InBefore,
                                                     std::vector<FActorTransformState> InAfter,
                                                     std::string InDescription)
        : Before(std::move(InBefore)), After(std::move(InAfter)), Description(std::move(InDescription)) {
        if (Description.empty()) {
            AActor* single = After.size() == 1 ? After[0].Ref.Resolve() : nullptr;
            if (single)
                Description = "Transform " + single->GetName();
            else
                Description = "Transform " + std::to_string(After.size()) + " Actors";
        }
    }

    void FTransformActorsCommand::Execute() {
        for (const auto& state : After)
            Apply(state);
    }

    void FTransformActorsCommand::Undo() {
        for (const auto& state : Before)
            Apply(state);
    }

} // namespace Leon::Editor
