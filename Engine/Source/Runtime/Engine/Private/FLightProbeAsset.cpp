#include "Engine/FLightProbeAsset.hpp"

#include <glm/gtc/packing.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace Leon {

    namespace {

        struct FLightProbeFileHeader {
            uint32_t Magic = FLightProbeAsset::kMagic;
            uint32_t Version = FLightProbeAsset::kVersion;
            uint64_t ContentHash = 0;
            float Origin[3] = {0.0f, 0.0f, 0.0f};
            float Spacing[3] = {0.0f, 0.0f, 0.0f};
            uint32_t Counts[3] = {0, 0, 0};
            uint32_t Reserved = 0;
        };
        static_assert(sizeof(FLightProbeFileHeader) == 56, "the header is written as it lies in memory");

        constexpr size_t kCoefficientsPerProbe = 12;
        // The largest value a half float holds; brighter light than this is not light a level has.
        constexpr float kLargestHalf = 65504.0f;

    } // namespace

    bool FLightProbeAsset::Save(const std::string& InPath, const FLightProbeGrid& InGrid, uint64_t InContentHash) {
        const size_t count = InGrid.ProbeCount();
        if (count == 0 || InGrid.Probes.size() != count || InGrid.Valid.size() != count)
            return false;

        FLightProbeFileHeader header;
        header.ContentHash = InContentHash;
        for (int axis = 0; axis < 3; ++axis) {
            header.Origin[axis] = InGrid.Origin[axis];
            header.Spacing[axis] = InGrid.Spacing[axis];
            header.Counts[axis] = InGrid.Counts[axis];
        }
        std::vector<uint16_t> coefficients(count * kCoefficientsPerProbe);
        for (size_t probe = 0; probe < count; ++probe) {
            const glm::vec4* rows[3] = {&InGrid.Probes[probe].Red, &InGrid.Probes[probe].Green, &InGrid.Probes[probe].Blue};
            for (size_t row = 0; row < 3; ++row) {
                for (int component = 0; component < 4; ++component) {
                    const float value = (*rows[row])[component];
                    // A value that is not a number is stored as no light rather than spread by the game.
                    const float stored = value == value ? glm::clamp(value, -kLargestHalf, kLargestHalf) : 0.0f;
                    coefficients[probe * kCoefficientsPerProbe + row * 4 + static_cast<size_t>(component)] =
                        glm::packHalf1x16(stored);
                }
            }
        }

        std::error_code error;
        const std::filesystem::path path(InPath);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), error);
        // Under another name first: a bake that is interrupted leaves no file that looks whole.
        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(InGrid.Valid.data()), static_cast<std::streamsize>(count));
            file.write(reinterpret_cast<const char*>(coefficients.data()),
                       static_cast<std::streamsize>(coefficients.size() * sizeof(uint16_t)));
            if (!file)
                return false;
        }
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path, error);
        return !error;
    }

    bool FLightProbeAsset::Load(const std::string& InPath, FLightProbeGrid& OutGrid, uint64_t& OutContentHash) {
        OutGrid = FLightProbeGrid{};
        OutContentHash = 0;
        std::ifstream file(InPath, std::ios::binary);
        FLightProbeFileHeader header;
        if (!file || !file.read(reinterpret_cast<char*>(&header), sizeof(header)))
            return false;
        if (header.Magic != kMagic || header.Version != kVersion)
            return false;
        const uint64_t count = static_cast<uint64_t>(header.Counts[0]) * header.Counts[1] * header.Counts[2];
        if (count == 0 || count > kMaxProbes)
            return false;

        std::vector<uint8_t> valid(static_cast<size_t>(count));
        std::vector<uint16_t> coefficients(static_cast<size_t>(count) * kCoefficientsPerProbe);
        if (!file.read(reinterpret_cast<char*>(valid.data()), static_cast<std::streamsize>(valid.size())) ||
            !file.read(reinterpret_cast<char*>(coefficients.data()),
                       static_cast<std::streamsize>(coefficients.size() * sizeof(uint16_t))))
            return false;

        FLightProbeGrid grid;
        grid.Origin = {header.Origin[0], header.Origin[1], header.Origin[2]};
        grid.Spacing = {header.Spacing[0], header.Spacing[1], header.Spacing[2]};
        grid.Resize({header.Counts[0], header.Counts[1], header.Counts[2]});
        for (size_t probe = 0; probe < grid.Probes.size(); ++probe) {
            glm::vec4* rows[3] = {&grid.Probes[probe].Red, &grid.Probes[probe].Green, &grid.Probes[probe].Blue};
            for (size_t row = 0; row < 3; ++row) {
                for (int component = 0; component < 4; ++component)
                    (*rows[row])[component] = glm::unpackHalf1x16(
                        coefficients[probe * kCoefficientsPerProbe + row * 4 + static_cast<size_t>(component)]);
            }
            grid.Valid[probe] = valid[probe] != 0 ? 1 : 0;
        }
        OutGrid = std::move(grid);
        OutContentHash = header.ContentHash;
        return true;
    }

} // namespace Leon
