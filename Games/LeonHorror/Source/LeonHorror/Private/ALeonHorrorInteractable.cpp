#include "LeonHorror/ALeonHorrorInteractable.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UNavigationSystem.hpp"
#include "Engine/Components.hpp"
#include "Engine/UWorld.hpp"

#include <algorithm>
#include <sstream>

namespace Leon {

    namespace {
        ALeonHorrorGameMode* GameModeOf(const AActor& InActor) {
            UWorld* world = InActor.GetWorld();
            return world ? dynamic_cast<ALeonHorrorGameMode*>(world->GetGameMode()) : nullptr;
        }

        /** Map properties are single-line; authors write \n for a line break. */
        std::string ExpandLineBreaks(const std::string& InText) {
            std::string out;
            out.reserve(InText.size());
            for (size_t i = 0; i < InText.size(); ++i) {
                if (InText[i] == '\\' && i + 1 < InText.size() && InText[i + 1] == 'n') {
                    out.push_back('\n');
                    ++i;
                } else {
                    out.push_back(InText[i]);
                }
            }
            return out;
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // Base
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorInteractable::ALeonHorrorInteractable(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetCanEverTick(false);
    }

    bool ALeonHorrorInteractable::CanInteract(const ALeonHorrorCharacter& InPlayer) const {
        return !IsPendingKill() && !InPlayer.IsDead();
    }

    // ---------------------------------------------------------------------------------------------
    // Pickup
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorPickup::ALeonHorrorPickup(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ALeonHorrorInteractable(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorPickup");
    }

    void ALeonHorrorPickup::OnPropertiesLoaded() {
        ItemId = GetProperty("Item", ItemId);
        ItemCount = std::max(1, GetPropertyInt("Count", ItemCount));
    }

    void ALeonHorrorPickup::SetItem(const std::string& InItemId, int32_t InCount) {
        ItemId = InItemId;
        ItemCount = std::max(1, InCount);
    }

    std::string ALeonHorrorPickup::GetInteractPrompt(const ALeonHorrorCharacter&) const {
        std::string name = LeonHorrorItems::DisplayName(ItemId);
        if (ItemCount > 1)
            name += " x" + std::to_string(ItemCount);
        return "Take " + name;
    }

    bool ALeonHorrorPickup::Interact(ALeonHorrorCharacter& InPlayer) {
        auto* inventory = InPlayer.GetInventory();
        if (!inventory || ItemId.empty())
            return false;
        inventory->AddItem(ItemId, ItemCount);
        if (auto* gameMode = GameModeOf(*this))
            gameMode->NotifyPickupCollected(*this);
        Destroy(); // items do not respawn
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Door
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorDoor::ALeonHorrorDoor(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ALeonHorrorInteractable(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorDoor");
        SetCanEverTick(true); // slides open over a moment
    }

    void ALeonHorrorDoor::PostInitializeComponents() {
        if (!Collider) {
            Collider = AddActorComponent<UBoxComponent>("DoorCollision");
            Collider->SetCollisionObjectType(ECollisionChannel::WorldStatic);
            Collider->SetBoxExtent(Size * 0.5f);
        }
    }

    void ALeonHorrorDoor::SetDoorSize(const glm::vec3& InSize) {
        Size = glm::max(InSize, glm::vec3(0.02f));
        if (Collider)
            Collider->SetBoxExtent(Size * 0.5f);
        RefreshCollision();
    }

    void ALeonHorrorDoor::SetClosedLocation(const glm::vec3& InLocation) {
        ClosedLocation = InLocation;
        bHasClosedLocation = true;
        ApplyOpenState(true);
    }

    void ALeonHorrorDoor::RefreshCollision() {
        if (!Collider)
            return;
        Collider->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        // The collider is a static body: it is created where the actor stands and does not follow it.
        // While closed it must sit in the doorway, wherever the sliding animation currently has the mesh.
        if (!bOpen && HasBegunPlay()) {
            const glm::vec3 current = GetActorLocation();
            SetActorLocation(ClosedLocation);
            Collider->RecreatePhysicsBody();
            SetActorLocation(current);
        }
    }

    void ALeonHorrorDoor::SyncColliderFromMesh() {
        // A door placed in a map carries a box mesh: block exactly what is drawn.
        if (!HasComponent<FMeshComponent>())
            return;
        const auto& mesh = GetComponent<FMeshComponent>();
        if (mesh.MeshType == "Box")
            SetDoorSize({mesh.MeshWidth, mesh.MeshHeight, mesh.MeshDepth});
    }

    void ALeonHorrorDoor::SetRequiredKey(const std::string& InKey) {
        RequiredKey = InKey;
        bLocked = !RequiredKey.empty();
    }

    void ALeonHorrorDoor::OnPropertiesLoaded() {
        SyncColliderFromMesh();
        SetRequiredKey(GetProperty("RequiredKey", RequiredKey));
        if (HasProperty("SlideX") || HasProperty("SlideY") || HasProperty("SlideZ")) {
            SlideOffset = {GetPropertyFloat("SlideX"), GetPropertyFloat("SlideY"), GetPropertyFloat("SlideZ")};
            bHasSlideOffset = true;
        }
        ClosedLocation = GetActorLocation();
        bHasClosedLocation = true;
        bOpen = GetPropertyBool("Open", false);
        if (bOpen)
            bLocked = false;
        ApplyOpenState(true);
    }

    void ALeonHorrorDoor::BeginPlay() {
        if (!bHasClosedLocation) {
            ClosedLocation = GetActorLocation();
            bHasClosedLocation = true;
        }
        ApplyOpenState(true);
    }

    void ALeonHorrorDoor::SetState(bool bInLocked, bool bInOpen, bool bInSnap) {
        bLocked = bInLocked && !bInOpen;
        bOpen = bInOpen;
        ApplyOpenState(bInSnap);
    }

    void ALeonHorrorDoor::ApplyOpenState(bool bInSnap) {
        if (!bHasClosedLocation) {
            ClosedLocation = GetActorLocation();
            bHasClosedLocation = true;
        }
        if (!bHasSlideOffset)
            SlideOffset = glm::vec3(Size.x, 0.0f, 0.0f);

        if (bInSnap) {
            OpenAlpha = bOpen ? 1.0f : 0.0f;
            SetActorLocation(ClosedLocation + SlideOffset * OpenAlpha);
        }
        // Collision changes at once: passable as soon as the door starts to open, solid as soon as it
        // starts to close.
        RefreshCollision();

        // Enemies path on a grid built from static collision: a door changing state changes the grid.
        if (World && World->HasBegunPlay()) {
            UNavigationSystem* navigation = World->GetNavigationSystem();
            if (navigation && navigation->IsBuilt())
                World->RebuildNavigation();
        }
    }

    void ALeonHorrorDoor::Tick(float DeltaSeconds) {
        const float target = bOpen ? 1.0f : 0.0f;
        if (OpenAlpha == target)
            return;
        const float step = DeltaSeconds / 0.45f;
        OpenAlpha = bOpen ? std::min(1.0f, OpenAlpha + step) : std::max(0.0f, OpenAlpha - step);
        SetActorLocation(ClosedLocation + SlideOffset * OpenAlpha);
    }

    std::string ALeonHorrorDoor::GetInteractPrompt(const ALeonHorrorCharacter& InPlayer) const {
        if (bLocked) {
            const auto* inventory = InPlayer.GetInventory();
            if (inventory && inventory->HasItem(RequiredKey))
                return "Unlock with " + LeonHorrorItems::DisplayName(RequiredKey);
            return "Locked";
        }
        return bOpen ? "Close door" : "Open door";
    }

    bool ALeonHorrorDoor::Interact(ALeonHorrorCharacter& InPlayer) {
        if (bLocked) {
            const auto* inventory = InPlayer.GetInventory();
            if (!inventory || !inventory->HasItem(RequiredKey))
                return false;
            bLocked = false;
            bOpen = true;
        } else {
            bOpen = !bOpen;
        }
        ApplyOpenState(false);
        if (auto* gameMode = GameModeOf(*this))
            gameMode->NotifyDoorChanged(*this);
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Note
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorNote::ALeonHorrorNote(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ALeonHorrorInteractable(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorNote");
    }

    void ALeonHorrorNote::OnPropertiesLoaded() {
        Title = GetProperty("Title", Title);
        Text = ExpandLineBreaks(GetProperty("Text", Text));
    }

    void ALeonHorrorNote::SetContent(const std::string& InTitle, const std::string& InText) {
        Title = InTitle;
        Text = InText;
    }

    std::string ALeonHorrorNote::GetInteractPrompt(const ALeonHorrorCharacter&) const {
        return "Read " + Title;
    }

    bool ALeonHorrorNote::Interact(ALeonHorrorCharacter&) {
        auto* gameMode = GameModeOf(*this);
        if (!gameMode)
            return false;
        gameMode->ShowNote(*this);
        return true;
    }

    std::vector<std::string> ALeonHorrorNote::WrapText(const std::string& InText, size_t InMaxColumns) {
        std::vector<std::string> lines;
        const size_t columns = std::max<size_t>(InMaxColumns, 8);
        std::stringstream paragraphs(InText);
        std::string paragraph;
        while (std::getline(paragraphs, paragraph, '\n')) {
            std::stringstream words(paragraph);
            std::string word;
            std::string line;
            while (words >> word) {
                // A single word longer than the line is split hard.
                while (word.size() > columns) {
                    if (!line.empty()) {
                        lines.push_back(line);
                        line.clear();
                    }
                    lines.push_back(word.substr(0, columns));
                    word = word.substr(columns);
                }
                if (line.empty()) {
                    line = word;
                } else if (line.size() + 1 + word.size() <= columns) {
                    line += " " + word;
                } else {
                    lines.push_back(line);
                    line = word;
                }
            }
            lines.push_back(line); // keeps blank lines between paragraphs
        }
        return lines;
    }

    // ---------------------------------------------------------------------------------------------
    // Save point
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorSavePoint::ALeonHorrorSavePoint(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ALeonHorrorInteractable(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorSavePoint");
    }

    std::string ALeonHorrorSavePoint::GetInteractPrompt(const ALeonHorrorCharacter&) const {
        return "Save";
    }

    bool ALeonHorrorSavePoint::Interact(ALeonHorrorCharacter& InPlayer) {
        auto* gameMode = GameModeOf(*this);
        return gameMode && gameMode->RequestSave(InPlayer);
    }

    // ---------------------------------------------------------------------------------------------
    // Exit
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorExit::ALeonHorrorExit(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ALeonHorrorInteractable(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorExit");
    }

    void ALeonHorrorExit::OnPropertiesLoaded() {
        RequiredKey = GetProperty("RequiredKey", RequiredKey);
    }

    std::string ALeonHorrorExit::GetInteractPrompt(const ALeonHorrorCharacter& InPlayer) const {
        const auto* inventory = InPlayer.GetInventory();
        if (!RequiredKey.empty() && !(inventory && inventory->HasItem(RequiredKey)))
            return "Locked";
        return "Leave";
    }

    bool ALeonHorrorExit::Interact(ALeonHorrorCharacter& InPlayer) {
        const auto* inventory = InPlayer.GetInventory();
        if (!RequiredKey.empty() && !(inventory && inventory->HasItem(RequiredKey)))
            return false;
        if (auto* gameMode = GameModeOf(*this))
            gameMode->NotifySliceCompleted();
        return true;
    }

} // namespace Leon
