#pragma once

#include "Assets/UAssetManager.hpp"
#include "Core/FProjectPaths.hpp"
#include "Core/FTimestep.hpp"
#include "Engine/IGameModule.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/UClassRegistry.hpp"

#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/LeonHorrorModule.hpp"

#include <algorithm>
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

} // namespace Leon::Test
