#pragma once

#include "Gameplay/AGameModeBase.hpp"

namespace Leon {

    /**
     * Single-player rules for the horror slice: one player, spawned at the map's PlayerStart,
     * playing immediately (no lobby, no match state).
     */
    class ALeonHorrorGameMode : public AGameModeBase {
    public:
        ALeonHorrorGameMode() = default;
        ALeonHorrorGameMode(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "LeonHorrorGameMode");

        void Tick(float DeltaSeconds) override;

        float GetElapsedPlaySeconds() const { return ElapsedPlaySeconds; }

    private:
        void TickSmokeRun();

        float ElapsedPlaySeconds = 0.0f;
        bool bSmokeRunFinished = false;
    };

} // namespace Leon
