#include "RenderCore/FShaderLibrary.hpp"

#include "Core/FLog.hpp"
#include "ShaderCompiler/FShaderCompiler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace Leon {

    namespace {

        namespace fs = std::filesystem;

        /** Bump when the compiler's arguments change: everything cached before stops matching. */
        constexpr uint64_t kCacheVersion = 1;
        const char* const kCacheDirectory = "Saved/ShaderCache";

        std::string& Directory() {
            static std::string directory = "Engine/Shaders";
            return directory;
        }

        /** Null entries are failed compilations. */
        std::map<std::string, TRef<IRHIShader>>& Cache() {
            static std::map<std::string, TRef<IRHIShader>> cache;
            return cache;
        }

        uint64_t& Generation() {
            static uint64_t generation = 1;
            return generation;
        }

        /** Hash of the shader sources; 0 = not computed since the last Clear. */
        uint64_t& SourceHash() {
            static uint64_t hash = 0;
            return hash;
        }

        /** What the folder looked like at the last ReloadIfSourcesChanged; 0 = never looked. */
        uint64_t& FolderStamp() {
            static uint64_t stamp = 0;
            return stamp;
        }

        uint64_t HashBytes(uint64_t InHash, const void* InData, size_t InSize) {
            const auto* bytes = static_cast<const unsigned char*>(InData);
            for (size_t i = 0; i < InSize; ++i) {
                InHash ^= bytes[i];
                InHash *= 1099511628211ull; // FNV-1a
            }
            return InHash;
        }

        uint64_t HashString(uint64_t InHash, const std::string& InText) {
            return HashBytes(HashBytes(InHash, InText.data(), InText.size()), "\0", 1);
        }

        std::vector<fs::path> SourceFiles() {
            std::vector<fs::path> files;
            std::error_code ec;
            for (const fs::directory_entry& entry : fs::directory_iterator(Directory(), ec)) {
                const std::string extension = entry.path().extension().string();
                if (entry.is_regular_file(ec) && (extension == ".hlsl" || extension == ".hlsli"))
                    files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        /**
         * Every file in the folder, names and contents. A shader's result depends on the files it
         * includes; hashing them all is simpler than tracking which, and they are a few kilobytes.
         */
        uint64_t ComputeSourceHash() {
            uint64_t hash = 14695981039346656037ull;
            for (const fs::path& file : SourceFiles()) {
                hash = HashString(hash, file.filename().string());
                std::ifstream stream(file, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                hash = HashString(hash, text);
            }
            return hash == 0 ? 1 : hash;
        }

        /** Names, sizes and modification times: changes when a file is added, removed or saved. */
        uint64_t ComputeFolderStamp() {
            uint64_t hash = 14695981039346656037ull;
            std::error_code ec;
            for (const fs::path& file : SourceFiles()) {
                hash = HashString(hash, file.filename().string());
                const auto size = static_cast<uint64_t>(fs::file_size(file, ec));
                const auto time = static_cast<int64_t>(fs::last_write_time(file, ec).time_since_epoch().count());
                hash = HashBytes(hash, &size, sizeof(size));
                hash = HashBytes(hash, &time, sizeof(time));
            }
            return hash == 0 ? 1 : hash;
        }

        std::string MakeKey(const std::string& InFile, const std::string& InEntryPoint, ERHIShaderStage InStage,
                            const FShaderLibrary::FDefines& InDefines) {
            std::string key = InFile + "|" + InEntryPoint + "|" + (InStage == ERHIShaderStage::Vertex ? "vs" : "ps");
            for (const auto& [name, value] : InDefines)
                key += "|" + name + "=" + value;
            return key;
        }

        bool DiskCacheEnabled() {
            static const bool bEnabled = std::getenv("LEON_NO_SHADER_CACHE") == nullptr;
            return bEnabled;
        }

        fs::path CacheFile(const std::string& InKey, bool bInDebug) {
            if (SourceHash() == 0)
                SourceHash() = ComputeSourceHash();
            uint64_t hash = HashBytes(14695981039346656037ull, &kCacheVersion, sizeof(kCacheVersion));
            hash = HashBytes(hash, &SourceHash(), sizeof(uint64_t));
            hash = HashString(hash, InKey);
            hash = HashBytes(hash, &bInDebug, sizeof(bInDebug));
            char name[32];
            std::snprintf(name, sizeof(name), "%016llx.dxil", static_cast<unsigned long long>(hash));
            return fs::path(kCacheDirectory) / name;
        }

        /** A compiled shader is a DXBC container; anything else is a file cut short or not ours. */
        bool LooksLikeBytecode(const std::vector<uint8_t>& InBytes) {
            return InBytes.size() > 32 && InBytes[0] == 'D' && InBytes[1] == 'X' && InBytes[2] == 'B' && InBytes[3] == 'C';
        }

        bool ReadCache(const fs::path& InFile, std::vector<uint8_t>& OutBytes) {
            std::ifstream stream(InFile, std::ios::binary);
            if (!stream)
                return false;
            OutBytes.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            return LooksLikeBytecode(OutBytes);
        }

        void WriteCache(const fs::path& InFile, const std::vector<uint8_t>& InBytes) {
            std::error_code ec;
            fs::create_directories(InFile.parent_path(), ec);
            // Written under another name and renamed: a run that is killed half-way leaves no entry.
            fs::path temporary = InFile;
            temporary += ".tmp";
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                if (!stream)
                    return;
                stream.write(reinterpret_cast<const char*>(InBytes.data()), static_cast<std::streamsize>(InBytes.size()));
                if (!stream)
                    return;
            }
            fs::rename(temporary, InFile, ec);
            if (ec)
                fs::remove(temporary, ec);
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

        std::vector<uint8_t> bytecode;
        fs::path cacheFile;
        bool bFromDisk = false;
        if (DiskCacheEnabled()) {
            cacheFile = CacheFile(key, input.bDebug);
            bFromDisk = ReadCache(cacheFile, bytecode);
        }
        if (!bFromDisk) {
            FShaderCompileOutput output;
            if (FShaderCompiler::Compile(input, output)) {
                bytecode = std::move(output.Bytecode);
                if (DiskCacheEnabled())
                    WriteCache(cacheFile, bytecode);
            } else {
                bytecode.clear();
                LE_CORE_ERROR("Shader '{0}' ({1}) failed to compile:\n{2}", input.SourcePath, InEntryPoint, output.Messages);
            }
        }

        TRef<IRHIShader> shader;
        if (!bytecode.empty()) {
            FRHIShaderDesc desc;
            desc.Stage = InStage;
            desc.Bytecode = bytecode.data();
            desc.BytecodeSize = bytecode.size();
            desc.DebugName = InFile + ":" + InEntryPoint;
            shader = device->CreateShader(desc);
        }
        Cache()[key] = shader;
        return shader;
    }

    void FShaderLibrary::Clear() {
        Cache().clear();
        SourceHash() = 0;
        ++Generation();
    }

    uint64_t FShaderLibrary::GetGeneration() {
        return Generation();
    }

    bool FShaderLibrary::ReloadIfSourcesChanged() {
        const uint64_t stamp = ComputeFolderStamp();
        const bool bChanged = FolderStamp() != 0 && stamp != FolderStamp();
        FolderStamp() = stamp;
        if (bChanged) {
            LE_CORE_INFO("Shaders changed on disk: reloading");
            Clear();
        }
        return bChanged;
    }

    void FShaderLibrary::SetShaderDirectory(const std::string& InDirectory) {
        Directory() = InDirectory;
        FolderStamp() = 0;
        Clear();
    }

    const std::string& FShaderLibrary::GetShaderDirectory() {
        return Directory();
    }

} // namespace Leon
