import modern_io_async;

#include <gtest/gtest.h>
#include <coroutine>
#include <expected>
#include <memory_resource>

using modern::io::ExpectedTask;

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
    explicit FixedTraceContextPolicy(std::optional<modern::io::TraceContext> trace_context) noexcept
        : trace_context_(std::move(trace_context)) {}

    std::optional<modern::io::TraceContext> get_trace_context() noexcept override {
        return trace_context_;
    }

private:
    std::optional<modern::io::TraceContext> trace_context_;
};

static modern::io::TraceContext make_trace_context() {
    modern::io::TraceContext context;
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
    context.trace_flags = 0x01;
    return context;
}

static modern::io::TraceContext make_alternate_trace_context() {
    auto context = make_trace_context();
    context.span_id[7] = std::byte{0x55};
    return context;
}

static ExpectedTask<int> make_expected_value_task(int value) {
    co_return std::expected<int, std::error_code>{ value };
}

static ExpectedTask<std::optional<modern::io::TraceContext>> read_current_trace_context_task() {
    co_return std::expected<std::optional<modern::io::TraceContext>, std::error_code>{
        co_await modern::io::current_trace_context()
    };
}

static ExpectedTask<std::optional<modern::io::TraceContext>> read_child_trace_context_task() {
    co_return co_await read_current_trace_context_task();
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
        auto result = task.sync_wait();
        ASSERT_TRUE((bool)result);
        EXPECT_EQ(result.value(), 7);
    }

    EXPECT_EQ(modern::io::expected_task_memory_resource_policy(), previous_policy);

    {
        auto task = make_expected_value_task(9);
        auto result = task.sync_wait();
        ASSERT_TRUE((bool)result);
        EXPECT_EQ(result.value(), 9);
    }
}

TEST(ExpectedTaskTest, CurrentTraceContextIsEmptyWithoutSeed) {
    auto task = read_current_trace_context_task();
    auto result = task.sync_wait();

    ASSERT_TRUE((bool)result);
    EXPECT_FALSE(result.value().has_value());
}

TEST(ExpectedTaskTest, ChildTaskInheritsParentTraceContext) {
    const auto trace_context = make_trace_context();

    auto task = read_child_trace_context_task();
    task.set_trace_context(trace_context);

    auto result = task.sync_wait();
    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), trace_context);
}

TEST(ExpectedTaskTest, RootTaskUsesConfiguredTraceContextPolicy) {
    const auto trace_context = make_trace_context();
    FixedTraceContextPolicy policy(trace_context);

    auto task = read_current_trace_context_task();
    modern::io::ExpectedTaskTraceContextPolicyScope scope(&policy);
    auto result = task.sync_wait();

    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), trace_context);
}

TEST(ExpectedTaskTest, ExplicitTraceContextOverridesRootPolicy) {
    const auto seeded_context = make_trace_context();
    const auto explicit_context = make_alternate_trace_context();
    FixedTraceContextPolicy policy(seeded_context);

    auto task = read_current_trace_context_task();
    task.set_trace_context(explicit_context);

    modern::io::ExpectedTaskTraceContextPolicyScope scope(&policy);
    auto result = task.sync_wait();

    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), explicit_context);
}