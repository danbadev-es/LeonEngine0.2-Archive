#pragma once

#include "Core/Base.hpp"
#include "Core/events/FEvent.hpp"
#include "RHI/IGraphicsContext.hpp"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Leon {

    /**
     * HD client policy: 16:9, windowed default 720p, hard cap 1080p.
     * Keeps framebuffer memory and camera aspect stable when the OS desktop is 16:10 / 1440p / 4K.
     */
    struct FWindowDisplayPolicy {
        static constexpr unsigned int DefaultWidth = 1280;
        static constexpr unsigned int DefaultHeight = 720;
        static constexpr unsigned int MaxWidth = 1920;
        static constexpr unsigned int MaxHeight = 1080;
        static constexpr int AspectNumerator = 16;
        static constexpr int AspectDenominator = 9;

        static void ConstrainClientSize(unsigned int& InOutWidth, unsigned int& InOutHeight);
    };

    struct FWindowProps {
        std::string Title;
        unsigned int Width;
        unsigned int Height;
        bool bVSync;
        /** Game client: 16:9, 720p–1080p cap, maximize snaps to HD. Editor leaves this false. */
        bool bConstrainAspect;
        bool bResizable;
        /** OS-maximized at glfwCreateWindow. Independent of game HD maximize-snap. */
        bool bMaximized;

        FWindowProps(const std::string& InTitle = "LeonEngine2",
                     unsigned int InWidth = FWindowDisplayPolicy::DefaultWidth,
                     unsigned int InHeight = FWindowDisplayPolicy::DefaultHeight, bool bInVSync = true,
                     bool bInConstrainAspect = false, bool bInResizable = true, bool bInMaximized = false)
            : Title(InTitle), Width(InWidth), Height(InHeight), bVSync(bInVSync), bConstrainAspect(bInConstrainAspect),
              bResizable(bInResizable), bMaximized(bInMaximized) {}
    };

    class FWindow {
    public:
        using FEventCallbackFn = std::function<void(FEvent&)>;

        FWindow(const FWindowProps& InProps);
        ~FWindow();

        void OnUpdate();

        unsigned int GetWidth() const { return Data.Width; }
        unsigned int GetHeight() const { return Data.Height; }

        void SetEventCallback(const FEventCallbackFn& InCallback) { Data.EventCallback = InCallback; }
        void SetVSync(bool bInEnabled);
        bool IsVSync() const { return Data.bVSync; }

        void SetFullscreen(bool bInEnabled);
        bool IsFullscreen() const { return Data.bFullscreen; }

        void SetCursorVisible(bool bVisible);
        bool IsCursorVisible() const { return Data.bCursorVisible; }

        /** Title-bar / taskbar icon from an RGBA PNG. Also applies the exe resource on Windows. */
        void SetIconFromFile(const std::string& InPath);

        /**
         * The windowing library's own handle (a GLFWwindow*), for the few callers that must hand it to
         * another library, such as the editor's ImGui backend. Engine code should not need it.
         */
        void* GetNativeHandle() const { return NativeWindow; }
        /** The Win32 HWND, or null on other platforms. What a GPU swapchain is created on. */
        void* GetOSHandle() const;

        void SetTitle(const std::string& InTitle);
        bool IsMaximized() const;
        /** Position and size of the window on the desktop, in screen coordinates. */
        void GetWindowedRect(int& OutX, int& OutY, int& OutWidth, int& OutHeight) const;
        /** Cancels (or requests) closing, e.g. after the user declines a "save changes?" prompt. */
        void SetShouldClose(bool bInShouldClose);

        /** The first window created; input is read from it. Null before any window exists. */
        static FWindow* GetMainWindow();

        static TScope<FWindow> Create(const FWindowProps& InProps = FWindowProps());

    private:
        static FWindow*& MainWindowSlot() {
            static FWindow* slot = nullptr;
            return slot;
        }
        void Init(const FWindowProps& InProps);
        void Shutdown();
        void ApplyHdClientConstraints();
        void ApplyEmbeddedWin32Icon();

    private:
        GLFWwindow* NativeWindow = nullptr;
        TScope<IGraphicsContext> Context;
        int WindowedPosX = 100;
        int WindowedPosY = 100;
        unsigned int WindowedWidth = FWindowDisplayPolicy::DefaultWidth;
        unsigned int WindowedHeight = FWindowDisplayPolicy::DefaultHeight;

        struct FWindowData {
            std::string Title;
            unsigned int Width = 0;
            unsigned int Height = 0;
            bool bVSync = true;
            bool bFullscreen = false;
            bool bCursorVisible = true;
            bool bHdClientPolicy = false;
            FEventCallbackFn EventCallback;
        };

        FWindowData Data;
    };

} // namespace Leon
