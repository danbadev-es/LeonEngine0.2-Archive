#include "Launch/FGameLauncher.hpp"

#include "ULeonTournamentGameInstance.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

LE_GAME_MODULE_EXPORT void LE_RegisterGameModule(Leon::UClassRegistry& InRegistry, Leon::FGameModuleHooks& OutHooks);

int main(int argc, char** argv) {
    Leon::FGameLaunchDesc game;
    game.ProjectName = "LeonTournament";
    game.DefaultProjectPath = "Games/LeonTournament/LeonTournament.lproject";
    game.RegisterModule = &LE_RegisterGameModule;
    game.Configure = [](const Leon::FApplicationCommandLineArgs& InArgs, Leon::FGameModuleHooks& InOutHooks) {
        bool autoOffline = false;
        bool animLab = false;
        float validateSeconds = 65.0f;
        std::string reportPath;
        int32_t botsTeam1 = -1;
        int32_t botsTeam2 = -1;
        for (int i = 1; i < InArgs.Count; ++i) {
            const std::string arg = InArgs[i] ? InArgs[i] : "";
            if (arg == "--offline-match" || arg == "--benchmark")
                autoOffline = true;
            else if (arg == "--anim-lab")
                animLab = true;
            else if (arg.rfind("--validate-seconds=", 0) == 0)
                validateSeconds = std::strtof(arg.c_str() + 19, nullptr);
            else if (arg.rfind("--report=", 0) == 0)
                reportPath = arg.substr(9);
            else if (arg.rfind("--bots=", 0) == 0) {
                const char* v = arg.c_str() + 7;
                char* end = nullptr;
                botsTeam1 = static_cast<int32_t>(std::strtol(v, &end, 10));
                if (end && *end == ',')
                    botsTeam2 = static_cast<int32_t>(std::strtol(end + 1, nullptr, 10));
            }
        }

        if (animLab)
            Leon::UEngine::SetStartupOverrides("/Game/Maps/AnimLab", "ALeonTournamentAnimLabGameMode");

        InOutHooks.GameInstanceFactory = [autoOffline, validateSeconds, reportPath, botsTeam1, botsTeam2]()
            -> Leon::TRef<Leon::UGameInstance> {
            auto gi = Leon::CreateRef<Leon::ULeonTournamentGameInstance>("LeonTournamentGameInstance");
            if (autoOffline)
                gi->ConfigureAutoOfflineMatch(validateSeconds, reportPath);
            if (botsTeam1 >= 0)
                gi->SetDesiredBotsTeam1(botsTeam1);
            if (botsTeam2 >= 0)
                gi->SetDesiredBotsTeam2(botsTeam2);
            return gi;
        };
    };
    return Leon::LaunchGame(argc, argv, game);
}
