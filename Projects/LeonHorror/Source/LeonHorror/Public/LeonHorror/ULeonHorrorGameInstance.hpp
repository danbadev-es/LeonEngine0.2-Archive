#pragma once

#include "Engine/UGameInstance.hpp"

namespace Leon {

    /** State that survives map travel. Also carries the automated smoke-run request. */
    class ULeonHorrorGameInstance : public UGameInstance {
    public:
        ULeonHorrorGameInstance() = default;
        explicit ULeonHorrorGameInstance(const std::string& InName) : UGameInstance(InName) {}

        /** When positive, the game quits by itself after this many seconds of play (--validate-seconds). */
        void SetValidateSeconds(float InSeconds) { ValidateSeconds = InSeconds; }
        float GetValidateSeconds() const { return ValidateSeconds; }

    private:
        float ValidateSeconds = -1.0f;
    };

} // namespace Leon
