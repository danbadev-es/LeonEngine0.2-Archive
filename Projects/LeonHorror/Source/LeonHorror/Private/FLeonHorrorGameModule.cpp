#include "LeonHorror/LeonHorrorModule.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorFlashlight.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

namespace Leon {

    void RegisterLeonHorrorModule(UClassRegistry& InRegistry, FGameModuleHooks& OutHooks) {
        InRegistry.RegisterClass<ALeonHorrorGameMode>("ALeonHorrorGameMode");
        InRegistry.RegisterClass<ALeonHorrorCharacter>("ALeonHorrorCharacter");
        InRegistry.RegisterClass<ALeonHorrorFlashlight>("ALeonHorrorFlashlight");

        OutHooks.GameInstanceFactory = []() -> TRef<UGameInstance> {
            return CreateRef<ULeonHorrorGameInstance>("LeonHorrorGameInstance");
        };
    }

} // namespace Leon
