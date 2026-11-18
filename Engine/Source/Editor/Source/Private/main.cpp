#include "Editor/FEditorApp.hpp"

#include "FJoltPhysicsDriver.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHID3D12/FD3D12RHI.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        // The editor draws; register what a drawing executable needs before the application starts.
        Leon::FLegacyRenderDriver::Register();
        Leon::FD3D12RHI::Register();
        Leon::FJoltPhysicsDriver::Register();

        Leon::FApplicationCommandLineArgs Args{argc, argv};
        Leon::Editor::FEditorApp App(Args);
        App.Run();
        return 0;
    } catch (const std::exception& Ex) {
        std::cerr << "Fatal: " << Ex.what() << '\n';
        return 1;
    }
}
