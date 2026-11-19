#include "Launch/FGameLauncher.hpp"

#include "LeonHorror/LeonHorrorModule.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    Leon::FGameLaunchDesc game;
    game.ProjectName = "LeonHorror";
    game.DefaultProjectPath = "Games/LeonHorror/LeonHorror.lproject";
    game.RegisterModule = &Leon::RegisterLeonHorrorModule;
    game.Configure = [](const Leon::FApplicationCommandLineArgs& InArgs, Leon::FGameModuleHooks& InOutHooks) {
        // --validate-seconds=N : play for N seconds and exit with code 0 (automated smoke run).
        // --validate-give=A,B  : in a smoke run, start with these items (Weapon.Pipe, Weapon.Pistol, ...);
        //                        the last weapon named is the one in the hand.
        float validateSeconds = -1.0f;
        std::vector<std::string> validateItems;
        for (int i = 1; i < InArgs.Count; ++i) {
            const std::string arg = InArgs[i] ? InArgs[i] : "";
            if (arg.rfind("--validate-seconds=", 0) == 0)
                validateSeconds = std::strtof(arg.c_str() + 19, nullptr);
            else if (arg.rfind("--validate-give=", 0) == 0) {
                std::string item;
                for (const char c : arg.substr(16) + ",") {
                    if (c != ',') {
                        item += c;
                    } else if (!item.empty()) {
                        validateItems.push_back(item);
                        item.clear();
                    }
                }
            }
        }
        InOutHooks.GameInstanceFactory = [validateSeconds, validateItems]() -> Leon::TRef<Leon::UGameInstance> {
            auto instance = Leon::CreateRef<Leon::ULeonHorrorGameInstance>("LeonHorrorGameInstance");
            instance->SetValidateSeconds(validateSeconds);
            instance->SetValidateItems(validateItems);
            return instance;
        };
    };
    return Leon::LaunchGame(argc, argv, game);
}
