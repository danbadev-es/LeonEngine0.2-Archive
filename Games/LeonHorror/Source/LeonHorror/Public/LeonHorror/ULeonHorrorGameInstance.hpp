#pragma once

#include "Engine/UGameInstance.hpp"

#include "LeonHorror/FLeonHorrorSaveGame.hpp"

#include <optional>

namespace Leon {

    /** State that survives map travel. Also carries the automated smoke-run request. */
    class ULeonHorrorGameInstance : public UGameInstance {
    public:
        ULeonHorrorGameInstance() = default;
        explicit ULeonHorrorGameInstance(const std::string& InName) : UGameInstance(InName) {}

        /** When positive, the game quits by itself after this many seconds of play (--validate-seconds). */
        void SetValidateSeconds(float InSeconds) { ValidateSeconds = InSeconds; }
        float GetValidateSeconds() const { return ValidateSeconds; }

        /** A save waiting to be applied once the map it travels to has begun play. */
        void SetPendingSave(const FLeonHorrorSaveGame& InSave) { PendingSave = InSave; }
        std::optional<FLeonHorrorSaveGame> TakePendingSave() {
            std::optional<FLeonHorrorSaveGame> save = std::move(PendingSave);
            PendingSave.reset();
            return save;
        }

    private:
        std::optional<FLeonHorrorSaveGame> PendingSave;
        float ValidateSeconds = -1.0f;
    };

} // namespace Leon
