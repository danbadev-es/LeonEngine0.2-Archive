#include "Launch/FGameLauncher.hpp"

#include "Engine/UEngine.hpp"
#include "Gameplay/UClassRegistry.hpp"

// The backends this build of the engine ships with. Games never name them.
#include "FJoltPhysicsDriver.hpp"
#include "FOpenGLRenderDriver.hpp"

#include <cstdlib>
#include <filesystem>

namespace Leon {

    std::string ResolveGameProjectFile(int InArgc, char** InArgv, const FGameLaunchDesc& InDesc) {
        namespace fs = std::filesystem;

        for (int i = 1; i < InArgc; ++i) {
            const std::string arg = InArgv[i] ? InArgv[i] : "";
            if (arg.rfind("--project=", 0) == 0)
                return arg.substr(10);
            if (arg.rfind("-project=", 0) == 0)
                return arg.substr(9);
            if ((arg == "--project" || arg == "-project") && i + 1 < InArgc && InArgv[i + 1])
                return InArgv[i + 1];
            if (arg.size() > 9 && arg.rfind(".lproject") == arg.size() - 9)
                return arg;
        }

        if (const char* env = std::getenv("LEON_PROJECT")) {
            if (env[0] != '\0')
                return env;
        }

        std::error_code ec;
        if (InArgc > 0 && InArgv[0]) {
            const fs::path exeDir = fs::absolute(fs::path(InArgv[0]).parent_path(), ec);
            if (!ec) {
                for (const auto& entry : fs::directory_iterator(exeDir, ec)) {
                    if (entry.path().extension() == ".lproject")
                        return entry.path().string();
                }
            }
        }

        const std::string beside = InDesc.ProjectName + ".lproject";
        if (!InDesc.ProjectName.empty() && fs::exists(beside, ec))
            return beside;
        return InDesc.DefaultProjectPath;
    }

    int LaunchGame(int InArgc, char** InArgv, const FGameLaunchDesc& InDesc) {
        FOpenGLRenderDriver::Register();
        FJoltPhysicsDriver::Register();

        FGameModuleHooks hooks;
        if (InDesc.RegisterModule)
            InDesc.RegisterModule(UClassRegistry::Get(), hooks);
        if (hooks.TransportFactorySetup)
            hooks.TransportFactorySetup();

        const FApplicationCommandLineArgs args{InArgc, InArgv};
        if (InDesc.Configure)
            InDesc.Configure(args, hooks);
        if (hooks.GameInstanceFactory)
            UEngine::SetGameInstanceFactory(hooks.GameInstanceFactory);

        return UEngine::Run(args, ResolveGameProjectFile(InArgc, InArgv, InDesc));
    }

} // namespace Leon
