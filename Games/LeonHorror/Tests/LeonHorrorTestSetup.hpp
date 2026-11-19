#pragma once

#include <doctest/doctest.h>

#include "Assets/UAssetManager.hpp"
#include "Core/FProjectPaths.hpp"
#include "Core/FTimestep.hpp"
#include "Engine/IGameModule.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/UClassRegistry.hpp"

#include "Engine/FMapSerializer.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/LeonHorrorModule.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace Leon::Test {

    inline void BindLeonHorrorProject() {
        const std::string project = FProjectPaths::LocateProjectFile("Games/LeonHorror/LeonHorror.lproject");
        if (project.empty())
            return;
        FProjectPaths::SetProjectRoot(project);
        UAssetManager::SetContentRoot(FProjectPaths::ProjectContentDir());
    }

    /** Registers the game classes through the same entry point the executable and the editor use. */
    inline void RegisterLeonHorrorClasses() {
        FGameModuleHooks hooks;
        RegisterLeonHorrorModule(UClassRegistry::Get(), hooks);
    }

    /** Headless world running the horror game mode: no window, no GPU objects. */
    struct FHorrorWorld {
        TRef<UWorld> World;
        ALeonHorrorGameMode* GM = nullptr;

        explicit FHorrorWorld(bool bInBeginPlay = true) {
            BindLeonHorrorProject();
            RegisterLeonHorrorClasses();
            World = UWorld::Create("HorrorTestWorld");
            GM = World->SpawnActor<ALeonHorrorGameMode>("GM");
            World->SetGameMode(GM);
            World->InitWorld();
            GM->HUDClass = "None";
            if (bInBeginPlay)
                World->BeginPlay();
        }

        void TickSeconds(float InSeconds, float InDt = 1.0f / 60.0f) {
            const int steps = std::max(1, static_cast<int>(InSeconds / InDt + 0.5f));
            for (int i = 0; i < steps; ++i)
                World->Tick(FTimestep(InDt));
        }
    };

    /** Where a map of the project is on disk, from its virtual path (/Game/Maps/Name). */
    inline std::string MapFile(const std::string& InVirtualPath) {
        BindLeonHorrorProject();
        std::string file = FProjectPaths::ResolveVirtualPath(InVirtualPath);
        if (!file.ends_with(".lmap"))
            file += ".lmap";
        return file;
    }

    /** A shipped map loaded into a headless world; optionally with the game running on it. */
    struct FMapWorld {
        TRef<UWorld> World;
        ALeonHorrorGameMode* GM = nullptr;
        ALeonHorrorCharacter* Player = nullptr;

        FMapWorld(const std::string& InVirtualPath, bool bInPlay) {
            BindLeonHorrorProject();
            RegisterLeonHorrorClasses();
            World = UWorld::Create("MapTestWorld");
            REQUIRE(FMapSerializer(World).Deserialize(MapFile(InVirtualPath)));
            if (!bInPlay)
                return;
            GM = World->SpawnActor<ALeonHorrorGameMode>("GM");
            World->SetGameMode(GM);
            World->InitWorld();
            GM->HUDClass = "None";
            GM->SetSaveFilePath("build/LeonHorrorSaves/map_test.lsav");
            World->BeginPlay();
            Player = GM->GetPlayerCharacter();
            REQUIRE(Player);
            Player->SetInjectedInput(FLeonHorrorInputState{});
            Tick(0.5f); // settle on the floor
        }

        void Tick(float InSeconds) {
            const int steps = std::max(1, static_cast<int>(InSeconds * 60.0f + 0.5f));
            for (int i = 0; i < steps; ++i)
                World->Tick(FTimestep(1.0f / 60.0f));
        }

        template <typename T> T* Find(const std::string& InName) const {
            for (const auto& actor : World->GetAllActors()) {
                if (actor && actor->GetName() == InName)
                    return dynamic_cast<T*>(actor.get());
            }
            return nullptr;
        }

        /** Stands the player at a floor position and turns the view to the target. */
        void StandAndLookAt(float InX, float InZ, const glm::vec3& InTarget) {
            Player->SetActorLocation({InX, Player->GetCapsuleHalfHeight() + 0.02f, InZ});
            Tick(0.05f);
            const glm::vec3 to = glm::normalize(InTarget - Player->GetPawnViewLocation());
            const float yaw = glm::degrees(std::atan2(to.z, to.x));
            const float pitch = glm::degrees(std::asin(glm::clamp(to.y, -1.0f, 1.0f)));
            // Whichever sign the engine uses for pitch, take the one that faces the target.
            Player->SetControlRotation({pitch, yaw, 0.0f});
            if (glm::dot(Player->GetControlLookDirection(), to) < 0.99f)
                Player->SetControlRotation({-pitch, yaw, 0.0f});
            Tick(0.05f);
        }

        /** Walks up to something (by teleport), checks it is what the player is looking at, uses it. */
        bool Use(const std::string& InName, float InStandX, float InStandZ) {
            auto* target = Find<ALeonHorrorInteractable>(InName);
            REQUIRE_MESSAGE(target != nullptr, InName);
            StandAndLookAt(InStandX, InStandZ, target->GetInteractLocation());
            CHECK_MESSAGE(Player->FindInteractable() == target, InName);
            return Player->FindInteractable() == target && Player->TryInteract();
        }
    };

} // namespace Leon::Test
