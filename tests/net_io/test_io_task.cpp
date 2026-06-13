#include <gtest/gtest.h>
#include <string>
#include <coroutine>
#include <stop_token>

import net_io.async_utils; // IoTask alias -> net_io::Task
import modern.runtime;

namespace {

struct ManualSuspend {
    std::coroutine_handle<>* parked{};

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h) const noexcept {
        *parked = h;
        return true;
    }
    void await_resume() const noexcept {}
};

auto observe_cancel_state() -> modern::net::Task<bool> {
    auto env = co_await modern::runtime::current_task_environment();
    co_return env.stop_token.stop_requested();
}

auto observe_cancel_state_after_suspend(std::coroutine_handle<>* parked) -> modern::net::Task<bool> {
    co_await ManualSuspend{parked};
    auto env = co_await modern::runtime::current_task_environment();
    co_return env.stop_token.stop_requested();
}

} // namespace

// Simple tests for IoTask
TEST(IoTaskUnit, ReturnValue) {
    auto make_task = [&]() -> modern::net::Task<int> {
        co_return 42;
    };
    auto t = make_task();
    int v = t.get();
    ASSERT_EQ(v, 42);
}

TEST(IoTaskUnit, ExceptionPropagation) {
    auto make_task = [&]() -> modern::net::Task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };
    try {
        auto t = make_task();
        // get() should rethrow
        (void)t.get();
        FAIL() << "expected exception";
    } catch (const std::runtime_error& e) {
        ASSERT_STREQ(e.what(), "boom");
    }
}

TEST(IoTaskUnit, MoveAndVoidTaskCompletion) {
    // simple IoTask<void> that sets a flag when run
    bool ran = false;
    auto make_void = [&]() -> modern::net::Task<void> {
        ran = true;
        co_return;
    };
    auto t = make_void();
    // test move semantics
    auto t2 = std::move(t);
    // waiting/get should complete and not throw
    EXPECT_NO_THROW(t2.get());
    EXPECT_TRUE(ran);
}

TEST(IoTaskUnit, LegacyTaskNameRemainsCompatible) {
    static_assert(std::same_as<modern::net::Task<int>, net_io::Task<int>>);
    static_assert(std::same_as<modern::net::IoTask<int>, net_io::IoTask<int>>);
}

TEST(IoTaskUnit, LazyStartDoesNotRunOnConstruction) {
    bool ran = false;

    auto make_task = [&]() -> modern::net::Task<int> {
        ran = true;
        co_return 7;
    };

    auto t = make_task();
    EXPECT_FALSE(ran);
    EXPECT_FALSE(t.done());

    t.start();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(t.done());
    EXPECT_EQ(t.get(), 7);
}

TEST(IoTaskUnit, StartIsIdempotent) {
    int runs = 0;

    auto make_task = [&]() -> modern::net::Task<void> {
        ++runs;
        co_return;
    };

    auto t = make_task();
    EXPECT_EQ(runs, 0);

    t.start();
    t.start();

    EXPECT_EQ(runs, 1);
    EXPECT_NO_THROW(t.get());
}

TEST(IoTaskUnit, CancellationBeforeStartIsVisibleInTaskEnvironment) {
    std::stop_source source;

    auto t = observe_cancel_state();
    auto env = t.environment();
    env.stop_token = source.get_token();
    t.set_environment(env);

    source.request_stop();
    EXPECT_TRUE(t.get());
}

TEST(IoTaskUnit, CancellationDuringSuspendIsVisibleAfterResume) {
    std::stop_source source;
    std::coroutine_handle<> parked{};

    auto t = observe_cancel_state_after_suspend(&parked);
    auto env = t.environment();
    env.stop_token = source.get_token();
    t.set_environment(env);

    t.start();
    ASSERT_TRUE(static_cast<bool>(parked));

    source.request_stop();
    parked.resume();

    EXPECT_TRUE(t.get());
}

TEST(IoTaskUnit, CancellationAfterCompletionDoesNotChangeCompletedResult) {
    std::stop_source source;

    auto t = []() -> modern::net::Task<int> { co_return 11; }();
    auto env = t.environment();
    env.stop_token = source.get_token();
    t.set_environment(env);

    const int result = t.get();
    source.request_stop();

    EXPECT_EQ(result, 11);
}
