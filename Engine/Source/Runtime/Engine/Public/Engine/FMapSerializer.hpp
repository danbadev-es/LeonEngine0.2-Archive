#pragma once

#include "Core/Base.hpp"
#include "Engine/UWorld.hpp"

#include <string>

namespace Leon {

    /**
     * @brief Unreal Engine aligned FMapSerializer for saving and loading .lmap files into UWorld.
     */
    class FMapSerializer {
    public:
        explicit FMapSerializer(const TRef<UWorld>& InWorld);

        bool Serialize(const std::string& InFilePath);
        bool SerializeText(std::string& OutYamlString);

        /**
         * Loads a map into the world. Returns false without modifying the world when the text is malformed
         * (bad number, newer format version); the error log names the offending line.
         */
        bool Deserialize(const std::string& InFilePath);
        bool DeserializeText(const std::string& InYamlString);

        /** Highest `Version` major this build can read. Files with a newer major are rejected. */
        static constexpr int SupportedFormatMajor = 2;

    private:
        struct FLoadProgress {
            size_t Line = 0;             // 1-based line being parsed
            bool bWorldModified = false; // true once the target world has been cleared
        };
        bool ParseAndPopulate(const std::string& InYamlString, FLoadProgress& OutProgress);

        TRef<UWorld> World;
    };

} // namespace Leon
