#pragma once

#include "Gameplay/AActor.hpp"
#include "Gameplay/IInteractable.hpp"
#include "Gameplay/UPrimitiveComponent.hpp"

#include <string>

namespace Leon {

    /**
     * Base for things the player uses with the interact key: pickups, doors, notes, save points.
     * A convenience, not a requirement: the player uses anything that implements IInteractable.
     *
     * The player finds its target geometrically (within reach, closest to where it is looking, with a
     * clear line to it), so an interactable needs no collider of its own. Authored data comes from the
     * actor's map properties; state that must survive a save is reported to the game mode by GUID.
     */
    class ALeonHorrorInteractable : public AActor, public IInteractable {
    public:
        ALeonHorrorInteractable() = default;
        ALeonHorrorInteractable(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Interactable");

        /** Only the living player, and only while this is in the world (a collected pickup is not). */
        bool CanInteract(const APawn& InInstigator) const override;
        /** Point the player has to look at; defaults to the actor location. */
        glm::vec3 GetInteractLocation() const override { return GetActorLocation(); }
    };

    /** An item lying in the level. Properties: Item (id), Count (default 1). */
    class ALeonHorrorPickup : public ALeonHorrorInteractable {
    public:
        ALeonHorrorPickup() = default;
        ALeonHorrorPickup(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Pickup");

        void OnPropertiesLoaded() override;
        std::string GetInteractPrompt(const APawn& InInstigator) const override;
        bool Interact(APawn& InInstigator) override;

        void SetItem(const std::string& InItemId, int32_t InCount = 1);
        const std::string& GetItemId() const { return ItemId; }
        int32_t GetItemCount() const { return ItemCount; }

    private:
        std::string ItemId;
        int32_t ItemCount = 1;
    };

    /**
     * A door. Properties: RequiredKey (item id; empty = never locked), Open (true/false),
     * SlideX/SlideY/SlideZ (how far it moves when open; default: its own width along X).
     * Unlocking does not consume the key.
     */
    class ALeonHorrorDoor : public ALeonHorrorInteractable {
    public:
        ALeonHorrorDoor() = default;
        ALeonHorrorDoor(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Door");

        void PostInitializeComponents() override;
        void OnPropertiesLoaded() override;
        void BeginPlay() override;
        void Tick(float DeltaSeconds) override;

        std::string GetInteractPrompt(const APawn& InInstigator) const override;
        bool Interact(APawn& InInstigator) override;

        bool IsLocked() const { return bLocked; }
        bool IsOpen() const { return bOpen; }
        const std::string& GetRequiredKey() const { return RequiredKey; }
        void SetRequiredKey(const std::string& InKey);
        /** Sets the lock and open state directly (loading a save); applies collision and position. */
        void SetState(bool bInLocked, bool bInOpen, bool bInSnap = true);
        /** Full size of the blocking box, in metres. */
        void SetDoorSize(const glm::vec3& InSize);
        /** Where the door sits when closed. Use this instead of SetActorLocation to place a door. */
        void SetClosedLocation(const glm::vec3& InLocation);
        const glm::vec3& GetClosedLocation() const { return ClosedLocation; }

        TRef<UBoxComponent> GetCollider() const { return Collider; }

    private:
        void ApplyOpenState(bool bInSnap);
        void RefreshCollision();
        void SyncColliderFromMesh();

        TRef<UBoxComponent> Collider;
        std::string RequiredKey;
        bool bLocked = false;
        bool bOpen = false;
        glm::vec3 ClosedLocation{0.0f};
        bool bHasClosedLocation = false;
        glm::vec3 SlideOffset{0.0f};
        bool bHasSlideOffset = false;
        glm::vec3 Size{1.0f, 2.2f, 0.12f};
        float OpenAlpha = 0.0f;
    };

    /** Something to read. Properties: Title, Text ("\n" for line breaks). */
    class ALeonHorrorNote : public ALeonHorrorInteractable {
    public:
        ALeonHorrorNote() = default;
        ALeonHorrorNote(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Note");

        void OnPropertiesLoaded() override;
        std::string GetInteractPrompt(const APawn& InInstigator) const override;
        bool Interact(APawn& InInstigator) override;

        void SetContent(const std::string& InTitle, const std::string& InText);
        const std::string& GetTitle() const { return Title; }
        const std::string& GetText() const { return Text; }

        /** Breaks text into lines of at most InMaxColumns characters at word boundaries. */
        static std::vector<std::string> WrapText(const std::string& InText, size_t InMaxColumns);

    private:
        std::string Title = "Note";
        std::string Text;
    };

    /** Where the game is saved. */
    class ALeonHorrorSavePoint : public ALeonHorrorInteractable {
    public:
        ALeonHorrorSavePoint() = default;
        ALeonHorrorSavePoint(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "SavePoint");

        std::string GetInteractPrompt(const APawn& InInstigator) const override;
        bool Interact(APawn& InInstigator) override;
    };

    /** The way out. Properties: RequiredKey (item id). Using it with the key ends the slice. */
    class ALeonHorrorExit : public ALeonHorrorInteractable {
    public:
        ALeonHorrorExit() = default;
        ALeonHorrorExit(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Exit");

        void OnPropertiesLoaded() override;
        std::string GetInteractPrompt(const APawn& InInstigator) const override;
        bool Interact(APawn& InInstigator) override;

        void SetRequiredKey(const std::string& InKey) { RequiredKey = InKey; }
        const std::string& GetRequiredKey() const { return RequiredKey; }

    private:
        std::string RequiredKey;
    };

} // namespace Leon
