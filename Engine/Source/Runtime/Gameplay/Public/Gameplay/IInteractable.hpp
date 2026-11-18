#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Leon {

    class APawn;

    /**
     * Something a pawn can use: pick up, open, read, talk to. Any actor or component may implement
     * it, whatever its base class; find it with `Cast<IInteractable>(actor)`.
     *
     * The interface says what can be done, not how the pawn finds it. Reach, line of sight and
     * which candidate wins are the game's choice.
     */
    class IInteractable {
    public:
        virtual ~IInteractable() = default;

        /** Whether it can be targeted at all right now. */
        virtual bool CanInteract(const APawn& InInstigator) const = 0;
        /** Short prompt for the HUD, e.g. "Open door" or "Locked". */
        virtual std::string GetInteractPrompt(const APawn& InInstigator) const = 0;
        /** Performs the action. Returns false when nothing happened. */
        virtual bool Interact(APawn& InInstigator) = 0;
        /** The point the pawn has to reach and look at. */
        virtual glm::vec3 GetInteractLocation() const = 0;
    };

} // namespace Leon
