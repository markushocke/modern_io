#include <gtest/gtest.h>

#include <coroutine>
#include <expected>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

import modern.runtime;

namespace {

struct ThenProbe {
    int operator()(std::expected<int, std::error_code>) const {
        return 1;
    }
};

struct CatchProbe {
    std::expected<int, std::error_code> operator()(std::exception_ptr) const {
        return 1;
    }
};

struct FinallyProbe {
    void operator()() const {}
};

template<class T>
auto detect_then(int) -> decltype(std::declval<T&&>().then(ThenProbe{}), std::true_type{});

template<class>
auto detect_then(...) -> std::false_type;

template<class T>
auto detect_catching(int) -> decltype(std::declval<T&&>().catching(CatchProbe{}), std::true_type{});

template<class>
auto detect_catching(...) -> std::false_type;

template<class T>
auto detect_finally(int) -> decltype(std::declval<T&&>().finally(FinallyProbe{}), std::true_type{});

template<class>
auto detect_finally(...) -> std::false_type;

} // namespace

TEST(RuntimeTaskFluentTest, ThenTransformsValueAndRemainsLazyUntilActivation) {
    auto source = []() -> modern::task<int> {
        co_return 21;
    }();

    auto chained = std::move(source).then([](int value) { return value * 2; });

    EXPECT_EQ(chained.get(), 42);
}

TEST(RuntimeTaskFluentTest, ThenSupportsTaskReturningContinuation) {
    auto source = []() -> modern::task<int> {
        co_return 3;
    }();

    auto chained = std::move(source).then([](int value) -> modern::task<int> {
        co_return value + 4;
    });

    EXPECT_EQ(chained.get(), 7);
}

TEST(RuntimeTaskFluentTest, ThenSupportsVoidSourceTask) {
    auto source = []() -> modern::task<void> {
        co_return;
    }();

    auto chained = std::move(source).then([] { return 9; });

    EXPECT_EQ(chained.get(), 9);
}

TEST(RuntimeTaskFluentTest, ThenWithoutOverrideKeepsSchedulerUnset) {
    auto parent_scheduler = modern::inline_scheduler();

    auto parent_env = modern::current_task_environment_value();
    parent_env.scheduler = parent_scheduler;
    modern::task_environment_scope scope(parent_env);
    auto source = []() -> modern::task<int> {
        co_return 10;
    }();

    auto chained = std::move(source).then([](int value) -> modern::task<int> {
        auto env = co_await modern::this_task::environment();
        EXPECT_TRUE(env.scheduler.valid());
        co_return value + 1;
    });

    EXPECT_EQ(chained.get(), 11);
}

TEST(RuntimeTaskFluentTest, ThenOnOverridesSchedulerForContinuationChain) {
    auto parent_scheduler = modern::inline_scheduler();
    auto override_scheduler = modern::inline_scheduler();

    auto parent_env = modern::current_task_environment_value();
    parent_env.scheduler = parent_scheduler;
    modern::task_environment_scope scope(parent_env);
    auto source = []() -> modern::task<int> {
        co_return 20;
    }();

    auto chained = std::move(source).then_on(override_scheduler, [&override_scheduler](int value) -> modern::task<int> {
        auto env = co_await modern::this_task::environment();
        EXPECT_TRUE(env.scheduler.valid());
        co_return value + 2;
    });

    EXPECT_EQ(chained.get(), 22);
}

TEST(RuntimeTaskFluentTest, CatchingRecoversFromException) {
    auto source = []() -> modern::task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    }();

    auto recovered = std::move(source).catching([](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::runtime_error&) {
            return 5;
        }

        return -1;
    });

    EXPECT_EQ(recovered.get(), 5);
}

TEST(RuntimeTaskFluentTest, CatchingSupportsVoidRecoveryPath) {
    bool recovered = false;

    auto source = []() -> modern::task<void> {
        throw std::runtime_error("fail");
        co_return;
    }();

    auto chained = std::move(source).catching([&](std::exception_ptr) {
        recovered = true;
    });

    EXPECT_NO_THROW(chained.get());
    EXPECT_TRUE(recovered);
}

TEST(RuntimeTaskFluentTest, FinallyRunsOnSuccessAndFailure) {
    int finally_calls = 0;

    auto success = []() -> modern::task<int> {
        co_return 11;
    }();

    auto success_chained = std::move(success).finally([&] {
        ++finally_calls;
    });

    EXPECT_EQ(success_chained.get(), 11);

    auto failure = []() -> modern::task<int> {
        throw std::runtime_error("x");
        co_return 0;
    }();

    auto failure_chained = std::move(failure).finally([&] {
        ++finally_calls;
    });

    EXPECT_THROW((void)failure_chained.get(), std::runtime_error);
    EXPECT_EQ(finally_calls, 2);
}

TEST(RuntimeTaskFluentTest, ResultTaskUsesUnifiedTaskFluentOperators) {
    using ResultType = modern::result_task<int, std::error_code>;

    static_assert(decltype(detect_then<ResultType>(0))::value);
    static_assert(decltype(detect_catching<ResultType>(0))::value);
    static_assert(decltype(detect_finally<ResultType>(0))::value);
}
