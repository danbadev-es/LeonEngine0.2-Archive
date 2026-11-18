#pragma once

#include "Gameplay/AGameModeBase.hpp"

namespace Leon {

    class ALeonHorrorCharacter;

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

        /** The character controlled by the (single) player, or null before login or after travel. */
        ALeonHorrorCharacter* GetPlayerCharacter() const;

        /** Called by the player character when its health reaches zero. */
        void NotifyPlayerDied(ALeonHorrorCharacter& InCharacter);
        bool HasPlayerDied() const { return bPlayerDied; }

    private:
        void TickSmokeRun();

        float ElapsedPlaySeconds = 0.0f;
        bool bSmokeRunFinished = false;
        bool bPlayerDied = false;
    };

} // namespace Leon
