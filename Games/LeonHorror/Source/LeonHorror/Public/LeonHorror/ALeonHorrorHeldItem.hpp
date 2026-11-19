#pragma once

#include "Gameplay/AActor.hpp"

#include <string>

namespace Leon {

    /**
     * Something a character carries in a hand: a mesh on its own actor that is put, every tick, where
     * a bone of the carrier's skeleton is. Actor attachment does not carry transforms, and a skeletal
     * mesh has no children of its own, so the carrier does the placing.
     *
     * The mesh is authored as it is held (Art: SM_Held_*): its origin where the hand closes, pointing
     * along -Z, up along +Y. It collides with nothing and casts no shadow.
     */
    class ALeonHorrorHeldItem : public AActor {
    public:
        ALeonHorrorHeldItem() = default;
        ALeonHorrorHeldItem(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "HeldItem");

        /** The mesh to show, as a virtual path; empty shows nothing. */
        void SetMesh(const std::string& InMeshPath);
        const std::string& GetMeshPath() const { return MeshPath; }

        void SetShown(bool bInShown);
        bool IsShown() const { return bShown && !MeshPath.empty(); }

        /**
         * How the thing sits in the hand: where its origin is from the bone, in metres along the
         * bone's own axes, and how it is turned from them, in degrees.
         */
        void SetGrip(const glm::vec3& InOffset, const glm::vec3& InRotationDegrees);

        /** Goes where the bone is and turns with it. The bone's matrix may carry any scale. */
        void FollowBone(const glm::mat4& InBoneWorld);
        /** Goes where the bone is but points along a direction: a flashlight held out at something. */
        void FollowBoneAimed(const glm::mat4& InBoneWorld, const glm::vec3& InForward);

    private:
        void ApplyVisibility();

        std::string MeshPath;
        glm::vec3 GripOffset{0.0f};
        glm::vec3 GripRotation{0.0f};
        bool bShown = true;
    };

} // namespace Leon
