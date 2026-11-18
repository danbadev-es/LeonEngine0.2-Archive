#pragma once

#include "Core/UObject.hpp"

#include <cstddef>
#include <memory>

namespace Leon {

    class AActor;

    /**
     * @brief Non-owning reference to a UObject-derived object that knows when its target is gone.
     *
     * Actors and components are owned by shared pointers but refer to each other through raw pointers.
     * Any raw pointer kept across frames can outlive its target. TWeakObjectPtr is the replacement for
     * those: Get() returns null once the object has been released, and also while an actor is
     * pending-kill (destroyed but not yet removed from the world).
     *
     * The target must be owned by a shared pointer (everything created through UWorld::SpawnActor or
     * AActor::AddActorComponent is). A pointer to an object that is not, resolves to null.
     *
     * For references that must survive a map change or a save file, store the actor GUID instead.
     */
    template <typename T> class TWeakObjectPtr {
    public:
        TWeakObjectPtr() = default;
        TWeakObjectPtr(std::nullptr_t) {}
        TWeakObjectPtr(T* InObject) { Assign(InObject); }
        TWeakObjectPtr(const TRef<T>& InObject) { Assign(InObject.get()); }

        TWeakObjectPtr& operator=(T* InObject) {
            Assign(InObject);
            return *this;
        }
        TWeakObjectPtr& operator=(std::nullptr_t) {
            Reset();
            return *this;
        }

        /** The live object, or null if it was released or is pending-kill. */
        T* Get() const {
            if (!Raw || Control.expired())
                return nullptr;
            return IsAlive(*Raw) ? Raw : nullptr;
        }

        /** Owning reference for the duration of a call that may destroy the target. */
        TRef<T> Pin() const {
            T* live = Get();
            return live ? std::static_pointer_cast<T>(Control.lock()) : nullptr;
        }

        bool IsValid() const { return Get() != nullptr; }
        explicit operator bool() const { return IsValid(); }

        /** True when a target was assigned and has since gone away. */
        bool IsStale() const { return Raw != nullptr && Get() == nullptr; }

        void Reset() {
            Control.reset();
            Raw = nullptr;
        }

        bool operator==(const T* InOther) const { return Get() == InOther; }
        bool operator!=(const T* InOther) const { return Get() != InOther; }
        bool operator==(const TWeakObjectPtr& InOther) const { return Get() == InOther.Get(); }
        bool operator!=(const TWeakObjectPtr& InOther) const { return Get() != InOther.Get(); }

    private:
        void Assign(T* InObject) {
            if (!InObject) {
                Reset();
                return;
            }
            Control = InObject->weak_from_this();
            Raw = InObject;
        }

        static bool IsAlive(const T& InObject) {
            if constexpr (requires { InObject.IsPendingKill(); })
                return !InObject.IsPendingKill();
            else
                return true;
        }

        // Control block of the owning shared pointer; Raw is only dereferenced while it has not expired.
        std::weak_ptr<UObject> Control;
        T* Raw = nullptr;
    };

    /** Weak reference to an actor: null once the actor is destroyed or pending-kill. */
    using FActorHandle = TWeakObjectPtr<AActor>;

} // namespace Leon
