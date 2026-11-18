#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Assets/UAssetManager.hpp"
#include "FJoltPhysicsDriver.hpp"
#include "RHI/FRenderer.hpp"

#include "LeonHorrorTestSetup.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    // Content is addressed relative to the repository root.
    std::error_code ec;
    if (!std::filesystem::exists("Projects/LeonHorror/LeonHorror.lproject", ec)) {
        std::cerr << "LeonHorrorTests must be run from the repository root.\n";
        return 2;
    }

    Leon::Test::BindLeonHorrorProject();
    Leon::FJoltPhysicsDriver::Register();
    Leon::Test::RegisterLeonHorrorClasses();

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const int res = context.run();
    Leon::UAssetManager::Shutdown();
    Leon::FRenderer::Shutdown();
    if (context.shouldExit())
        return res;
    std::quick_exit(res);
}
