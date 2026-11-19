#pragma once

#include <functional>

namespace Leon {

    class IRHICommandList;
    class IRHITexture;

    /**
     * The frame being drawn, for code that draws without being handed a command list: the HUD, debug
     * overlays and other immediate-mode drawing called from gameplay code.
     *
     * Whoever owns the frame (the application's main loop, the editor's viewport, a test) calls Begin
     * once the device has started the frame and End before it submits. Outside that window Commands
     * and Output are null and such drawing is skipped.
     */
    class FRHIFrame {
    public:
        /** InOutput is the image the frame ends up in; 2D drawing goes on top of it. */
        static void Begin(IRHICommandList& InCommands, IRHITexture& InOutput);
        static void End();

        static IRHICommandList* GetCommands();
        static IRHITexture* GetOutput();

        /**
         * Work that needs the device between frames (reading a texture back to memory cannot happen
         * while a frame is being recorded). Queue it from inside the frame; whoever owns the frame
         * calls RunAfterFrameTasks once the device has ended it. Tasks run once, in the order queued.
         */
        static void RunAfterFrame(std::function<void()> InTask);
        static void RunAfterFrameTasks();
    };

} // namespace Leon
