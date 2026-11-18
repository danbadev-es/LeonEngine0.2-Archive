#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "Assets/FAnimRuntime.hpp"
#include "Assets/UAnimSequence.hpp"
#include "Assets/USkeleton.hpp"

#include <cmath>
#include <filesystem>
#include <string>

// Checks on animation clips produced by Projects/LeonHorror/Scripts/import_animations.py
// (Blender -> FBX -> AssetTool -> .lanim).

namespace Leon {

    namespace {

        const char* kSkeletonPath = "Projects/LeonHorror/Content/Skeletons/YBot.lskeleton";

        TRef<USkeleton> LoadSkeleton() {
            auto skeleton = USkeleton::Create("YBot");
            if (!skeleton->LoadFromFile(kSkeletonPath))
                return nullptr;
            return skeleton;
        }

        TRef<UAnimSequence> LoadClip(const std::string& InPath, const TRef<USkeleton>& InSkeleton) {
            auto clip = UAnimSequence::Create(std::filesystem::path(InPath).stem().string());
            if (!clip->LoadFromFile(InPath))
                return nullptr;
            clip->LinkSkeleton(InSkeleton);
            return clip;
        }

        /** Component-space position of a bone: accumulates local transforms up the parent chain. */
        glm::vec3 BoneComponentPosition(const USkeleton& InSkeleton, const FPose& InPose, int32_t InBone) {
            glm::mat4 matrix(1.0f);
            for (int32_t bone = InBone; bone >= 0; bone = InSkeleton.GetBones()[static_cast<size_t>(bone)].ParentIndex)
                matrix = InPose.LocalTransforms[static_cast<size_t>(bone)].ToMatrix() * matrix;
            return glm::vec3(matrix[3]);
        }

        float RotationDifferenceDegrees(const glm::quat& InA, const glm::quat& InB) {
            const float d = std::min(1.0f, std::abs(glm::dot(glm::normalize(InA), glm::normalize(InB))));
            return glm::degrees(2.0f * std::acos(d));
        }

    } // namespace

    TEST_SUITE("LeonHorror animation pipeline") {

        TEST_CASE("Blender round trip: a re-exported clip matches the original bone for bone") {
            // Produced by: python Projects/LeonHorror/Scripts/import_animations.py --roundtrip
            const std::string roundTripPath = "build/anim_roundtrip/Content/Animations/IdleRoundTrip.lanim";
            if (!std::filesystem::exists(roundTripPath)) {
                MESSAGE("round-trip clip not generated; run import_animations.py --roundtrip to enable this check");
                return;
            }

            auto skeleton = LoadSkeleton();
            REQUIRE(skeleton);
            auto original = LoadClip("Projects/LeonHorror/Content/Animations/Idle.lanim", skeleton);
            auto roundTrip = LoadClip(roundTripPath, skeleton);
            REQUIRE(original);
            REQUIRE(roundTrip);

            CHECK(roundTrip->GetDuration() == doctest::Approx(original->GetDuration()).epsilon(0.03));
            CHECK(roundTrip->GetTracks().size() == original->GetTracks().size());

            const size_t boneCount = skeleton->GetBones().size();
            float worstRotation = 0.0f;
            float worstPosition = 0.0f;
            std::string worstBone;
            for (float alpha : {0.0f, 0.25f, 0.5f, 0.75f, 0.95f}) {
                const float time = original->GetDuration() * alpha;
                FPose a;
                FPose b;
                FAnimRuntime::SampleSequence(*original, *skeleton, time, false, a);
                FAnimRuntime::SampleSequence(*roundTrip, *skeleton, time, false, b);
                REQUIRE(a.Num() == boneCount);
                REQUIRE(b.Num() == boneCount);
                for (size_t i = 0; i < boneCount; ++i) {
                    const float rotation =
                        RotationDifferenceDegrees(a.LocalTransforms[i].Rotation, b.LocalTransforms[i].Rotation);
                    if (rotation > worstRotation) {
                        worstRotation = rotation;
                        worstBone = skeleton->GetBones()[i].Name;
                    }
                    const glm::vec3 pa = BoneComponentPosition(*skeleton, a, static_cast<int32_t>(i));
                    const glm::vec3 pb = BoneComponentPosition(*skeleton, b, static_cast<int32_t>(i));
                    worstPosition = std::max(worstPosition, glm::length(pa - pb));
                }
            }
            INFO("worst local rotation difference " << worstRotation << " deg at bone " << worstBone);
            INFO("worst component-space position difference " << worstPosition << " m");
            // Bone axes and scale survive: under a degree and under a centimetre anywhere in the body.
            CHECK(worstRotation < 1.0f);
            CHECK(worstPosition < 0.01f);
        }
    }

} // namespace Leon
