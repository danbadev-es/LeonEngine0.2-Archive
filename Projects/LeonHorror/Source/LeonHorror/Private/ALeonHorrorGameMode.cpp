#include "LeonHorror/ALeonHorrorGameMode.hpp"

#include "LeonHorror/ULeonHorrorGameInstance.hpp"

#include "Core/FApplication.hpp"
#include "Core/FLog.hpp"
#include "Engine/UEngine.hpp"

namespace Leon {

    ALeonHorrorGameMode::ALeonHorrorGameMode(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AGameModeBase(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorGameMode");
        DefaultPawnClass = "ACharacter";
        PlayerControllerClass = "APlayerController";
        HUDClass = "AHUD";
        GameStateClass = "AGameStateBase";
        PlayerStateClass = "APlayerState";
        MaxPlayers = 1;
        SetCanEverTick(true);
    }

    void ALeonHorrorGameMode::Tick(float DeltaSeconds) {
        AGameModeBase::Tick(DeltaSeconds);
        ElapsedPlaySeconds += DeltaSeconds;
        TickSmokeRun();
    }

    void ALeonHorrorGameMode::TickSmokeRun() {
        // Only meaningful in the real executable; headless tests have no application or engine.
        if (bSmokeRunFinished || !FApplication::HasInstance())
            return;
        auto instance = std::dynamic_pointer_cast<ULeonHorrorGameInstance>(UEngine::Get().GetGameInstance());
        if (!instance || instance->GetValidateSeconds() <= 0.0f)
            return;
        if (ElapsedPlaySeconds < instance->GetValidateSeconds())
            return;
        bSmokeRunFinished = true;
        LE_CORE_INFO("LeonHorror: smoke run finished after {0} s, closing.", ElapsedPlaySeconds);
        FApplication::Get().Close();
    }

} // namespace Leon
