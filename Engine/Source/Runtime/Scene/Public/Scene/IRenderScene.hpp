#pragma once

#include "Scene/Components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Leon {

    /**
     * What the renderer needs to know about a world, and nothing else.
     *
     * The world implements it. The renderer draws from the components in the registry and asks for the
     * few things that are not components, so it does not depend on worlds, actors or gameplay.
     */
    class IRenderScene {
    public:
        virtual ~IRenderScene() = default;

        /** The entities to draw and their components (Scene/Components.hpp). */
        virtual entt::registry& GetRegistry() = 0;

        /** False when the map's baked lighting no longer matches it: lightmaps are then not sampled. */
        virtual bool AreLightmapsTrusted() const = 0;

        /**
         * Identifies the baked lighting in use: the hash of everything the bake depended on (geometry,
         * materials, lights, sky). 0 when the world has no bake or does not trust it. What is derived
         * from the lit static scene (reflection probe captures) is kept on disk under this number.
         */
        virtual uint64_t GetStaticLightingHash() const { return 0; }

        /**
         * Where an entity is in the world. For an entity that belongs to an actor this follows what the
         * actor is attached to; otherwise it is the transform component as it stands.
         */
        virtual glm::mat4 GetEntityWorldMatrix(entt::entity InEntity, const FTransformComponent& InTransform) const = 0;

        /** True when DrawDebug has something to draw this frame. */
        virtual bool WantsDebugDraw() const { return false; }

        /**
         * World-space debug shapes (collision, traces). Called inside a FDebugRenderer scene, after
         * the frame's geometry.
         */
        virtual void DrawDebug() {}
    };

} // namespace Leon
