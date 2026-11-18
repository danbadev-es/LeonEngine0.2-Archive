#pragma once

#include "Gameplay/AHUD.hpp"

#include "LeonHorror/ALeonHorrorGameMode.hpp"

#include <string>
#include <vector>

namespace Leon {

    class ALeonHorrorCharacter;

    /**
     * Everything the HUD shows this frame, as plain data. Built from the game state without touching
     * the renderer, so it can be checked in headless tests; drawing only lays it out.
     */
    struct FLeonHorrorHudModel {
        ELeonHorrorScreen Screen = ELeonHorrorScreen::None;

        // --- In-game overlay (hidden while a full screen is up) ---
        bool bShowStatus = false;
        float HealthPercent = 0.0f;
        float StaminaPercent = 1.0f;
        /** Only drawn while it is not full. */
        bool bShowStamina = false;
        /** "HANDGUN  5 / 12", "STEEL PIPE", or empty when unarmed. */
        std::string WeaponText;
        std::string Prompt;
        bool bShowCrosshair = false;
        /** Short message such as "GAME SAVED". */
        std::string Toast;

        // --- Full screen ---
        std::string ScreenTitle;
        std::vector<std::string> ScreenLines;
        std::string ScreenFooter;
    };

    /** Draws the status overlay and the inventory, pause, note, death and completion screens. */
    class ALeonHorrorHUD : public AHUD {
    public:
        ALeonHorrorHUD() = default;
        ALeonHorrorHUD(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "LeonHorrorHUD");

        void Tick(float DeltaSeconds) override;
        void DrawHUD() override;

        static FLeonHorrorHudModel BuildModel(const ALeonHorrorGameMode& InGameMode,
                                              const ALeonHorrorCharacter* InPlayer,
                                              const std::string& InToast = std::string());
        /** Model for the current world and player; empty when there is no horror game mode. */
        FLeonHorrorHudModel BuildModel() const;

        const std::string& GetToast() const { return Toast; }

        static constexpr float ToastSeconds = 2.5f;
        static constexpr size_t NoteColumns = 56;

    private:
        void DrawModel(const FLeonHorrorHudModel& InModel, float InWidth, float InHeight) const;

        std::string Toast;
        float ToastRemaining = 0.0f;
        int32_t SeenSaveCount = 0;
    };

} // namespace Leon
