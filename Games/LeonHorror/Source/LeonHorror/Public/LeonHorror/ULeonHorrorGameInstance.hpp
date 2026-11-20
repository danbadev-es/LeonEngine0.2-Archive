#pragma once

#include "Engine/UGameInstance.hpp"

#include "LeonHorror/FLeonHorrorSaveGame.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Leon {

    /** State that survives map travel. Also carries the automated smoke-run request. */
    class ULeonHorrorGameInstance : public UGameInstance {
    public:
        ULeonHorrorGameInstance() = default;
        explicit ULeonHorrorGameInstance(const std::string& InName) : UGameInstance(InName) {}

        /** When positive, the game quits by itself after this many seconds of play (--validate-seconds). */
        void SetValidateSeconds(float InSeconds) { ValidateSeconds = InSeconds; }
        float GetValidateSeconds() const { return ValidateSeconds; }
        /** What the player of a smoke run starts with (--validate-give). */
        void SetValidateItems(const std::vector<std::string>& InItems) { ValidateItems = InItems; }
        const std::vector<std::string>& GetValidateItems() const { return ValidateItems; }
        /** Where the player of a smoke run is stood (--validate-at), if anywhere but the PlayerStart. */
        void SetValidatePlace(float InX, float InZ) { ValidatePlace = std::make_pair(InX, InZ); }
        const std::optional<std::pair<float, float>>& GetValidatePlace() const { return ValidatePlace; }
        /** Where the frame timings of a smoke run are written when it ends (--validate-report); empty: nowhere. */
        void SetValidateReport(const std::string& InPath) { ValidateReport = InPath; }
        const std::string& GetValidateReport() const { return ValidateReport; }

        /** A save waiting to be applied once the map it travels to has begun play. */
        void SetPendingSave(const FLeonHorrorSaveGame& InSave) { PendingSave = InSave; }
        std::optional<FLeonHorrorSaveGame> TakePendingSave() {
            std::optional<FLeonHorrorSaveGame> save = std::move(PendingSave);
            PendingSave.reset();
            return save;
        }

    private:
        std::optional<FLeonHorrorSaveGame> PendingSave;
        std::vector<std::string> ValidateItems;
        std::optional<std::pair<float, float>> ValidatePlace;
        std::string ValidateReport;
        float ValidateSeconds = -1.0f;
    };

} // namespace Leon
