#include "Core/FProjectPaths.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "Core/FAssetPath.hpp"
#include <filesystem>
#include <algorithm>

namespace Leon {

    std::string FProjectPaths::CachedProjectDir = ".";

    void FProjectPaths::SetProjectRoot(const std::string& InProjectFilePathOrDir) {
        std::string norm = FAssetPath::Normalize(InProjectFilePathOrDir);
        if (norm.empty()) {
            CachedProjectDir = ".";
            return;
        }

        // File paths (e.g. MyGame.lproject) resolve to the parent directory
        if (FAssetPath::GetExtension(norm) == "lproject" || FAssetPath::GetExtension(norm) == "ini") {
            CachedProjectDir = FAssetPath::GetDirectory(norm);
        } else {
            CachedProjectDir = norm;
        }
        if (CachedProjectDir.empty()) {
            CachedProjectDir = ".";
        }
    }

    const std::string& FProjectPaths::ProjectDir() {
        return CachedProjectDir;
    }

    std::string FProjectPaths::ProjectContentDir() {
        return FAssetPath::Combine(CachedProjectDir, "Content");
    }

    std::string FProjectPaths::ProjectConfigDir() {
        return FAssetPath::Combine(CachedProjectDir, "Config");
    }

    std::string FProjectPaths::ProjectSourceDir() {
        return FAssetPath::Combine(CachedProjectDir, "Source");
    }

    std::string FProjectPaths::ProjectSavedDir() {
        return FAssetPath::Combine(CachedProjectDir, "Saved");
    }

    std::string FProjectPaths::ProjectIntermediateDir() {
        return FAssetPath::Combine(CachedProjectDir, "Intermediate");
    }

    std::string FProjectPaths::EngineDir() {
        return "Engine";
    }

    std::string FProjectPaths::EngineContentDir() {
        return "Engine/Content";
    }

    std::string FProjectPaths::EngineConfigDir() {
        return "Engine/Config";
    }

    namespace {
        // Absolute paths baked into assets (blend spaces, etc.) may still point at a
        // renamed project's Content/. Keep the path after /Content/ and retarget it.
        std::string ContentRelativeSuffix(const std::string& InNormPath) {
            const std::string marker = "/Content/";
            const size_t pos = InNormPath.find(marker);
            if (pos == std::string::npos)
                return {};
            return InNormPath.substr(pos + marker.size());
        }
    } // namespace

    std::string FProjectPaths::ResolveVirtualPath(const std::string& InVirtualPath) {
        if (InVirtualPath.empty()) {
            return "";
        }

        std::string norm = FAssetPath::Normalize(InVirtualPath);

        if (norm.rfind("/Game/", 0) == 0 || norm.rfind("Game/", 0) == 0) {
            std::string subPath = (norm.rfind("/Game/", 0) == 0) ? norm.substr(6) : norm.substr(5);
            std::string contentDir = ProjectContentDir();
            std::string directCombined = FAssetPath::Combine(contentDir, subPath);

            if (std::filesystem::exists(directCombined)) {
                return directCombined;
            }

            if (FAssetPath::GetExtension(subPath).empty()) {
                const std::vector<std::string> candidateExts = {".lmap", ".lmat", ".lmesh", ".ltex", ".lhdr",
                                                                ".lmi",  ".png",  ".hlsl",  ".wav",  ".ogg"};
                for (const auto& ext : candidateExts) {
                    std::string testPath = directCombined + ext;
                    if (std::filesystem::exists(testPath)) {
                        return testPath;
                    }
                }
            }

            return directCombined;
        }

        if (norm.rfind("/Engine/", 0) == 0 || norm.rfind("Engine/", 0) == 0) {
            std::string subPath = (norm.rfind("/Engine/", 0) == 0) ? norm.substr(8) : norm.substr(7);
            std::string engineAssetsDir = EngineContentDir();
            std::string directCombined = FAssetPath::Combine(engineAssetsDir, subPath);

            if (std::filesystem::exists(directCombined)) {
                return directCombined;
            }

            std::string engineDirect = FAssetPath::Combine(EngineDir(), subPath);
            if (std::filesystem::exists(engineDirect)) {
                return engineDirect;
            }

            if (FAssetPath::GetExtension(subPath).empty()) {
                const std::vector<std::string> candidateExts = {".hlsl", ".ttf", ".bin", ".ltex", ".lmesh", ".lmat"};
                for (const auto& ext : candidateExts) {
                    std::string testPath = directCombined + ext;
                    if (std::filesystem::exists(testPath)) {
                        return testPath;
                    }
                }
            }

            return directCombined;
        }

        if (std::filesystem::exists(norm)) {
            return norm;
        }

        const std::string contentRel = ContentRelativeSuffix(norm);
        if (!contentRel.empty()) {
            return FAssetPath::Combine(ProjectContentDir(), contentRel);
        }

        std::string inContent = FAssetPath::Combine(ProjectContentDir(), norm);
        if (std::filesystem::exists(inContent)) {
            return inContent;
        }

        std::string inEngine = FAssetPath::Combine(EngineContentDir(), norm);
        if (std::filesystem::exists(inEngine)) {
            return inEngine;
        }

        return norm;
    }

