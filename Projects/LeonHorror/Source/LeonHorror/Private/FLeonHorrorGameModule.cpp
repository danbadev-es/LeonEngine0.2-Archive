#include "LeonHorror/LeonHorrorModule.hpp"

#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

namespace Leon {

    void RegisterLeonHorrorModule(UClassRegistry& InRegistry, FGameModuleHooks& OutHooks) {
        InRegistry.RegisterClass<ALeonHorrorGameMode>("ALeonHorrorGameMode");

        OutHooks.GameInstanceFactory = []() -> TRef<UGameInstance> {
            return CreateRef<ULeonHorrorGameInstance>("LeonHorrorGameInstance");
        };
    }

} // namespace Leon
