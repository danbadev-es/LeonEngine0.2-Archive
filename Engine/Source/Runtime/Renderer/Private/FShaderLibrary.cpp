#include "Renderer/FShaderLibrary.hpp"

#include "Core/FLog.hpp"
#include "ShaderCompiler/FShaderCompiler.hpp"

#include <map>

namespace Leon {

    namespace {

        std::string& Directory() {
            static std::string directory = "Engine/Shaders";
            return directory;
        }

        /** Null entries are failed compilations. */
        std::map<std::string, TRef<IRHIShader>>& Cache() {
            static std::map<std::string, TRef<IRHIShader>> cache;
            return cache;
        }

        std::string MakeKey(const std::string& InFile, const std::string& InEntryPoint, ERHIShaderStage InStage,
                            const FShaderLibrary::FDefines& InDefines) {
            std::string key = InFile + "|" + InEntryPoint + "|" + (InStage == ERHIShaderStage::Vertex ? "vs" : "ps");
            for (const auto& [name, value] : InDefines)
                key += "|" + name + "=" + value;
            return key;
        }

    } // namespace

    TRef<IRHIShader> FShaderLibrary::Get(const std::string& InFile, const std::string& InEntryPoint,
                                         ERHIShaderStage InStage, const FDefines& InDefines) {
        IRHIDevice* device = FRHI::GetDevice();
        if (!device)
            return nullptr;

        const std::string key = MakeKey(InFile, InEntryPoint, InStage, InDefines);
        const auto found = Cache().find(key);
        if (found != Cache().end())
            return found->second;

        FShaderCompileInput input;
        input.SourcePath = Directory() + "/" + InFile;
        input.EntryPoint = InEntryPoint;
        input.Stage = InStage;
        input.Defines = InDefines;
#ifndef NDEBUG
        input.bDebug = true;
#endif
        FShaderCompileOutput output;
        TRef<IRHIShader> shader;
        if (FShaderCompiler::Compile(input, output)) {
            FRHIShaderDesc desc;
            desc.Stage = InStage;
            desc.Bytecode = output.Bytecode.data();
            desc.BytecodeSize = output.Bytecode.size();
            desc.DebugName = InFile + ":" + InEntryPoint;
            shader = device->CreateShader(desc);
        } else {
            LE_CORE_ERROR("Shader '{0}' ({1}) failed to compile:\n{2}", input.SourcePath, InEntryPoint, output.Messages);
        }
        Cache()[key] = shader;
        return shader;
    }

    void FShaderLibrary::Clear() {
        Cache().clear();
    }

    void FShaderLibrary::SetShaderDirectory(const std::string& InDirectory) {
        Directory() = InDirectory;
        Clear();
    }

    const std::string& FShaderLibrary::GetShaderDirectory() {
        return Directory();
    }

} // namespace Leon
