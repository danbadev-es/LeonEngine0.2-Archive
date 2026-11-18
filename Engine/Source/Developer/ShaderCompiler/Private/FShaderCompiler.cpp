#include "ShaderCompiler/FShaderCompiler.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// dxcapi.h uses COM types that WIN32_LEAN_AND_MEAN leaves out of windows.h.
#include <unknwn.h>
#include <objidl.h>
#include <oleauto.h>

#include <dxcapi.h>
#include <d3d12shader.h>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Leon {

    namespace {

        using Microsoft::WRL::ComPtr;

        std::wstring Widen(const std::string& InText) {
            if (InText.empty())
                return {};
            const int length = MultiByteToWideChar(CP_UTF8, 0, InText.data(), static_cast<int>(InText.size()), nullptr, 0);
            std::wstring out(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, InText.data(), static_cast<int>(InText.size()), out.data(), length);
            return out;
        }

        bool ReadFile(const std::string& InPath, std::string& OutText) {
            std::ifstream file(InPath, std::ios::in | std::ios::binary);
            if (!file.is_open())
                return false;
            std::stringstream buffer;
            buffer << file.rdbuf();
            OutText = buffer.str();
            return true;
        }

        const wchar_t* Profile(ERHIShaderStage InStage) {
            switch (InStage) {
            case ERHIShaderStage::Vertex:
                return L"vs_6_0";
            case ERHIShaderStage::Pixel:
                return L"ps_6_0";
            }
            return L"vs_6_0";
        }

    } // namespace

    bool FShaderCompiler::Compile(const FShaderCompileInput& InInput, FShaderCompileOutput& OutResult) {
        OutResult.Bytecode.clear();
        OutResult.Messages.clear();

        std::string source = InInput.Source;
        if (source.empty() && !ReadFile(InInput.SourcePath, source)) {
            OutResult.Messages = "cannot read shader file '" + InInput.SourcePath + "'";
            return false;
        }

        ComPtr<IDxcUtils> utils;
        ComPtr<IDxcCompiler3> compiler;
        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
            FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
            OutResult.Messages = "the DirectX Shader Compiler (dxcompiler.dll) could not be loaded";
            return false;
        }
        ComPtr<IDxcIncludeHandler> includeHandler;
        utils->CreateDefaultIncludeHandler(&includeHandler);

        const std::wstring sourceName = Widen(InInput.SourcePath.empty() ? std::string("shader.hlsl") : InInput.SourcePath);
        const std::wstring entry = Widen(InInput.EntryPoint);

        std::vector<std::wstring> owned;
        std::vector<LPCWSTR> arguments;
        const auto add = [&](std::wstring InArgument) { owned.push_back(std::move(InArgument)); };
        add(sourceName);
        add(L"-E");
        add(entry);
        add(L"-T");
        add(Profile(InInput.Stage));
        add(L"-HV");
        add(L"2021");
        add(L"-Zpc"); // column-major matrices: the bytes of a glm::mat4 are the HLSL matrix
        add(L"-WX");  // warnings are errors: a shader that warns is a shader with a bug waiting
        if (InInput.bDebug) {
            add(L"-Zi");
            add(L"-Qembed_debug");
            add(L"-Od");
        } else {
            add(L"-O3");
        }
        if (!InInput.SourcePath.empty()) {
            const std::filesystem::path folder = std::filesystem::path(InInput.SourcePath).parent_path();
            if (!folder.empty()) {
                add(L"-I");
                add(folder.wstring());
            }
        }
        for (const std::string& dir : InInput.IncludeDirs) {
            add(L"-I");
            add(Widen(dir));
        }
        for (const auto& [name, value] : InInput.Defines) {
            add(L"-D");
            add(Widen(value.empty() ? name : name + "=" + value));
        }
        arguments.reserve(owned.size());
        for (const std::wstring& argument : owned)
            arguments.push_back(argument.c_str());

        DxcBuffer buffer{};
        buffer.Ptr = source.data();
        buffer.Size = source.size();
        buffer.Encoding = DXC_CP_UTF8;

        ComPtr<IDxcResult> result;
        if (FAILED(compiler->Compile(&buffer, arguments.data(), static_cast<UINT32>(arguments.size()),
                                     includeHandler.Get(), IID_PPV_ARGS(&result))) ||
            !result) {
            OutResult.Messages = "the shader compiler failed to run";
            return false;
        }

        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0)
            OutResult.Messages.assign(errors->GetStringPointer(), errors->GetStringLength());

        HRESULT status = E_FAIL;
        result->GetStatus(&status);
        if (FAILED(status)) {
            if (OutResult.Messages.empty())
                OutResult.Messages = "compilation failed without a message";
            return false;
        }

        ComPtr<IDxcBlob> object;
        if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || !object ||
            object->GetBufferSize() == 0) {
            OutResult.Messages += "\nthe compiler produced no code";
            return false;
        }
        const auto* bytes = static_cast<const uint8_t*>(object->GetBufferPointer());
        OutResult.Bytecode.assign(bytes, bytes + object->GetBufferSize());

        // What the shader expects in each constant buffer, so tests can hold the C++ side to it.
        ComPtr<IDxcBlob> reflectionBlob;
        if (SUCCEEDED(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr)) && reflectionBlob) {
            DxcBuffer reflectionBuffer{};
            reflectionBuffer.Ptr = reflectionBlob->GetBufferPointer();
            reflectionBuffer.Size = reflectionBlob->GetBufferSize();
            reflectionBuffer.Encoding = DXC_CP_ACP;
            ComPtr<ID3D12ShaderReflection> reflection;
            D3D12_SHADER_DESC shaderDesc{};
            if (SUCCEEDED(utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&reflection))) && reflection &&
                SUCCEEDED(reflection->GetDesc(&shaderDesc))) {
                for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i) {
                    D3D12_SHADER_BUFFER_DESC bufferDesc{};
                    ID3D12ShaderReflectionConstantBuffer* constantBuffer = reflection->GetConstantBufferByIndex(i);
                    if (!constantBuffer || FAILED(constantBuffer->GetDesc(&bufferDesc)) || !bufferDesc.Name)
                        continue;
                    FShaderConstantBuffer reflected;
                    reflected.Name = bufferDesc.Name;
                    reflected.Size = bufferDesc.Size;
                    D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
                    if (SUCCEEDED(reflection->GetResourceBindingDescByName(bufferDesc.Name, &bindDesc)))
                        reflected.Register = bindDesc.BindPoint;
                    OutResult.ConstantBuffers.push_back(std::move(reflected));
                }
            }
        }
        return true;
    }

} // namespace Leon
