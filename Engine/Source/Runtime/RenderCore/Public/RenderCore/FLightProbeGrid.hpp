#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Light probes: the diffuse light arriving at points of a level's volume, baked, for the things a
// lightmap cannot cover (characters and whatever moves).
//
// Each probe stores the irradiance around its point as first-order spherical harmonics, four numbers
// per colour channel, already folded with the cosine lobe: the irradiance on a surface with unit
// normal n is dot(coefficients, (n.x, n.y, n.z, 1)). Four numbers hold a level and a direction: light
// that is stronger from one side, which is what makes a character belong to the room it stands in.
// Second order (nine numbers) would add the difference between two opposite sides against the sides
// between them; with one sample for a whole object that detail is lost anyway, and it would more than
// double what every draw carries.

namespace Leon {

    /** The light at one point: one row per colour channel, xyz = how it changes with direction, w = its level. */
    struct FLightProbeSample {
        glm::vec4 Red{0.0f};
        glm::vec4 Green{0.0f};
        glm::vec4 Blue{0.0f};

        /** Irradiance on a surface facing InNormal (unit length). The twin of the surface shader's line. */
        glm::vec3 Irradiance(const glm::vec3& InNormal) const {
            const glm::vec4 normal(InNormal, 1.0f);
            return glm::max(glm::vec3(glm::dot(Red, normal), glm::dot(Green, normal), glm::dot(Blue, normal)),
                            glm::vec3(0.0f));
        }

        /**
         * Adds radiance arriving from a small part of the sphere around InDirection: InRadiance times
         * the solid angle it comes from. A light that is a point adds its irradiance on a surface
         * facing it. The projection onto the harmonics and the cosine lobe are both in the factors:
         * 1/4 for the level, 1/2 for the direction.
         */
        void AddRadiance(const glm::vec3& InDirection, const glm::vec3& InRadianceTimesSolidAngle) {
            const glm::vec4 basis(InDirection * 0.5f, 0.25f);
            Red += basis * InRadianceTimesSolidAngle.r;
            Green += basis * InRadianceTimesSolidAngle.g;
            Blue += basis * InRadianceTimesSolidAngle.b;
        }

        void AddScaled(const FLightProbeSample& InOther, float InWeight) {
            Red += InOther.Red * InWeight;
            Green += InOther.Green * InWeight;
            Blue += InOther.Blue * InWeight;
        }
    };

    /**
     * A regular grid of probes over the playable volume of a level. Probe (x, y, z) stands at
     * Origin + (x, y, z) * Spacing and is entry x + CountX * (y + CountY * z) of Probes.
     */
    struct FLightProbeGrid {
        glm::vec3 Origin{0.0f};
        glm::vec3 Spacing{1.5f, 1.0f, 1.5f};
        glm::uvec3 Counts{0};
        std::vector<FLightProbeSample> Probes;
        /**
         * 0 where a probe ended up inside a wall or outside the level: what it holds was copied from
         * its neighbours and is used only where no valid probe is in reach.
         */
        std::vector<uint8_t> Valid;

        bool IsEmpty() const { return Probes.empty() || Counts.x == 0 || Counts.y == 0 || Counts.z == 0; }
        size_t ProbeCount() const { return static_cast<size_t>(Counts.x) * Counts.y * Counts.z; }
        size_t IndexOf(uint32_t InX, uint32_t InY, uint32_t InZ) const {
            return InX + static_cast<size_t>(Counts.x) * (InY + static_cast<size_t>(Counts.y) * InZ);
        }
        glm::vec3 PositionOf(uint32_t InX, uint32_t InY, uint32_t InZ) const {
            return Origin + glm::vec3(static_cast<float>(InX), static_cast<float>(InY), static_cast<float>(InZ)) * Spacing;
        }

        void Resize(const glm::uvec3& InCounts) {
            Counts = InCounts;
            Probes.assign(ProbeCount(), FLightProbeSample{});
            Valid.assign(ProbeCount(), 1);
        }

        /**
         * The light at a point: the eight probes around it, blended by distance, leaving out the ones
         * that are not valid. A point outside the grid takes the light at the nearest point of it.
         * False when there is no grid.
         */
        bool Sample(const glm::vec3& InPosition, FLightProbeSample& OutSample) const {
            if (IsEmpty() || Probes.size() != ProbeCount() || Valid.size() != ProbeCount())
                return false;
            uint32_t low[3];
            uint32_t high[3];
            float along[3];
            for (int axis = 0; axis < 3; ++axis) {
                const uint32_t last = Counts[axis] - 1;
                const float cell = Spacing[axis] > 1.0e-6f ? (InPosition[axis] - Origin[axis]) / Spacing[axis] : 0.0f;
                const float clamped = std::clamp(cell, 0.0f, static_cast<float>(last));
                low[axis] = std::min(static_cast<uint32_t>(clamped), last);
                high[axis] = std::min(low[axis] + 1, last);
                along[axis] = clamped - static_cast<float>(low[axis]);
            }
            FLightProbeSample valid;
            FLightProbeSample all;
            float validWeight = 0.0f;
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const bool bHighX = (corner & 1) != 0;
                const bool bHighY = (corner & 2) != 0;
                const bool bHighZ = (corner & 4) != 0;
                const float weight = (bHighX ? along[0] : 1.0f - along[0]) * (bHighY ? along[1] : 1.0f - along[1]) *
                                     (bHighZ ? along[2] : 1.0f - along[2]);
                if (weight <= 0.0f)
                    continue;
                const size_t index = IndexOf(bHighX ? high[0] : low[0], bHighY ? high[1] : low[1], bHighZ ? high[2] : low[2]);
                all.AddScaled(Probes[index], weight);
                if (Valid[index]) {
                    valid.AddScaled(Probes[index], weight);
                    validWeight += weight;
                }
            }
            if (validWeight > 1.0e-4f) {
                OutSample = FLightProbeSample{};
                OutSample.AddScaled(valid, 1.0f / validWeight);
            } else {
                OutSample = all;
            }
            return true;
        }
    };

} // namespace Leon
