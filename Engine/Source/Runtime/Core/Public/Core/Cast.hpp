#pragma once

#include "Core/Base.hpp"

namespace Leon {

    /**
     * The object as a TTo, or null when it is not one (or is null). TTo may be a class or an
     * interface: `if (auto* usable = Cast<IInteractable>(actor))` is how to ask whether an object
     * implements one.
     */
    template <typename TTo, typename TFrom> TTo* Cast(TFrom* InObject) {
        return dynamic_cast<TTo*>(InObject);
    }

    template <typename TTo, typename TFrom> TTo* Cast(const TRef<TFrom>& InObject) {
        return dynamic_cast<TTo*>(InObject.get());
    }

} // namespace Leon
