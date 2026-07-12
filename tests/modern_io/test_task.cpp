import modern_io_async;
import modern.runtime;

#include <gtest/gtest.h>
#include <coroutine>
#include <expected>
#include <memory_resource>
#include <stop_token>
#include <type_traits>
#include <utility>

using modern::io::ExpectedTask;

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

struct TransformProbe {
    int operator()(int value) const {
        return value * 2;
    }
};

struct OrElseProbe {
    std::expected<int, std::error_code> operator()(std::error_code error) const {
        return std::unexpected(error);
    }
};

template<class T>
auto detect_transform(int) -> decltype(std::declval<T&&>().transform(TransformProbe{}), std::true_type{});

template<class>
auto detect_transform(...) -> std::false_type;

template<class T>
auto detect_or_else(int) -> decltype(std::declval<T&&>().or_else(OrElseProbe{}), std::true_type{});

template<class>
auto detect_or_else(...) -> std::false_type;

} // namespace

class CountingMemoryResource : public std::pmr::memory_resource {
public:
    std::size_t allocation_count = 0;
    std::size_t deallocation_count = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocation_count;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        ++deallocation_count;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

class FixedMemoryResourcePolicy final : public modern::io::ExpectedTaskMemoryResourcePolicy {
public:
    explicit FixedMemoryResourcePolicy(std::pmr::memory_resource* resource) noexcept
        : resource_(resource) {}

    std::pmr::memory_resource* get_memory_resource() noexcept override {
        return resource_;
    }

private:
    std::pmr::memory_resource* resource_;
};

class FixedTraceContextPolicy final : public modern::io::ExpectedTaskTraceContextPolicy {
public:
    explicit FixedTraceContextPolicy(std::optional<modern::trace::TraceContext> trace_context) noexcept
        : trace_context_(std::move(trace_context)) {}

    std::optional<modern::trace::TraceContext> get_trace_context() noexcept override {
        return trace_context_;
    }

private:
    std::optional<modern::trace::TraceContext> trace_context_;
};

static modern::trace::TraceContext make_trace_context() {
    modern::trace::TraceContext context;
    context.version = 0x00;
    context.trace_id = {
        std::byte{0x4b}, std::byte{0xf9}, std::byte{0x2f}, std::byte{0x35},
        std::byte{0x77}, std::byte{0xb3}, std::byte{0x4d}, std::byte{0xa6},
        std::byte{0xa3}, std::byte{0xce}, std::byte{0x92}, std::byte{0x9d},
        std::byte{0x0e}, std::byte{0x0e}, std::byte{0x47}, std::byte{0x36},
    };
    context.span_id = {
        std::byte{0x00}, std::byte{0xf0}, std::byte{0x67}, std::byte{0xaa},
        std::byte{0x0b}, std::byte{0xa9}, std::byte{0x02}, std::byte{0xb7},
    };
    context.flags = 0x01;
    return context;
}

static modern::trace::TraceContext make_alternate_trace_context() {
    auto context = make_trace_context();
    context.span_id[7] = std::byte{0x55};
    return context;
}

static ExpectedTask<int> make_expected_value_task(int value) {
    co_return std::expected<int, std::error_code>{ value };
}

static ExpectedTask<std::optional<modern::trace::TraceContext>> read_current_trace_context_task() {
    co_return std::expected<std::optional<modern::trace::TraceContext>, std::error_code>{
        co_await modern::io::current_trace_context()
    };
}

static ExpectedTask<std::optional<modern::trace::TraceContext>> read_child_trace_context_task() {
    co_return co_await read_current_trace_context_task();
}

static ExpectedTask<int> make_lazy_expected_task(bool& ran) {
    ran = true;
    co_return std::expected<int, std::error_code>{ 5 };
}

static ExpectedTask<void> make_counted_expected_task(int& runs) {
    ++runs;
    co_return std::expected<void, std::error_code>{};
}

static ExpectedTask<int> make_expected_error_task(std::errc error) {
    co_return std::unexpected(std::make_error_code(error));
}

static ExpectedTask<void> make_expected_void_task() {
    co_return std::expected<void, std::error_code>{};
}

static ExpectedTask<bool> observe_expected_cancel_state() {
    auto env = co_await modern::this_task::environment();
    co_return std::expected<bool, std::error_code>{ env.stop_token.stop_requested() };
}

static ExpectedTask<bool> observe_expected_cancel_state_after_suspend(std::coroutine_handle<>* parked) {
    co_await ManualSuspend{parked};
    auto env = co_await modern::this_task::environment();
    co_return std::expected<bool, std::error_code>{ env.stop_token.stop_requested() };
}

TEST(ExpectedTaskTest, UsesConfiguredPolicyForCoroutineFrames) {
    using Promise = ExpectedTask<int>::promise_type;
    CountingMemoryResource resource;
    FixedMemoryResourcePolicy policy(&resource);
    auto* previous_policy = modern::io::expected_task_memory_resource_policy();

    {
        modern::io::ExpectedTaskMemoryResourcePolicyScope scope(&policy);
        EXPECT_EQ(modern::io::expected_task_memory_resource_policy(), &policy);
        EXPECT_EQ(modern::io::expected_task_memory_resource(), &resource);

        void* frame = Promise::operator new(sizeof(Promise));
        ASSERT_NE(frame, nullptr);
        Promise::operator delete(frame, sizeof(Promise));
    }

    EXPECT_EQ(modern::io::expected_task_memory_resource_policy(), previous_policy);
    EXPECT_EQ(resource.allocation_count, 1u);
    EXPECT_EQ(resource.deallocation_count, resource.allocation_count);
}

TEST(ExpectedTaskTest, ResourceScopeRestoresPreviousPolicy) {
    CountingMemoryResource resource;
    auto* previous_policy = modern::io::expected_task_memory_resource_policy();

    {
        modern::io::ExpectedTaskMemoryResourceScope scope(&resource);
        EXPECT_EQ(modern::io::expected_task_memory_resource(), &resource);
        auto task = make_expected_value_task(7);
        auto result = task.get();
        ASSERT_TRUE((bool)result);
        EXPECT_EQ(result.value(), 7);
    }

    EXPECT_EQ(modern::io::expected_task_memory_resource_policy(), previous_policy);

    {
        auto task = make_expected_value_task(9);
        auto result = task.get();
        ASSERT_TRUE((bool)result);
        EXPECT_EQ(result.value(), 9);
    }
}

TEST(ExpectedTaskTest, CurrentTraceContextIsEmptyWithoutSeed) {
    auto task = read_current_trace_context_task();
    auto result = task.get();

    ASSERT_TRUE((bool)result);
    EXPECT_FALSE(result.value().has_value());
}

TEST(ExpectedTaskTest, ChildTaskInheritsParentTraceContext) {
    const auto trace_context = make_trace_context();

    std::expected<std::optional<modern::trace::TraceContext>, std::error_code> result;
    {
        modern::trace_context_scope scope(trace_context);
        auto task = read_child_trace_context_task();
        result = task.get();
    }
    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), trace_context);
}

