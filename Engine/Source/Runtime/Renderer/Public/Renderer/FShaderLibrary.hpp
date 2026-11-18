#pragma once

#include "RHI/IRHIDevice.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Leon {

    /**
     * Compiled engine shaders, by file and entry point.
     *
     * Shaders are HLSL files under Engine/Shaders. They are compiled the first time they are asked
     * for and kept until Clear, so asking again is cheap.
     */
    class FShaderLibrary {
    public:
        using FDefines = std::vector<std::pair<std::string, std::string>>;

        /**
         * InFile is relative to the shader folder ("PBR_Lit.hlsl"). Returns null, after logging the
         * compiler's messages, when there is no device or the shader does not compile; the failure
         * is remembered so a broken shader is reported once, not every frame.
         */
        static TRef<IRHIShader> Get(const std::string& InFile, const std::string& InEntryPoint, ERHIShaderStage InStage,
                                    const FDefines& InDefines = {});

        /** Drops every shader, so the next Get recompiles from disk. Call before the device goes away. */
        static void Clear();

        /** Folder the files are read from. Defaults to Engine/Shaders. */
        static void SetShaderDirectory(const std::string& InDirectory);
        static const std::string& GetShaderDirectory();
    };

} // namespace Leon
