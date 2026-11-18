#include "AssetTool/Commands/PrimitivesCommand.hpp"

#include "AssetPipeline/FEnginePrimitiveImport.hpp"

#include <iostream>

namespace Leon::Tools {

    void FPrimitivesCommand::PrintHelp() const {
        std::cout << "Usage: " << GetUsage() << "\n\n";
        std::cout << "Run from the repository root. Imports Cube, Sphere, Cylinder and Plane from the .fbx next to\n";
        std::cout << "each .lmesh in Engine/Content/Meshes when the FBX is newer, or all of them with --force.\n";
    }

    int FPrimitivesCommand::Execute(const FCommandArgs& InArgs) {
        const FEnginePrimitiveImport::FResult result = FEnginePrimitiveImport::Run(InArgs.HasFlag("force"));
        for (const std::string& path : result.Imported)
            std::cout << "[IMPORTED] " << path << "\n";
        for (const std::string& path : result.UpToDate)
            std::cout << "[UP TO DATE] " << path << "\n";
        for (const std::string& error : result.Errors)
            std::cerr << "[ERROR] " << error << "\n";
        return result.Errors.empty() ? 0 : 1;
    }

} // namespace Leon::Tools
