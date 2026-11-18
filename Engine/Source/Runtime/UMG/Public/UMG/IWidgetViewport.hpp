#pragma once

#include "Core/Base.hpp"

#include <cstdint>

namespace Leon {

    class UUserWidget;

    /**
     * Where user widgets are put on screen. The HUD implements it; widgets only know this much of
     * it, so the UI library does not depend on gameplay.
     */
    class IWidgetViewport {
    public:
        virtual ~IWidgetViewport() = default;
        virtual void AddWidgetToViewport(const TRef<UUserWidget>& InWidget, int32_t InZOrder = 0) = 0;
        virtual void RemoveWidgetFromViewport(const TRef<UUserWidget>& InWidget) = 0;
    };

} // namespace Leon
