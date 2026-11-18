#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Leon {

    namespace Detail {

        /** What a handle needs from the delegate it came from, whatever its signature. */
        class IDelegateListeners {
        public:
            virtual ~IDelegateListeners() = default;
            virtual void Remove(uint64_t InId) = 0;
            virtual bool Contains(uint64_t InId) const = 0;
        };

    } // namespace Detail

    /**
     * What Add returns: the way to stop listening. It can outlive the delegate; removing through a
     * handle whose delegate is gone does nothing.
     */
    class FDelegateHandle {
    public:
        FDelegateHandle() = default;

        /** Whether the listener is still registered. */
        bool IsValid() const {
            const auto listeners = Listeners.lock();
            return listeners && listeners->Contains(Id);
        }

        void Remove() {
            if (const auto listeners = Listeners.lock())
                listeners->Remove(Id);
            Listeners.reset();
            Id = 0;
        }

    private:
        template <typename TSignature> friend class TMulticastDelegate;
        FDelegateHandle(std::weak_ptr<Detail::IDelegateListeners> InListeners, uint64_t InId)
            : Listeners(std::move(InListeners)), Id(InId) {}

        std::weak_ptr<Detail::IDelegateListeners> Listeners;
        uint64_t Id = 0;
    };

    /** A handle that stops listening when it goes away. Keep it as a member of the listener. */
    class FScopedDelegateHandle {
    public:
        FScopedDelegateHandle() = default;
        FScopedDelegateHandle(FDelegateHandle InHandle) : Handle(std::move(InHandle)) {}
        FScopedDelegateHandle(FScopedDelegateHandle&& InOther) noexcept : Handle(std::exchange(InOther.Handle, {})) {}
        FScopedDelegateHandle& operator=(FScopedDelegateHandle&& InOther) noexcept {
            if (this != &InOther) {
                Handle.Remove();
                Handle = std::exchange(InOther.Handle, {});
            }
            return *this;
        }
        FScopedDelegateHandle(const FScopedDelegateHandle&) = delete;
        FScopedDelegateHandle& operator=(const FScopedDelegateHandle&) = delete;
        ~FScopedDelegateHandle() { Handle.Remove(); }

        bool IsValid() const { return Handle.IsValid(); }
        void Remove() { Handle.Remove(); }

    private:
        FDelegateHandle Handle;
    };

    template <typename TSignature> class TMulticastDelegate;

    /**
     * An event with any number of listeners: `TMulticastDelegate<void(float, float)> OnHealthChanged;`
     *
     *   FDelegateHandle handle = health.OnDeath.Add([this](const FDamageInfo& InInfo) { ... });
     *   handle.Remove();
     *   door.OnOpened.AddWeak(listener, [raw = listener.get()] { raw->React(); });
     *
     * A listener may, while it is being called, add or remove listeners (itself included) and even
     * destroy the object that owns the delegate. Listeners added during a broadcast are first called
     * on the next one. A listener added with AddWeak is dropped once its owner is gone, and the owner
     * is kept alive for the length of the call. Not thread-safe.
     *
     * A single callback needs none of this: use std::function.
     */
    template <typename... TArgs> class TMulticastDelegate<void(TArgs...)> {
    public:
        using FFunction = std::function<void(TArgs...)>;

        TMulticastDelegate() = default;
        TMulticastDelegate(TMulticastDelegate&&) noexcept = default;
        TMulticastDelegate& operator=(TMulticastDelegate&&) noexcept = default;
        // The copy of an object does not inherit who was listening to the original.
        TMulticastDelegate(const TMulticastDelegate&) {}
        TMulticastDelegate& operator=(const TMulticastDelegate& InOther) {
            if (this != &InOther)
                Clear();
            return *this;
        }

        FDelegateHandle Add(FFunction InFunction) { return AddEntry(std::move(InFunction), {}, false); }

        /** The listener lives as long as InOwner does. */
        FDelegateHandle AddWeak(std::weak_ptr<const void> InOwner, FFunction InFunction) {
            return AddEntry(std::move(InFunction), std::move(InOwner), true);
        }

        void Remove(FDelegateHandle& InOutHandle) { InOutHandle.Remove(); }

        void Clear() {
            if (Listeners)
                Listeners->Clear();
        }

        bool IsBound() const { return Num() > 0; }

        size_t Num() const { return Listeners ? Listeners->Count() : 0; }

        void Broadcast(TArgs... InArgs) const {
            if (!Listeners)
                return;
            // From here on only the local reference is used: a listener may destroy this delegate.
            const std::shared_ptr<FListeners> listeners = Listeners;
            ++listeners->BroadcastDepth;
            const size_t count = listeners->Entries.size();
            for (size_t i = 0; i < count && i < listeners->Entries.size(); ++i) {
                // Copies, not references: a listener may add entries and move the array.
                const std::shared_ptr<FFunction> function = listeners->Entries[i].Function;
                if (!function)
                    continue;
                std::shared_ptr<const void> owner;
                if (listeners->Entries[i].bWeak) {
                    owner = listeners->Entries[i].Owner.lock();
                    if (!owner) {
                        listeners->Entries[i].Function.reset();
                        listeners->bHasRemoved = true;
                        continue;
                    }
                }
                (*function)(InArgs...);
            }
            if (--listeners->BroadcastDepth == 0)
                listeners->Compact();
        }

    private:
        struct FEntry {
            uint64_t Id = 0;
            std::shared_ptr<FFunction> Function; ///< Null once removed during a broadcast.
            std::weak_ptr<const void> Owner;
            bool bWeak = false;
        };

        struct FListeners final : Detail::IDelegateListeners {
            std::vector<FEntry> Entries;
            uint64_t NextId = 1;
            int BroadcastDepth = 0;
            bool bHasRemoved = false;

            void Remove(uint64_t InId) override {
                for (FEntry& entry : Entries) {
                    if (entry.Id == InId) {
                        entry.Function.reset();
                        bHasRemoved = true;
                        break;
                    }
                }
                Compact();
            }

            bool Contains(uint64_t InId) const override {
                for (const FEntry& entry : Entries) {
                    if (entry.Id == InId)
                        return entry.Function != nullptr && (!entry.bWeak || !entry.Owner.expired());
                }
                return false;
            }

            void Clear() {
                for (FEntry& entry : Entries)
                    entry.Function.reset();
                bHasRemoved = !Entries.empty();
                Compact();
            }

            size_t Count() const {
                size_t count = 0;
                for (const FEntry& entry : Entries) {
                    if (entry.Function && (!entry.bWeak || !entry.Owner.expired()))
                        ++count;
                }
                return count;
            }

            /** Removed entries leave the array only when nobody is walking it. */
            void Compact() {
                if (BroadcastDepth > 0 || !bHasRemoved)
                    return;
                Entries.erase(std::remove_if(Entries.begin(), Entries.end(),
                                             [](const FEntry& InEntry) { return !InEntry.Function; }),
                              Entries.end());
                bHasRemoved = false;
            }
        };

        FDelegateHandle AddEntry(FFunction InFunction, std::weak_ptr<const void> InOwner, bool bInWeak) {
            if (!InFunction)
                return {};
            if (!Listeners)
                Listeners = std::make_shared<FListeners>();
            FEntry entry;
            entry.Id = Listeners->NextId++;
            entry.Function = std::make_shared<FFunction>(std::move(InFunction));
            entry.Owner = std::move(InOwner);
            entry.bWeak = bInWeak;
            Listeners->Entries.push_back(std::move(entry));
            return FDelegateHandle(Listeners, Listeners->Entries.back().Id);
        }

        std::shared_ptr<FListeners> Listeners;
    };

} // namespace Leon