TEST(ExpectedTaskTest, RootTaskUsesConfiguredTraceContextPolicy) {
    const auto trace_context = make_trace_context();
    FixedTraceContextPolicy policy(trace_context);

    modern::io::ExpectedTaskTraceContextPolicyScope scope(&policy);
    auto task = read_current_trace_context_task();
    auto result = task.get();

    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), trace_context);
}

TEST(ExpectedTaskTest, ExplicitTraceContextOverridesRootPolicy) {
    const auto seeded_context = make_trace_context();
    const auto explicit_context = make_alternate_trace_context();
    FixedTraceContextPolicy policy(seeded_context);

    modern::io::ExpectedTaskTraceContextPolicyScope scope(&policy);
    std::expected<std::optional<modern::trace::TraceContext>, std::error_code> result;
    {
        modern::trace_context_scope explicit_scope(explicit_context);
        auto task = read_current_trace_context_task();
        result = task.get();
    }

    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), explicit_context);
}

TEST(ExpectedTaskTest, LazyStartDoesNotRunOnConstruction) {
    bool ran = false;
    auto task = make_lazy_expected_task(ran);

    EXPECT_TRUE(ran);
    EXPECT_TRUE(task.ready());

    auto result = task.get();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 5);
    EXPECT_TRUE(ran);
}

TEST(ExpectedTaskTest, StartIsIdempotent) {
    int runs = 0;
    auto task = make_counted_expected_task(runs);

    EXPECT_EQ(runs, 1);
    task.start();
    task.start();

    EXPECT_EQ(runs, 1);
    auto result = task.get();
    EXPECT_TRUE((bool)result);
}

TEST(ExpectedTaskTest, CancellationScopeIsCapturedAtConstruction) {
    std::stop_source source;
    source.request_stop();

    auto env = modern::current_task_environment_value();
    env.stop_token = source.get_token();
    modern::task_environment_scope scope(env);
    auto task = observe_expected_cancel_state();
    auto result = task.get();
    ASSERT_TRUE((bool)result);
    EXPECT_TRUE(result.value());
}

