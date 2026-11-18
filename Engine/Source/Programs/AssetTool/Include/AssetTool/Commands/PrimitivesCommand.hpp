#pragma once

#include "ToolsCommon/ICommand.hpp"

namespace Leon::Tools {

    class FPrimitivesCommand : public ICommand {
    public:
        [[nodiscard]] std::string GetName() const override { return "primitives"; }
        [[nodiscard]] std::string GetDescription() const override {
            return "Import the engine's primitive meshes from their FBX files into Engine/Content/Meshes.";
        }
        [[nodiscard]] std::string GetUsage() const override { return "AssetTool primitives [--force]"; }

        int Execute(const FCommandArgs& InArgs) override;
        void PrintHelp() const override;
    };

} // namespace Leon::Tools
