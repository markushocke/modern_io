#include <gtest/gtest.h>
#include <string>
#include <coroutine>

import net_io.async_utils; // IoTask alias -> net_io::Task

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