TEST(ExpectedTaskTest, CancellationDuringSuspendIsVisibleAfterResume) {
    std::stop_source source;
    std::coroutine_handle<> parked{};

    auto env = modern::current_task_environment_value();
    env.stop_token = source.get_token();
    modern::task_environment_scope scope(env);
    auto task = observe_expected_cancel_state_after_suspend(&parked);

    task.start();
    ASSERT_TRUE(static_cast<bool>(parked));

    source.request_stop();
    parked.resume();

    auto result = task.get();
    ASSERT_TRUE((bool)result);
    EXPECT_TRUE(result.value());
}

TEST(ExpectedTaskTest, CancellationAfterCompletionDoesNotChangeCompletedResult) {
    std::stop_source source;

    auto env = modern::current_task_environment_value();
    env.stop_token = source.get_token();
    modern::task_environment_scope scope(env);
    auto task = []() -> ExpectedTask<int> {
        co_return std::expected<int, std::error_code>{ 11 };
    }();

    auto result = task.get();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 11);

    source.request_stop();
}

TEST(ExpectedTaskTest, TransformMapsExpectedValue) {
    auto task = make_expected_value_task(7);

    auto result = std::move(task).then([](std::expected<int, std::error_code> value) {
        if (!value)
            return value;
        return std::expected<int, std::error_code>{*value * 3};
    }).get();

    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 21);
}

TEST(ExpectedTaskTest, TransformPreservesErrorWithoutInvokingMapper) {
    bool invoked = false;
    auto task = make_expected_error_task(std::errc::permission_denied);

    auto result = std::move(task).then([&](std::expected<int, std::error_code> value) {
        if (!value)
            return value;
        invoked = true;
        return std::expected<int, std::error_code>{*value * 3};
    }).get();

    ASSERT_FALSE((bool)result);
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::permission_denied));
    EXPECT_FALSE(invoked);
}

TEST(ExpectedTaskTest, TransformSupportsVoidExpectedValue) {
    auto task = make_expected_void_task();

    auto result = std::move(task).then([](std::expected<void, std::error_code> value) {
        if (!value)
            return std::expected<int, std::error_code>{std::unexpected(value.error())};
        return std::expected<int, std::error_code>{9};
    }).get();

    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 9);
}

TEST(ExpectedTaskTest, OrElseRecoversFromExpectedError) {
    auto task = make_expected_error_task(std::errc::broken_pipe);

    auto result = std::move(task).then([](std::expected<int, std::error_code> value) {
        EXPECT_FALSE(value);
        EXPECT_EQ(value.error(), std::make_error_code(std::errc::broken_pipe));
        return std::expected<int, std::error_code>{42};
    }).get();

    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 42);
}

TEST(ExpectedTaskTest, OrElsePreservesValueWithoutInvokingHandler) {
    bool invoked = false;
    auto task = make_expected_value_task(13);

    auto result = std::move(task).then([&](std::expected<int, std::error_code> value) {
        if (value)
            return value;
        invoked = true;
        return std::expected<int, std::error_code>{std::unexpected(value.error())};
    }).get();

    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 13);
    EXPECT_FALSE(invoked);
}

TEST(ExpectedTaskTest, ThenValueMapsExpectedValue) {
    auto task = make_expected_value_task(5);

    auto result = std::move(task).then([](std::expected<int, std::error_code> value) {
        if (!value)
            return value;
        return std::expected<int, std::error_code>{*value + 8};
    }).get();

    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 13);
}

TEST(ExpectedTaskTest, ThenValueOnOverridesSchedulerForExpectedChain) {
    auto override_scheduler = modern::inline_scheduler();
    auto task = make_expected_value_task(6);

    auto chained = std::move(task).then_on(override_scheduler, [](std::expected<int, std::error_code> value) {
        if (!value)
            return value;
        return std::expected<int, std::error_code>{*value + 4};
    });

    auto result = chained.get();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 10);
}

TEST(ExpectedTaskTest, ThenErrorRecoversFromExpectedError) {
    auto task = make_expected_error_task(std::errc::timed_out);

    auto result = std::move(task).then([](std::expected<int, std::error_code> value) {
        EXPECT_FALSE(value);
        EXPECT_EQ(value.error(), std::make_error_code(std::errc::timed_out));
        return std::expected<int, std::error_code>{77};
    }).get();

    ASSERT_TRUE((bool)result);
    EXPECT_EQ(result.value(), 77);
}

TEST(ExpectedTaskTest, ExceptionTaskDoesNotExposeExpectedChannelOperators) {
    using RuntimeTask = modern::task<int>;

    static_assert(!decltype(detect_transform<RuntimeTask>(0))::value);
    static_assert(!decltype(detect_or_else<RuntimeTask>(0))::value);
}
