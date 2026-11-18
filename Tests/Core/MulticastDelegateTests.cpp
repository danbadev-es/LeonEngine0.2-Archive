#include <doctest/doctest.h>

#include "Core/TMulticastDelegate.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Leon {

    TEST_SUITE("TMulticastDelegate") {

        TEST_CASE("every listener is called, in the order it was added, with the arguments") {
            TMulticastDelegate<void(int, const std::string&)> delegate;
            std::vector<std::string> calls;
            delegate.Add([&](int InNumber, const std::string& InText) { calls.push_back("a" + std::to_string(InNumber) + InText); });
            delegate.Add([&](int InNumber, const std::string& InText) { calls.push_back("b" + std::to_string(InNumber) + InText); });

            CHECK(delegate.IsBound());
            CHECK(delegate.Num() == 2);
            delegate.Broadcast(7, "x");
            CHECK(calls == std::vector<std::string>{"a7x", "b7x"});
        }

        TEST_CASE("an empty delegate broadcasts to nobody") {
            TMulticastDelegate<void()> delegate;
            CHECK_FALSE(delegate.IsBound());
            delegate.Broadcast();
            CHECK_FALSE(delegate.Add(nullptr).IsValid());
            CHECK_FALSE(delegate.IsBound());
        }

        TEST_CASE("a handle removes only its own listener") {
            TMulticastDelegate<void()> delegate;
            int first = 0;
            int second = 0;
            FDelegateHandle handle = delegate.Add([&] { ++first; });
            delegate.Add([&] { ++second; });
            CHECK(handle.IsValid());

            handle.Remove();
            CHECK_FALSE(handle.IsValid());
            delegate.Broadcast();
            CHECK(first == 0);
            CHECK(second == 1);

            handle.Remove(); // twice is harmless
            CHECK(delegate.Num() == 1);
        }

        TEST_CASE("a handle outlives its delegate") {
            FDelegateHandle handle;
            {
                TMulticastDelegate<void()> delegate;
                handle = delegate.Add([] {});
                CHECK(handle.IsValid());
            }
            CHECK_FALSE(handle.IsValid());
            handle.Remove();
        }

        TEST_CASE("a scoped handle stops listening when it goes away") {
            TMulticastDelegate<void()> delegate;
            int calls = 0;
            {
                FScopedDelegateHandle scoped = delegate.Add([&] { ++calls; });
                delegate.Broadcast();
                CHECK(calls == 1);

                FScopedDelegateHandle moved = std::move(scoped);
                CHECK(moved.IsValid());
                CHECK(delegate.Num() == 1);
            }
            delegate.Broadcast();
            CHECK(calls == 1);
            CHECK_FALSE(delegate.IsBound());
        }

        TEST_CASE("a weak listener is dropped with its owner") {
            TMulticastDelegate<void()> delegate;
            int calls = 0;
            auto owner = std::make_shared<int>(0);
            FDelegateHandle handle = delegate.AddWeak(owner, [&] { ++calls; });

            delegate.Broadcast();
            CHECK(calls == 1);

            owner.reset();
            CHECK_FALSE(handle.IsValid());
            CHECK_FALSE(delegate.IsBound());
            delegate.Broadcast();
            CHECK(calls == 1);
            CHECK(delegate.Num() == 0);
        }

        TEST_CASE("the owner of a weak listener stays alive during its call") {
            TMulticastDelegate<void()> delegate;
            auto owner = std::make_shared<int>(5);
            std::weak_ptr<int> watch = owner;
            int seen = 0;
            delegate.AddWeak(owner, [&] {
                owner.reset(); // the last reference outside the delegate
                seen = watch.expired() ? -1 : *watch.lock();
            });
            delegate.Broadcast();
            CHECK(seen == 5);
            CHECK(watch.expired());
        }

        TEST_CASE("a listener may remove itself and others while being called") {
            TMulticastDelegate<void()> delegate;
            std::vector<int> calls;
            FDelegateHandle first;
            FDelegateHandle third;
            first = delegate.Add([&] {
                calls.push_back(1);
                first.Remove();
                third.Remove();
            });
            delegate.Add([&] { calls.push_back(2); });
            third = delegate.Add([&] { calls.push_back(3); });

            delegate.Broadcast();
            CHECK(calls == std::vector<int>{1, 2});
            CHECK(delegate.Num() == 1);
            delegate.Broadcast();
            CHECK(calls == std::vector<int>{1, 2, 2});
        }

        TEST_CASE("a listener added during a broadcast waits for the next one") {
            TMulticastDelegate<void()> delegate;
            int late = 0;
            int adds = 0;
            delegate.Add([&] {
                if (adds++ == 0) {
                    // Enough of them to move the array under the broadcast.
                    for (int i = 0; i < 64; ++i)
                        delegate.Add([&] { ++late; });
                }
            });
            delegate.Broadcast();
            CHECK(late == 0);
            delegate.Broadcast();
            CHECK(late == 64);
        }

        TEST_CASE("a listener may clear the delegate or destroy it") {
            SUBCASE("clear") {
                TMulticastDelegate<void()> delegate;
                int calls = 0;
                delegate.Add([&] {
                    ++calls;
                    delegate.Clear();
                });
                delegate.Add([&] { ++calls; });
                delegate.Broadcast();
                CHECK(calls == 1);
                CHECK_FALSE(delegate.IsBound());
            }
            SUBCASE("destroy") {
                struct FOwner {
                    TMulticastDelegate<void()> OnEvent;
                };
                auto owner = std::make_unique<FOwner>();
                int calls = 0;
                owner->OnEvent.Add([&] {
                    ++calls;
                    owner.reset();
                });
                owner->OnEvent.Add([&] { ++calls; });
                owner->OnEvent.Broadcast();
                CHECK(calls == 2); // the listeners of this broadcast are still told
                CHECK(owner == nullptr);
            }
        }

        TEST_CASE("a broadcast inside a broadcast is allowed") {
            TMulticastDelegate<void(int)> delegate;
            std::vector<int> calls;
            delegate.Add([&](int InDepth) {
                calls.push_back(InDepth);
                if (InDepth < 2)
                    delegate.Broadcast(InDepth + 1);
            });
            delegate.Broadcast(0);
            CHECK(calls == std::vector<int>{0, 1, 2});
        }

        TEST_CASE("a copy does not inherit the listeners") {
            TMulticastDelegate<void()> original;
            int calls = 0;
            original.Add([&] { ++calls; });

            TMulticastDelegate<void()> copy = original;
            CHECK_FALSE(copy.IsBound());
            copy.Broadcast();
            CHECK(calls == 0);

            TMulticastDelegate<void()> moved = std::move(original);
            moved.Broadcast();
            CHECK(calls == 1);
        }
    }

} // namespace Leon
