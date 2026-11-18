#pragma once

#include "Application/FApplication.hpp"
#include "Engine/IGameModule.hpp"

#include <functional>
#include <string>

namespace Leon {

    /** What a game executable tells the launcher about itself. */
    struct FGameLaunchDesc {
        /** Finds <ProjectName>.lproject next to the executable or in the working directory. */
        std::string ProjectName;
        /** Fallback when nothing else resolves, relative to the working directory. */
        std::string DefaultProjectPath;
        /** Registers the game's classes and hooks. */
        FRegisterGameModuleFn RegisterModule = nullptr;
        /**
         * Optional. Runs after the module registered and before the engine starts: the place to read
         * game-specific command line arguments and to install a game instance factory.
         */
        std::function<void(const FApplicationCommandLineArgs& InArgs, FGameModuleHooks& InOutHooks)> Configure;
    };

    /**
     * The whole of a game's main(): registers the engine backends, the game module, resolves the
     * project file and runs the engine. Returns the process exit code.
     */
    int LaunchGame(int InArgc, char** InArgv, const FGameLaunchDesc& InDesc);

    /**
     * Project file for a game executable, in this order: --project / -project argument, a bare
     * *.lproject argument, the LEON_PROJECT environment variable, a .lproject beside the executable,
     * <ProjectName>.lproject in the working directory, then DefaultProjectPath.
     */
    std::string ResolveGameProjectFile(int InArgc, char** InArgv, const FGameLaunchDesc& InDesc);

} // namespace Leon
