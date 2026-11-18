#include "Engine/IGameModule.hpp"
#include "Engine/UEngine.hpp"
#include "Gameplay/UClassRegistry.hpp"
#include "FJoltPhysicsDriver.hpp"
#include "FOpenGLRenderDriver.hpp"

#include "LeonHorror/LeonHorrorModule.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

    std::string ResolveProjectFile(int argc, char** argv) {
        namespace fs = std::filesystem;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i] ? argv[i] : "";
            if (arg.rfind("--project=", 0) == 0)
                return arg.substr(10);
            if (arg.rfind("-project=", 0) == 0)
                return arg.substr(9);
            if ((arg == "--project" || arg == "-project") && i + 1 < argc && argv[i + 1])
                return argv[i + 1];
            if (arg.size() > 9 && arg.rfind(".lproject") == arg.size() - 9)
                return arg;
        }

        if (const char* env = std::getenv("LEON_PROJECT")) {
            if (env[0] != '\0')
                return env;
        }

        std::error_code ec;
        fs::path exeDir = fs::absolute(fs::path(argv[0]).parent_path(), ec);
        if (!ec) {
            for (const auto& entry : fs::directory_iterator(exeDir, ec)) {
                if (entry.path().extension() == ".lproject")
                    return entry.path().string();
            }
        }

        const char* candidates[] = {
            "LeonHorror.lproject",
            "Games/LeonHorror/LeonHorror.lproject",
        };
        for (const char* c : candidates) {
            if (fs::exists(c))
                return c;
        }
        return "Games/LeonHorror/LeonHorror.lproject";
    }

} // namespace

int main(int argc, char** argv) {
    Leon::FOpenGLRenderDriver::Register();
    Leon::FJoltPhysicsDriver::Register();

    Leon::FGameModuleHooks hooks;
    Leon::RegisterLeonHorrorModule(Leon::UClassRegistry::Get(), hooks);

    // --validate-seconds=N : play for N seconds and exit with code 0 (automated smoke run).
    float validateSeconds = -1.0f;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg.rfind("--validate-seconds=", 0) == 0)
            validateSeconds = std::strtof(arg.c_str() + 19, nullptr);
    }

    Leon::UEngine::SetGameInstanceFactory([validateSeconds]() -> Leon::TRef<Leon::UGameInstance> {
        auto instance = Leon::CreateRef<Leon::ULeonHorrorGameInstance>("LeonHorrorGameInstance");
        instance->SetValidateSeconds(validateSeconds);
        return instance;
    });

    Leon::FApplicationCommandLineArgs args{argc, argv};
    return Leon::UEngine::Run(args, ResolveProjectFile(argc, argv));
}
