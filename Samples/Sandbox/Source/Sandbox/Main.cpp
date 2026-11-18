#include "Launch/FGameLauncher.hpp"

LE_GAME_MODULE_EXPORT void LE_RegisterGameModule(Leon::UClassRegistry& InRegistry, Leon::FGameModuleHooks& OutHooks);

int main(int argc, char** argv) {
    Leon::FGameLaunchDesc game;
    game.ProjectName = "Sandbox";
    game.DefaultProjectPath = "Samples/Sandbox/Sandbox.lproject";
    game.RegisterModule = &LE_RegisterGameModule;
    return Leon::LaunchGame(argc, argv, game);
}
