#pragma once

#include "Core/FStringUtils.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ACameraActor.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/ADefaultPawn.hpp"
#include "Gameplay/AGameModeBase.hpp"
#include "Gameplay/AGameStateBase.hpp"
#include "Gameplay/AHUD.hpp"
#include "Gameplay/APlayerCameraManager.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/APlayerStart.hpp"
#include "Gameplay/APlayerState.hpp"
#include "Gameplay/AWorldSettings.hpp"
#include "Scene/Components.hpp"

#include <charconv>
#include <cstdlib>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

namespace Leon {

    /** `Version` written by this build. 2.2 added escaped strings; see QuoteMapString. */
    inline constexpr const char* kMapFormatVersion = "2.2";

    /**
     * Shortest decimal text that parses back to exactly the same float. The default stream precision
     * (6 significant digits) quantised every transform a little more on each save.
     */
    inline std::string FormatMapFloat(float InValue) {
        char buffer[48];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), InValue);
        return std::string(buffer, result.ptr);
    }

    /** Text sink for the map writer: like a stringstream, but floats are written exactly. */
    class FMapTextWriter {
    public:
        FMapTextWriter& operator<<(float InValue) {
            Stream << FormatMapFloat(InValue);
            return *this;
        }
        FMapTextWriter& operator<<(double InValue) {
            Stream << FormatMapFloat(static_cast<float>(InValue));
            return *this;
        }
        FMapTextWriter& operator<<(std::ios_base& (*InManipulator)(std::ios_base&)) {
            Stream << InManipulator;
            return *this;
        }
        template <typename T> FMapTextWriter& operator<<(const T& InValue) {
            Stream << InValue;
            return *this;
        }
        std::string str() const { return Stream.str(); }

    private:
        std::stringstream Stream;
    };

    template <typename TStream> void Indent(TStream& ss, int level) {
        for (int i = 0; i < level; ++i)
            ss << "  ";
    }

    /**
     * Quotes a string value for a .lmap line. The format is line-based, so quotes, backslashes and line
     * breaks inside a name or a text block are escaped (format 2.2+).
     */
    inline std::string QuoteMapString(const std::string& InValue) {
        std::string out;
        out.reserve(InValue.size() + 2);
        out.push_back('"');
        for (char c : InValue) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out.push_back(c);
                break;
            }
        }
        out.push_back('"');
        return out;
    }

    /**
     * Removes surrounding quotes. With bInUnescape (format 2.2+ files) a double-quoted value also has its
     * escapes resolved. Older files are read verbatim: they can hold raw backslashes (Windows paths).
     */
    inline std::string StripQuotes(const std::string& str, bool bInUnescape = false) {
        std::string s = FStringUtils::Trim(str);
        if (s.size() < 2)
            return s;
        const bool bDouble = s.front() == '"' && s.back() == '"';
        const bool bSingle = s.front() == '\'' && s.back() == '\'';
        if (!bDouble && !bSingle)
            return s;
        s = s.substr(1, s.size() - 2);
        if (!bInUnescape || !bDouble)
            return s;

        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != '\\' || i + 1 >= s.size()) {
                out.push_back(s[i]);
                continue;
            }
            const char next = s[++i];
            switch (next) {
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            default: // \" and \\ ; an unknown escape keeps the escaped character
                out.push_back(next);
                break;
            }
        }
        return out;
    }

    /** True when the map text declares format 2.2 or newer, i.e. its quoted strings use escapes. */
    inline bool MapTextUsesEscapes(const std::string& InText) {
        std::stringstream ss(InText);
        std::string line;
        while (std::getline(ss, line)) {
            const std::string trimmed = FStringUtils::Trim(line);
            if (trimmed == "Environment:" || trimmed == "Actors:" || trimmed == "Entities:")
                break;
            if (trimmed.rfind("Version:", 0) != 0)
                continue;
            const std::string value = StripQuotes(trimmed.substr(8));
            char* end = nullptr;
            const long major = std::strtol(value.c_str(), &end, 10);
            long minor = 0;
            if (end && *end == '.')
                minor = std::strtol(end + 1, nullptr, 10);
            return major > 2 || (major == 2 && minor >= 2);
        }
        return false;
    }

    inline std::string LightingQualityToString(ELightingBuildQuality InQuality) {
        switch (InQuality) {
        case ELightingBuildQuality::Preview:
            return "Preview";
        case ELightingBuildQuality::Production:
            return "Production";
        default:
            return "Draft";
        }
    }

    inline ELightingBuildQuality StringToLightingQuality(const std::string& InStr) {
        if (InStr == "Preview")
            return ELightingBuildQuality::Preview;
        if (InStr == "Production")
            return ELightingBuildQuality::Production;
        return ELightingBuildQuality::Draft;
    }

    inline bool IsRuntimeFrameworkActor(const AActor& InActor) {
        // Any controller, not only the player's: AI controllers are created at runtime to drive pawns.
        return dynamic_cast<const AGameModeBase*>(&InActor) || dynamic_cast<const AGameStateBase*>(&InActor) ||
               dynamic_cast<const AController*>(&InActor) || dynamic_cast<const APlayerState*>(&InActor) ||
               dynamic_cast<const AHUD*>(&InActor) || dynamic_cast<const APlayerCameraManager*>(&InActor) ||
               dynamic_cast<const ADefaultPawn*>(&InActor) || dynamic_cast<const ACharacter*>(&InActor) ||
               dynamic_cast<const AWorldSettings*>(&InActor);
    }

    inline void WriteWorldSettings(FMapTextWriter& ss, const FWorldSettingsComponent& ws) {
        Indent(ss, 1);
        ss << "WorldSettings:\n";
        if (!ws.GameModeClass.empty()) {
            Indent(ss, 2);
            ss << "GameModeClass: " << QuoteMapString(ws.GameModeClass) << "\n";
        }
        Indent(ss, 2);
        ss << "StaticLighting: " << (ws.bStaticLighting ? "true" : "false") << "\n";
        Indent(ss, 2);
        ss << "LightingBuildQuality: " << LightingQualityToString(ws.LightingBuildQuality) << "\n";
        Indent(ss, 2);
        ss << "LightmapResolution: " << ws.LightmapResolution << "\n";
        Indent(ss, 2);
        ss << "NumIndirectBounces: " << ws.NumIndirectBounces << "\n";
        Indent(ss, 2);
        ss << "SamplesPerTexel: " << ws.SamplesPerTexel << "\n";
        Indent(ss, 2);
        ss << "IndirectIntensity: " << ws.IndirectIntensity << "\n";
        Indent(ss, 2);
        ss << "AmbientOcclusion: " << (ws.bAmbientOcclusion ? "true" : "false") << "\n";
        Indent(ss, 2);
        ss << "AOIntensity: " << ws.AOIntensity << "\n";
        Indent(ss, 2);
        ss << "AORadius: " << ws.AORadius << "\n";
        Indent(ss, 2);
        ss << "TexelPadding: " << ws.TexelPadding << "\n";
        Indent(ss, 2);
        ss << "WorldScale: " << ws.WorldScale << "\n";
        if (!ws.LightmapAssetPath.empty()) {
            Indent(ss, 2);
            ss << "LightmapAsset: " << QuoteMapString(ws.LightmapAssetPath) << "\n";
        }
        if (ws.LightmapBakeHash != 0) {
            Indent(ss, 2);
            ss << "LightmapBakeHash: \"" << std::hex << ws.LightmapBakeHash << std::dec << "\"\n";
        }
        // Light probes: only what differs from the defaults, so maps that never heard of them stay as they are.
        const FWorldSettingsComponent defaults;
        if (ws.bLightProbes != defaults.bLightProbes) {
            Indent(ss, 2);
            ss << "LightProbes: " << (ws.bLightProbes ? "true" : "false") << "\n";
        }
        if (ws.LightProbeSpacing != defaults.LightProbeSpacing) {
            Indent(ss, 2);
            ss << "LightProbeSpacing: " << ws.LightProbeSpacing << "\n";
        }
        if (ws.LightProbeVerticalSpacing != defaults.LightProbeVerticalSpacing) {
            Indent(ss, 2);
            ss << "LightProbeVerticalSpacing: " << ws.LightProbeVerticalSpacing << "\n";
        }
        if (!ws.LightProbeAssetPath.empty()) {
            Indent(ss, 2);
            ss << "LightProbeAsset: " << QuoteMapString(ws.LightProbeAssetPath) << "\n";
        }
    }

    inline std::vector<float> ParseFloatArray(const std::string& valStr) {
        std::vector<float> result;
        std::string s = FStringUtils::Trim(valStr);
        if (!s.empty() && s.front() == '[' && s.back() == ']') {
            s = s.substr(1, s.size() - 2);
        }
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = FStringUtils::Trim(item);
            if (!item.empty()) {
                // A malformed component used to become 0 silently (a typo moved an actor to the origin).
                // Let it throw: the reader reports the line and refuses the file instead.
                result.push_back(std::stof(item));
            }
        }
        return result;
    }

    /**
     * A list of quoted strings, ["A", "B"], as WriteStringArray writes it. Commas and brackets inside the
     * quotes belong to the strings.
     */
    inline std::vector<std::string> ParseStringArray(const std::string& InValue, bool bInUnescape) {
        std::vector<std::string> result;
        const std::string s = FStringUtils::Trim(InValue);
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != '"')
                continue;
            size_t end = i + 1;
            while (end < s.size() && s[end] != '"') {
                if (s[end] == '\\' && bInUnescape)
                    ++end;
                ++end;
            }
            result.push_back(StripQuotes(s.substr(i, std::min(end, s.size() - 1) - i + 1), bInUnescape));
            i = end;
        }
        return result;
    }

    inline std::string WriteStringArray(const std::vector<std::string>& InValues) {
        std::string out = "[";
        for (size_t i = 0; i < InValues.size(); ++i)
            out += (i > 0 ? ", " : "") + QuoteMapString(InValues[i]);
        return out + "]";
    }

} // namespace Leon
