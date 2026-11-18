#include "Launch/FGameLauncher.hpp"

#include "LeonHorror/LeonHorrorModule.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    Leon::FGameLaunchDesc game;
    game.ProjectName = "LeonHorror";
    game.DefaultProjectPath = "Games/LeonHorror/LeonHorror.lproject";
    game.RegisterModule = &Leon::RegisterLeonHorrorModule;
    game.Configure = [](const Leon::FApplicationCommandLineArgs& InArgs, Leon::FGameModuleHooks& InOutHooks) {
        // --validate-seconds=N : play for N seconds and exit with code 0 (automated smoke run).
        float validateSeconds = -1.0f;
        for (int i = 1; i < InArgs.Count; ++i) {
            const std::string arg = InArgs[i] ? InArgs[i] : "";
            if (arg.rfind("--validate-seconds=", 0) == 0)
                validateSeconds = std::strtof(arg.c_str() + 19, nullptr);
        }
        InOutHooks.GameInstanceFactory = [validateSeconds]() -> Leon::TRef<Leon::UGameInstance> {
            auto instance = Leon::CreateRef<Leon::ULeonHorrorGameInstance>("LeonHorrorGameInstance");
            instance->SetValidateSeconds(validateSeconds);
            return instance;
        };
    };
    return Leon::LaunchGame(argc, argv, game);
}
