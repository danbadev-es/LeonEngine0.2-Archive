#pragma once

#include "Core/Base.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "RHI/FVertexArray.hpp"

#include <glm/glm.hpp>

namespace Leon {

    class IRenderScene;

    /**
     * Camera-facing additive quads and thin beams for UParticleComponent instances.
     */
    class FParticleRenderer {
    public:
        static void Init();
        static void Shutdown();
        static void Render(IRenderScene* InWorld, const FPerspectiveCamera& InCamera);

    private:
        struct FVertex {
            glm::vec3 Position;
            glm::vec4 Color;
        };

        /** False without a render driver (tests, tools): nothing is drawn. */
        static bool bReady;
        static TRef<FVertexArray> VertexArray;
        static TRef<FVertexBuffer> VertexBuffer;
        static constexpr size_t MaxVertices = 16384;
    };

} // namespace Leon
