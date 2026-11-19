#include "LeonHorror/ALeonHorrorInteractable.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UNavigationSystem.hpp"
#include "Core/Cast.hpp"
#include "Scene/Components.hpp"
#include "Engine/UWorld.hpp"

#include "Gameplay/UGameplayStatics.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace Leon {

    namespace {
        /** What the pawn carries; null for anything but the player. */
        ULeonHorrorInventoryComponent* InventoryOf(const APawn& InPawn) {
            const auto* player = Cast<const ALeonHorrorCharacter>(&InPawn);
            return player ? player->GetInventory() : nullptr;
        }

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

    bool ALeonHorrorInteractable::CanInteract(const APawn& InInstigator) const {
        const auto* player = Cast<const ALeonHorrorCharacter>(&InInstigator);
        return !IsPendingKill() && player && !player->IsDead();
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

    std::string ALeonHorrorPickup::GetInteractPrompt(const APawn&) const {
        std::string name = LeonHorrorItems::DisplayName(ItemId);
        if (ItemCount > 1)
            name += " x" + std::to_string(ItemCount);
        return "Take " + name;
    }

    bool ALeonHorrorPickup::Interact(APawn& InInstigator) {
        auto* inventory = InventoryOf(InInstigator);
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
        }
        ConfigureCollider();
    }

    void ALeonHorrorDoor::ConfigureCollider() {
        if (!Collider)
            return;
        if (!bHinged) {
            Collider->SetRelativeLocation(glm::vec3(0.0f));
            Collider->SetRelativeRotation(glm::vec3(0.0f));
            Collider->SetBoxExtent(Size * 0.5f);
            return;
        }
        // The box sits on the closed leaf: half a width along the door from the hinge, half a height
        // up. It is kept square to the world (its own turn cancels that of the actor) and its sides
        // are swapped for a door that runs along Z, so it blocks the same whether or not the physics
        // scene in use honours the rotation of a box.
        const float yaw = glm::radians(ClosedRotation.y);
        const bool bAlongZ = std::abs(std::sin(yaw)) > 0.7071f;
        Collider->SetRelativeLocation({Size.x * 0.5f, Size.y * 0.5f, 0.0f});
        Collider->SetRelativeRotation(-ClosedRotation);
        Collider->SetBoxExtent(bAlongZ ? glm::vec3(Size.z, Size.y, Size.x) * 0.5f : Size * 0.5f);
    }

    void ALeonHorrorDoor::SetDoorSize(const glm::vec3& InSize) {
        Size = glm::max(InSize, glm::vec3(0.02f));
        ConfigureCollider();
        RefreshCollision();
    }

    void ALeonHorrorDoor::SetHinged(float InSwingDegrees, const glm::vec3& InLeafSize) {
        bHinged = true;
        SwingDegrees = InSwingDegrees;
        Size = glm::max(InLeafSize, glm::vec3(0.02f));
        ClosedRotation = GetActorRotation();
        ClosedLocation = GetActorLocation();
        bHasClosedLocation = true;
        ConfigureCollider();
        ApplyOpenState(true);
    }

    glm::vec3 ALeonHorrorDoor::GetInteractLocation() const {
        if (!bHinged)
            return GetActorLocation();
        // The middle of the closed leaf at hand height, wherever the leaf has swung to.
        const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(ClosedRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        return ClosedLocation + glm::vec3(turn * glm::vec4(Size.x * 0.5f, 1.05f, 0.0f, 0.0f));
    }

    void ALeonHorrorDoor::ApplyPose() {
        if (bHinged) {
            // Eased, so the leaf starts and settles gently instead of turning at one speed.
            const float eased = OpenAlpha * OpenAlpha * (3.0f - 2.0f * OpenAlpha);
            SetActorRotation(ClosedRotation + glm::vec3(0.0f, SwingDegrees * eased, 0.0f));
        } else {
            SetActorLocation(ClosedLocation + SlideOffset * OpenAlpha);
        }
    }

    void ALeonHorrorDoor::PlayDoorSound(const char* InWhat) const {
        if (!HasBegunPlay())
            return;
        const std::string what = InWhat;
        std::string sound = "/Game/Audio/SFX_Door_" + what;
        // Swing and metal doors have one sound of their own for opening and closing alike.
        if (SoundSet != "Wood" && (what == "Open" || what == "Close"))
            sound = "/Game/Audio/SFX_Door_" + SoundSet;
        UGameplayStatics::PlaySoundAtLocation(sound, GetInteractLocation(), 0.9f, 18.0f);
    }

    ALeonHorrorDoor* ALeonHorrorDoor::FindPartner() const {
        if (PartnerName.empty() || !World)
            return nullptr;
        return Cast<ALeonHorrorDoor>(World->FindActorByName(PartnerName));
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
            const glm::vec3 location = GetActorLocation();
            const glm::vec3 rotation = GetActorRotation();
            SetActorLocation(ClosedLocation);
            if (bHinged)
                SetActorRotation(ClosedRotation);
            Collider->RecreatePhysicsBody();
            SetActorLocation(location);
            SetActorRotation(rotation);
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
        ClosedRotation = GetActorRotation();
        if (HasProperty("Swing")) {
            bHinged = true;
            SwingDegrees = GetPropertyFloat("Swing", SwingDegrees);
            Size = {GetPropertyFloat("Width", 0.9f), GetPropertyFloat("Height", 2.05f), GetPropertyFloat("Thickness", 0.05f)};
            ConfigureCollider();
        }
        PartnerName = GetProperty("Partner", PartnerName);
        SoundSet = GetProperty("Sound", SoundSet);
        bNeedsPower = HasProperty("PowerGroup");
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
            ApplyPose();
        }
        // Collision changes at once: passable as soon as the door starts to open, solid as soon as it
        // starts to close.
        RefreshCollision();

        // Enemies path on a grid built from collision: a door changing state changes the grid around it.
        if (World && World->HasBegunPlay()) {
            UNavigationSystem* navigation = World->GetNavigationSystem();
            if (navigation && navigation->IsBuilt()) {
                const glm::vec3 middle = GetInteractLocation();
                const float half = std::max(Size.x, Size.z) * 0.5f;
                navigation->UpdateRegion(*World, middle - glm::vec3(half, 0.0f, half), middle + glm::vec3(half, 0.0f, half));
            }
        }
    }

    void ALeonHorrorDoor::Tick(float DeltaSeconds) {
        const float target = bOpen ? 1.0f : 0.0f;
        if (OpenAlpha == target)
            return;
        const float step = DeltaSeconds / (bHinged ? 0.8f : 0.45f);
        OpenAlpha = bOpen ? std::min(1.0f, OpenAlpha + step) : std::max(0.0f, OpenAlpha - step);
        ApplyPose();
    }

    std::string ALeonHorrorDoor::GetInteractPrompt(const APawn& InInstigator) const {
        if (!HasPower() && !bOpen)
            return "No power";
        if (bLocked) {
            const auto* inventory = InventoryOf(InInstigator);
            if (inventory && inventory->HasItem(RequiredKey))
                return "Unlock with " + LeonHorrorItems::DisplayName(RequiredKey);
            return "Locked";
        }
        return bOpen ? "Close door" : "Open door";
    }

    bool ALeonHorrorDoor::Interact(APawn& InInstigator) {
        if (!HasPower() && !bOpen) {
            PlayDoorSound("Locked");
            return false;
        }
        if (bLocked) {
            const auto* inventory = InventoryOf(InInstigator);
            if (!inventory || !inventory->HasItem(RequiredKey)) {
                PlayDoorSound("Locked");
                return false;
            }
            bLocked = false;
            bOpen = true;
            PlayDoorSound("Unlock");
        } else {
            bOpen = !bOpen;
            PlayDoorSound(bOpen ? "Open" : "Close");
        }
        ApplyOpenState(false);
        auto* gameMode = GameModeOf(*this);
        if (gameMode)
            gameMode->NotifyDoorChanged(*this);
        // The other leaf of a double door does what this one did.
        if (ALeonHorrorDoor* partner = FindPartner(); partner && partner != this) {
            partner->bLocked = bLocked;
            partner->bOpen = bOpen;
            partner->ApplyOpenState(false);
            if (gameMode)
                gameMode->NotifyDoorChanged(*partner);
        }
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

    std::string ALeonHorrorNote::GetInteractPrompt(const APawn&) const {
        return "Read " + Title;
    }

    bool ALeonHorrorNote::Interact(APawn&) {
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

    std::string ALeonHorrorSavePoint::GetInteractPrompt(const APawn&) const {
        return "Save";
    }

    bool ALeonHorrorSavePoint::Interact(APawn& InInstigator) {
        auto* gameMode = GameModeOf(*this);
        auto* player = Cast<ALeonHorrorCharacter>(&InInstigator);
        return gameMode && player && gameMode->RequestSave(*player);
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

    std::string ALeonHorrorExit::GetInteractPrompt(const APawn& InInstigator) const {
        const auto* inventory = InventoryOf(InInstigator);
        if (!RequiredKey.empty() && !(inventory && inventory->HasItem(RequiredKey)))
            return "Locked";
        return "Leave";
    }

    bool ALeonHorrorExit::Interact(APawn& InInstigator) {
        const auto* inventory = InventoryOf(InInstigator);
        if (!RequiredKey.empty() && !(inventory && inventory->HasItem(RequiredKey)))
            return false;
        if (auto* gameMode = GameModeOf(*this))
            gameMode->NotifySliceCompleted();
        return true;
    }

} // namespace Leon
