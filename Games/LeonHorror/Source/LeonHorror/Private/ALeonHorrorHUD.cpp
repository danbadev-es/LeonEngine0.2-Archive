#include "LeonHorror/ALeonHorrorHUD.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Core/FApplication.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/FOnScreenDebugMessage.hpp"
#include "Gameplay/UHealthComponent.hpp"
#include "UMG/FUIRenderer.hpp"

#include <algorithm>

namespace Leon {

    ALeonHorrorHUD::ALeonHorrorHUD(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AHUD(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorHUD");
    }

    void ALeonHorrorHUD::Tick(float DeltaSeconds) {
        AHUD::Tick(DeltaSeconds);
        const auto* gameMode = World ? dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode()) : nullptr;
        if (gameMode && gameMode->GetSaveCount() != SeenSaveCount) {
            SeenSaveCount = gameMode->GetSaveCount();
            Toast = "GAME SAVED";
            ToastRemaining = ToastSeconds;
        }
        if (ToastRemaining > 0.0f) {
            ToastRemaining -= DeltaSeconds;
            if (ToastRemaining <= 0.0f)
                Toast.clear();
        }
    }

    FLeonHorrorHudModel ALeonHorrorHUD::BuildModel() const {
        const auto* gameMode = World ? dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode()) : nullptr;
        if (!gameMode)
            return {};
        const auto* player = PlayerController ? dynamic_cast<ALeonHorrorCharacter*>(PlayerController->GetPawn())
                                              : gameMode->GetPlayerCharacter();
        return BuildModel(*gameMode, player, Toast);
    }

    FLeonHorrorHudModel ALeonHorrorHUD::BuildModel(const ALeonHorrorGameMode& InGameMode,
                                                   const ALeonHorrorCharacter* InPlayer, const std::string& InToast) {
        FLeonHorrorHudModel model;
        model.Screen = InGameMode.GetActiveScreen();
        model.Toast = InToast;

        if (InPlayer) {
            if (const UHealthComponent* health = InPlayer->GetHealth()) {
                const float maxHealth = std::max(health->GetMaxHealth(), 1.0f);
                model.HealthPercent = std::clamp(health->GetHealth() / maxHealth, 0.0f, 1.0f);
            }
            const float maxStamina = std::max(InPlayer->Tuning.StaminaMax, 1.0f);
            model.StaminaPercent = std::clamp(InPlayer->GetStamina() / maxStamina, 0.0f, 1.0f);
            model.bShowStamina = model.StaminaPercent < 0.999f;
            if (const auto* combat = InPlayer->GetCombat()) {
                switch (combat->GetEquipped()) {
                case ELeonHorrorWeapon::Pipe:
                    model.WeaponText = "STEEL PIPE";
                    break;
                case ELeonHorrorWeapon::Pistol:
                    model.WeaponText = "HANDGUN  " + std::to_string(combat->GetLoadedRounds()) + " / " +
                                       std::to_string(combat->GetSpareRounds());
                    break;
                case ELeonHorrorWeapon::None:
                    break;
                }
            }
        }

        switch (model.Screen) {
        case ELeonHorrorScreen::None:
            model.bShowStatus = InPlayer != nullptr;
            if (InPlayer) {
                model.Prompt = InPlayer->GetInteractPrompt();
                model.bShowCrosshair = InPlayer->IsAiming();
            }
            break;
        case ELeonHorrorScreen::Inventory:
            model.ScreenTitle = "INVENTORY";
            if (const auto* inventory = InPlayer ? InPlayer->GetInventory() : nullptr) {
                for (const auto& [item, count] : inventory->GetItems()) {
                    std::string line = LeonHorrorItems::DisplayName(item);
                    if (count > 1)
                        line += "  x" + std::to_string(count);
                    model.ScreenLines.push_back(line);
                }
            }
            if (model.ScreenLines.empty())
                model.ScreenLines.push_back("(empty)");
            model.ScreenLines.push_back("");
            model.ScreenLines.push_back("Health  " + std::to_string(static_cast<int>(model.HealthPercent * 100.0f + 0.5f)) +
                                        "%");
            model.ScreenFooter = "[H] Use first aid kit    [Tab] Close";
            break;
        case ELeonHorrorScreen::Pause:
            model.ScreenTitle = "PAUSED";
            model.ScreenLines = {
                "WASD  Move          Shift  Run",
                "Mouse  Look         F  Flashlight",
                "E  Interact         Tab  Inventory",
                "1 / 2  Pipe / Handgun",
                "Right mouse  Aim    Left mouse  Attack",
                "R  Reload           H  First aid kit",
            };
            model.ScreenFooter = "[Esc] Resume    [L] Load last save    [Q] Quit";
            break;
        case ELeonHorrorScreen::Note:
            model.ScreenTitle = InGameMode.GetNoteTitle();
            model.ScreenLines = ALeonHorrorNote::WrapText(InGameMode.GetNoteText(), NoteColumns);
            model.ScreenFooter = "[E] Close";
            break;
        case ELeonHorrorScreen::Dead:
            model.ScreenTitle = "YOU DIED";
            model.ScreenFooter = InGameMode.HasSaveFile() ? "[E] Load the last save" : "[E] Start again";
            break;
        case ELeonHorrorScreen::Completed:
            model.ScreenTitle = "END OF THE PROTOTYPE";
            model.ScreenLines = {"You made it out.", "Thank you for playing."};
            model.ScreenFooter = "[E] Play again";
            break;
        }
        return model;
    }

    void ALeonHorrorHUD::DrawHUD() {
        if (!FApplication::HasInstance())
            return;
        const uint32_t width = FApplication::Get().GetWindow().GetWidth();
        const uint32_t height = FApplication::Get().GetWindow().GetHeight();
        if (width == 0 || height == 0)
            return;

        FUIRenderer::Begin(width, height);
        DrawModel(BuildModel(), static_cast<float>(width), static_cast<float>(height));

        FGeometry root;
        root.Size = {static_cast<float>(width), static_cast<float>(height)};
        for (auto& widget : ViewportWidgets) {
            if (widget && widget->IsVisible())
                widget->Paint(root);
        }
        FOnScreenDebugMessageManager::Get().Draw(static_cast<float>(width), static_cast<float>(height));
        FUIRenderer::End();
    }

    void ALeonHorrorHUD::DrawModel(const FLeonHorrorHudModel& InModel, float InWidth, float InHeight) const {
        // Authored for 720 lines; everything scales with the window height.
        const float ui = std::max(InHeight / 720.0f, 0.5f);
        const glm::vec4 text{0.86f, 0.84f, 0.78f, 1.0f};
        const glm::vec4 dim{0.60f, 0.58f, 0.54f, 1.0f};
        const glm::vec4 shadow{0.0f, 0.0f, 0.0f, 0.75f};
        const auto drawText = [&](float InX, float InY, const std::string& InText, const glm::vec4& InColor,
                                  float InScale, ETextAlignment InAlign) {
            if (InText.empty())
                return;
            FUIRenderer::DrawString(InX + ui, InY + ui, InText, shadow, InScale, InAlign);
            FUIRenderer::DrawString(InX, InY, InText, InColor, InScale, InAlign);
        };
        const auto lineHeight = [](float InScale) { return FUIRenderer::MeasureString("Ag", InScale).y * 1.45f; };

        if (InModel.bShowStatus) {
            const float margin = 28.0f * ui;
            const float barWidth = 220.0f * ui;
            const float barHeight = 10.0f * ui;
            const glm::vec2 barMin{margin, InHeight - margin - barHeight};
            const glm::vec4 healthy{0.45f, 0.62f, 0.42f, 0.95f};
            const glm::vec4 hurt{0.70f, 0.16f, 0.12f, 0.95f};
            const glm::vec4 fill = glm::mix(hurt, healthy, InModel.HealthPercent);
            FUIRenderer::DrawBorderQuad(barMin, barMin + glm::vec2{barWidth, barHeight}, {0.0f, 0.0f, 0.0f, 0.55f},
                                        {0.0f, 0.0f, 0.0f, 0.85f}, ui);
            if (InModel.HealthPercent > 0.0f)
                FUIRenderer::DrawQuad(barMin, barMin + glm::vec2{barWidth * InModel.HealthPercent, barHeight}, fill);
            if (InModel.bShowStamina) {
                const glm::vec2 staminaMin{margin, barMin.y - 8.0f * ui};
                FUIRenderer::DrawQuad(staminaMin, staminaMin + glm::vec2{barWidth, 4.0f * ui}, {0.0f, 0.0f, 0.0f, 0.5f});
                FUIRenderer::DrawQuad(staminaMin,
                                      staminaMin + glm::vec2{barWidth * InModel.StaminaPercent, 4.0f * ui},
                                      {0.72f, 0.70f, 0.60f, 0.9f});
            }
            const float weaponScale = 1.1f * ui;
            drawText(InWidth - margin, InHeight - margin - lineHeight(weaponScale) * 0.7f, InModel.WeaponText, text,
                     weaponScale, ETextAlignment::Right);
            drawText(InWidth * 0.5f, InHeight * 0.68f, InModel.Prompt, text, 1.15f * ui, ETextAlignment::Center);
            if (InModel.bShowCrosshair) {
                const glm::vec2 centre{InWidth * 0.5f, InHeight * 0.5f};
                const float half = 1.5f * ui;
                FUIRenderer::DrawQuad(centre - glm::vec2{half + ui}, centre + glm::vec2{half + ui}, shadow);
                FUIRenderer::DrawQuad(centre - glm::vec2{half}, centre + glm::vec2{half}, {0.92f, 0.90f, 0.84f, 0.9f});
            }
        }

        if (InModel.Screen != ELeonHorrorScreen::None) {
            const bool bDead = InModel.Screen == ELeonHorrorScreen::Dead;
            const glm::vec4 veil = bDead ? glm::vec4{0.16f, 0.0f, 0.0f, 0.62f} : glm::vec4{0.0f, 0.0f, 0.0f, 0.78f};
            FUIRenderer::DrawQuad({0.0f, 0.0f}, {InWidth, InHeight}, veil);

            const float titleScale = 2.2f * ui;
            const float bodyScale = 1.15f * ui;
            const float bodyStep = lineHeight(bodyScale);
            const float blockHeight = lineHeight(titleScale) * 1.4f + bodyStep * static_cast<float>(InModel.ScreenLines.size());
            float y = std::max(InHeight * 0.5f - blockHeight * 0.5f, 60.0f * ui);
            drawText(InWidth * 0.5f, y, InModel.ScreenTitle, bDead ? glm::vec4{0.80f, 0.12f, 0.10f, 1.0f} : text,
                     titleScale, ETextAlignment::Center);
            y += lineHeight(titleScale) * 1.4f;
            for (const std::string& line : InModel.ScreenLines) {
                drawText(InWidth * 0.5f, y, line, text, bodyScale, ETextAlignment::Center);
                y += bodyStep;
            }
            drawText(InWidth * 0.5f, InHeight - 70.0f * ui, InModel.ScreenFooter, dim, 1.0f * ui, ETextAlignment::Center);
        }

        if (!InModel.Toast.empty())
            drawText(InWidth * 0.5f, 48.0f * ui, InModel.Toast, text, 1.2f * ui, ETextAlignment::Center);
    }

} // namespace Leon
