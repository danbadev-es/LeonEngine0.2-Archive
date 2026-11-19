#include "LeonHorror/ALeonHorrorHeldItem.hpp"

#include "Assets/UAssetManager.hpp"
#include "Scene/Components.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Leon {

    namespace {

        /** The turn a matrix makes, whatever scale it carries. */
        glm::quat RotationOf(const glm::mat4& InMatrix) {
            glm::mat3 axes(InMatrix);
            for (int i = 0; i < 3; ++i) {
                const float length = glm::length(axes[i]);
                axes[i] = length > 1.0e-8f ? axes[i] / length : glm::vec3(i == 0, i == 1, i == 2);
            }
            return glm::normalize(glm::quat_cast(axes));
        }

    } // namespace

    ALeonHorrorHeldItem::ALeonHorrorHeldItem(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorHeldItem");
        SetCanEverTick(false);
    }

    void ALeonHorrorHeldItem::SetMesh(const std::string& InMeshPath) {
        if (InMeshPath == MeshPath)
            return;
        MeshPath = InMeshPath;
        if (MeshPath.empty()) {
            ApplyVisibility();
            return;
        }
        if (!HasComponent<FStaticMeshComponent>())
            AddComponent<FStaticMeshComponent>();
        auto& mesh = GetComponent<FStaticMeshComponent>();
        mesh.StaticMesh = UAssetManager::GetStaticMesh(MeshPath);
        mesh.AssetPath = MeshPath;
        mesh.Mobility = EComponentMobility::Movable;
        mesh.bCollision = false;
        // It is small and right beside the lights the carrier holds: its shadow would only flicker.
        mesh.bCastShadows = false;
        ApplyVisibility();
    }

    void ALeonHorrorHeldItem::SetShown(bool bInShown) {
        bShown = bInShown;
        ApplyVisibility();
    }

    void ALeonHorrorHeldItem::ApplyVisibility() {
        if (HasComponent<FStaticMeshComponent>())
            GetComponent<FStaticMeshComponent>().bVisible = IsShown();
    }

    void ALeonHorrorHeldItem::SetGrip(const glm::vec3& InOffset, const glm::vec3& InRotationDegrees) {
        GripOffset = InOffset;
        GripRotation = InRotationDegrees;
    }

    void ALeonHorrorHeldItem::FollowBone(const glm::mat4& InBoneWorld) {
        const glm::quat bone = RotationOf(InBoneWorld);
        const glm::quat turn = bone * glm::quat(glm::radians(GripRotation));
        SetActorLocation(glm::vec3(InBoneWorld[3]) + bone * GripOffset);
        SetActorRotation(glm::degrees(glm::eulerAngles(turn)));
    }

    void ALeonHorrorHeldItem::FollowBoneAimed(const glm::mat4& InBoneWorld, const glm::vec3& InForward) {
        const float length = glm::length(InForward);
        const glm::vec3 forward = length > 1.0e-5f ? InForward / length : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 up = std::abs(forward.y) < 0.98f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::quat aim = glm::quatLookAt(forward, up);
        SetActorLocation(glm::vec3(InBoneWorld[3]) + RotationOf(InBoneWorld) * GripOffset);
        SetActorRotation(glm::degrees(glm::eulerAngles(aim * glm::quat(glm::radians(GripRotation)))));
    }

} // namespace Leon
