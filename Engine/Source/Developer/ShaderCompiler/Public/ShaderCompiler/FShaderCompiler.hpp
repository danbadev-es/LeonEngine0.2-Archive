#pragma once

#include "RHI/RHITypes.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Leon {

    struct FShaderCompileInput {
        /**
         * Path of the HLSL file. Read from disk when Source is empty; always used to resolve
         * `#include "..."` (relative to this file) and to label error messages.
         */
        std::string SourcePath;
        /** HLSL text. Leave empty to compile the file at SourcePath. */
        std::string Source;
        std::string EntryPoint = "main";
        ERHIShaderStage Stage = ERHIShaderStage::Vertex;
        /** Preprocessor definitions as name/value; an empty value defines the name as 1. */
        std::vector<std::pair<std::string, std::string>> Defines;
        /** Extra folders searched for includes. */
        std::vector<std::string> IncludeDirs;
        /** Keep debug information and skip optimisation (readable in GPU debuggers). */
        bool bDebug = false;
    };

    /** A constant buffer as the compiled shader sees it. */
    struct FShaderConstantBuffer {
        std::string Name;
        /** The b<n> register it is bound to. */
        uint32_t Register = 0;
        /** Bytes the shader expects there, padded to 16. The C++ structure that fills it must match. */
        uint32_t Size = 0;
    };

    struct FShaderCompileOutput {
        /** DXIL, ready for IRHIDevice::CreateShader. Empty on failure. */
        std::vector<uint8_t> Bytecode;
        /** The constant buffers the entry point uses (ones it never reads are not listed). */
        std::vector<FShaderConstantBuffer> ConstantBuffers;
        /** Compiler errors and warnings, with file and line. */
        std::string Messages;
    };

    /**
     * HLSL to DXIL through the DirectX Shader Compiler.
     *
     * Conventions every engine shader relies on:
     *  - Matrices are column-major in memory, exactly as glm stores them, and vectors are columns:
     *    write `mul(Matrix, Vector)`.
     *  - Vertex inputs use the semantic `ATTRIBUTE<n>`, n being the position in the vertex layout.
     */
    class FShaderCompiler {
    public:
        /** Returns false and fills OutResult.Messages when the shader does not compile. */
        static bool Compile(const FShaderCompileInput& InInput, FShaderCompileOutput& OutResult);
    };

} // namespace Leon
