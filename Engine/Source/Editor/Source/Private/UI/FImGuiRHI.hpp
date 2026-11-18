#pragma once

#include "RHI/IRHIDevice.hpp"

#include <imgui.h>

namespace Leon::Editor {

    /**
     * Draws Dear ImGui through the engine's RHI.
     *
     * A texture is passed to ImGui (ImGui::Image, the font atlas) as its IRHITexture pointer; use
     * ToImGuiTexture. The caller keeps the texture alive until the frame that shows it has been drawn.
     */
    class FImGuiRHI {
    public:
        /** Builds the font atlas texture. Call after the fonts have been added to the ImGui context. */
        static bool Init();
        static void Shutdown();

        /** Draws InDrawData over InTarget, keeping what it holds. Call between render passes. */
        static void Render(const ImDrawData* InDrawData, IRHICommandList& InCommands, IRHITexture& InTarget);

        /**
         * A texture from 8-bit RGBA pixels (rows from the top), for thumbnails and logos.
         * Null without a device or when the size is zero.
         */
        static TRef<IRHITexture> CreateTexture(const unsigned char* InPixels, int InWidth, int InHeight,
                                               const char* InDebugName);

        static ImTextureID ToImGuiTexture(IRHITexture* InTexture) {
            return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(InTexture));
        }
    };

} // namespace Leon::Editor
