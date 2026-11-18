#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Assets/UAssetManager.hpp"
#include "RHI/FRenderer.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

int main(int argc, char** argv) {
    // Tests address engine resources relative to the repo root; fail with a clear message instead of
    // hundreds of unrelated failures when launched from another directory.
    std::error_code ec;
    if (!std::filesystem::exists("Engine/Resources/Shaders", ec)) {
        std::cerr << "RendererTests must be run from the repository root (Engine/Resources/Shaders not found in '"
                  << std::filesystem::current_path(ec).string() << "').\n";
        return 2;
    }
    // Scratch directory used by tests for temporary files; absent on a clean clone.
    std::filesystem::create_directories("build", ec);
    if (ec) {
        std::cerr << "RendererTests could not create the scratch directory 'build': " << ec.message() << "\n";
        return 2;
    }

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    int res = context.run();
    Leon::UAssetManager::Shutdown();
    Leon::FRenderer::Shutdown();
    if (context.shouldExit())
        return res;
    std::quick_exit(res);
}
