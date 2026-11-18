#include "LeonHorror/LeonHorrorModule.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorFlashlight.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ALeonHorrorHUD.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ALeonHorrorPlayerController.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

namespace Leon {

    void RegisterLeonHorrorModule(UClassRegistry& InRegistry, FGameModuleHooks& OutHooks) {
        InRegistry.RegisterClass<ALeonHorrorGameMode>("ALeonHorrorGameMode");
        InRegistry.RegisterClass<ALeonHorrorCharacter>("ALeonHorrorCharacter");
        InRegistry.RegisterClass<ALeonHorrorFlashlight>("ALeonHorrorFlashlight");
        InRegistry.RegisterClass<ALeonHorrorPlayerController>("ALeonHorrorPlayerController");
        InRegistry.RegisterClass<ALeonHorrorHUD>("ALeonHorrorHUD");
        InRegistry.RegisterClass<ALeonHorrorEnemy>("ALeonHorrorEnemy");
        InRegistry.RegisterClass<ALeonHorrorEnemyController>("ALeonHorrorEnemyController");
        InRegistry.RegisterClass<ALeonHorrorEnemySpawn>("ALeonHorrorEnemySpawn");
        InRegistry.RegisterClass<ALeonHorrorPickup>("ALeonHorrorPickup");
        InRegistry.RegisterClass<ALeonHorrorDoor>("ALeonHorrorDoor");
        InRegistry.RegisterClass<ALeonHorrorNote>("ALeonHorrorNote");
        InRegistry.RegisterClass<ALeonHorrorSavePoint>("ALeonHorrorSavePoint");
        InRegistry.RegisterClass<ALeonHorrorExit>("ALeonHorrorExit");

        OutHooks.GameInstanceFactory = []() -> TRef<UGameInstance> {
            return CreateRef<ULeonHorrorGameInstance>("LeonHorrorGameInstance");
        };
    }

} // namespace Leon
