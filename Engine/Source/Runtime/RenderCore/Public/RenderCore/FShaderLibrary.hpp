#pragma once

#include <cstdint>
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
     *
     * Compiled code is also kept on disk (Saved/ShaderCache, under the working directory), named by a
     * hash of every file in the shader folder and of what was asked for, so a run after the first
     * does not compile anything and an edited shader never matches a stale entry. Set
     * LEON_NO_SHADER_CACHE to compile every time.
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

        /** Drops every shader, so the next Get reads them again. Call before the device goes away. */
        static void Clear();

        /**
         * Changes whenever the shaders are dropped. Whoever keeps pipelines built from them compares
         * it with the value it last saw and rebuilds when it differs.
         */
        static uint64_t GetGeneration();

        /**
         * Looks at the shader folder and, if a file was added, removed or saved since the last call,
         * drops every shader (Clear). Returns true when it did. Cheap enough to call a few times a
         * second while developing; the application does, in builds with assertions.
         */
        static bool ReloadIfSourcesChanged();

        /** Folder the files are read from. Defaults to Engine/Shaders. */
        static void SetShaderDirectory(const std::string& InDirectory);
        static const std::string& GetShaderDirectory();
    };

} // namespace Leon