    std::string FProjectPaths::MakeVirtualPath(const std::string& InPhysicalPath) {
        std::string norm = FAssetPath::Normalize(InPhysicalPath);
        if (norm.rfind("Game/", 0) == 0)
            return "/" + norm;
        if (norm.rfind("Engine/", 0) == 0)
            return "/" + norm;

        std::string contentDir = FAssetPath::Normalize(ProjectContentDir());
        std::string engineContent = FAssetPath::Normalize(EngineContentDir());

        if (norm.rfind(contentDir, 0) == 0) {
            std::string rel = norm.substr(contentDir.length());
            if (!rel.empty() && rel.front() == '/')
                rel = rel.substr(1);
            return "/Game/" + rel;
        }

        if (norm.rfind(engineContent, 0) == 0) {
            std::string rel = norm.substr(engineContent.length());
            if (!rel.empty() && rel.front() == '/')
                rel = rel.substr(1);
            return "/Engine/" + rel;
        }

        const std::string contentRel = ContentRelativeSuffix(norm);
        if (!contentRel.empty())
            return "/Game/" + contentRel;

        return norm;
    }

    bool FProjectPaths::AdoptPackagedWorkingDirectory(const char* InExecutableArgv0) {
        namespace fs = std::filesystem;
        if (!InExecutableArgv0 || InExecutableArgv0[0] == '\0')
            return false;

        std::error_code ec;
        fs::path exeDir = fs::absolute(fs::path(InExecutableArgv0).parent_path(), ec);
        if (ec || exeDir.empty())
            return false;

        const fs::path packagedResources = exeDir / "Engine" / "Content";
        if (!fs::is_directory(packagedResources, ec))
            return false;

        fs::current_path(exeDir, ec);
        return !ec;
    }

    bool FProjectPaths::AdoptEngineRoot(const char* InExecutableArgv0) {
        namespace fs = std::filesystem;
        if (AdoptPackagedWorkingDirectory(InExecutableArgv0))
            return true;

        std::error_code ec;
        if (fs::is_directory(fs::path("Engine") / "Content", ec))
            return true;
        if (!InExecutableArgv0 || InExecutableArgv0[0] == '\0')
            return false;

        // argv[0] is whatever the parent process typed (possibly a bare name found through PATH);
        // ask the OS where the executable really is.
        fs::path executable = fs::path(InExecutableArgv0);
#ifdef _WIN32
        wchar_t modulePath[4096];
        const unsigned long length = GetModuleFileNameW(nullptr, modulePath, 4096);
        if (length > 0 && length < 4096)
            executable = fs::path(std::wstring(modulePath, length));
#endif
        fs::path cursor = fs::absolute(executable.parent_path(), ec);
        if (ec)
            return false;
        for (int depth = 0; depth < 10 && !cursor.empty(); ++depth) {
            // A missing folder reports an error here; it only means "keep walking up".
            std::error_code probe;
            if (fs::is_directory(cursor / "Engine" / "Content", probe)) {
                fs::current_path(cursor, ec);
                return !ec;
            }
            const fs::path parent = cursor.parent_path();
            if (parent == cursor)
                break;
            cursor = parent;
        }
        return false;
    }

    std::string FProjectPaths::LocateProjectFile(const std::string& InPathOrDir) {
        namespace fs = std::filesystem;

        auto findInDirectory = [](const fs::path& dir) -> std::string {
            if (!fs::is_directory(dir))
                return {};
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == ".lproject") {
                    return FAssetPath::Normalize(entry.path().string());
                }
            }
            return {};
        };

        auto tryPath = [&](const fs::path& candidate) -> std::string {
            if (candidate.empty())
                return {};
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec) && candidate.extension() == ".lproject") {
                return FAssetPath::Normalize(candidate.string());
            }
            if (fs::is_directory(candidate, ec)) {
                return findInDirectory(candidate);
            }
            return {};
        };

        // 1. As-given (cwd-relative or absolute)
        if (!InPathOrDir.empty()) {
            if (auto hit = tryPath(InPathOrDir); !hit.empty())
                return hit;
        }

        // 2. Walk parents: relative hint, then any .lproject beside Engine/ (or under Games/, Samples/)
        fs::path cursor = fs::current_path();
        for (int depth = 0; depth < 8; ++depth) {
            if (!InPathOrDir.empty()) {
                if (auto hit = tryPath(cursor / InPathOrDir); !hit.empty())
                    return hit;
            }
            if (fs::is_directory(cursor / "Engine")) {
                // Prefer Games/*/*.lproject, then Samples/*/*.lproject, without naming a specific game
                for (const char* folder : {"Games", "Samples"}) {
                    const fs::path projectsDir = cursor / folder;
                    if (!fs::is_directory(projectsDir))
                        continue;
                    for (const auto& entry : fs::directory_iterator(projectsDir)) {
                        if (!entry.is_directory())
                            continue;
                        if (auto hit = findInDirectory(entry.path()); !hit.empty())
                            return hit;
                    }
                }
                if (auto hit = findInDirectory(cursor); !hit.empty())
                    return hit;
            }
            if (!cursor.has_parent_path() || cursor == cursor.root_path())
                break;
            cursor = cursor.parent_path();
        }

        return {};
    }

} // namespace Leon
