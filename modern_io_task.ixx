module;

#ifndef _MSC_VER
#include <expected>
#include <memory>
#include <memory_resource>
#include <new>
#include <coroutine>
#include <system_error>
#include <latch>
#include <optional>
#else
export import <expected>;
export import <memory>;
export import <memory_resource>;
export import <new>;
export import <coroutine>;
export import <system_error>;
export import <latch>;
export import <optional>;
#endif

export module modern_io.task;

import modern_io.trace;

export namespace modern_io {

class ExpectedTaskTraceContextPolicy {
public:
    virtual ~ExpectedTaskTraceContextPolicy() = default;
    [[nodiscard]] virtual std::optional<TraceContext> get_trace_context() noexcept = 0;
};

class ExpectedTaskMemoryResourcePolicy {
public:
    virtual ~ExpectedTaskMemoryResourcePolicy() = default;
    [[nodiscard]] virtual std::pmr::memory_resource* get_memory_resource() noexcept = 0;
};

namespace detail {

class FixedExpectedTaskMemoryResourcePolicy final : public ExpectedTaskMemoryResourcePolicy {
public:
    explicit FixedExpectedTaskMemoryResourcePolicy(std::pmr::memory_resource* resource = nullptr) noexcept
        : resource_(resource) {}

    void set_memory_resource(std::pmr::memory_resource* resource) noexcept {
        resource_ = resource;
    }

    [[nodiscard]] std::pmr::memory_resource* get_memory_resource() noexcept override {
        return resource_;
    }

private:
    std::pmr::memory_resource* resource_{};
};

struct ExpectedTaskAllocationHeader {
    std::pmr::memory_resource* resource{};
    void* raw{};
    std::size_t raw_size{};
    std::size_t raw_alignment{};
};

inline ExpectedTaskMemoryResourcePolicy*& expected_task_memory_resource_policy_storage() noexcept {
    thread_local ExpectedTaskMemoryResourcePolicy* policy = nullptr;
    return policy;
}

inline ExpectedTaskTraceContextPolicy*& expected_task_trace_context_policy_storage() noexcept {
    thread_local ExpectedTaskTraceContextPolicy* policy = nullptr;
    return policy;
}

inline FixedExpectedTaskMemoryResourcePolicy& expected_task_legacy_memory_resource_policy() noexcept {
    thread_local FixedExpectedTaskMemoryResourcePolicy policy;
    return policy;
}

inline std::pmr::synchronized_pool_resource& expected_task_default_pool() {
    static std::pmr::synchronized_pool_resource pool;
    return pool;
}

inline std::pmr::memory_resource* active_expected_task_memory_resource() {
    auto* policy = expected_task_memory_resource_policy_storage();
    if (policy) {
        if (auto* resource = policy->get_memory_resource()) {
            return resource;
        }
    }
    return &expected_task_default_pool();
}

inline void* allocate_expected_task_frame(std::size_t bytes, std::size_t alignment) {
    auto* resource = active_expected_task_memory_resource();
    const std::size_t header_size = sizeof(ExpectedTaskAllocationHeader);
    const std::size_t total_size = header_size + bytes + alignment;

    void* raw = resource->allocate(total_size, alignof(std::max_align_t));
    auto* begin = static_cast<std::byte*>(raw) + header_size;
    void* aligned = begin;
    std::size_t space = total_size - header_size;

    if (std::align(alignment, bytes, aligned, space) == nullptr) {
        resource->deallocate(raw, total_size, alignof(std::max_align_t));
        throw std::bad_alloc();
    }

    auto* header = reinterpret_cast<ExpectedTaskAllocationHeader*>(
        static_cast<std::byte*>(aligned) - header_size);
    *header = ExpectedTaskAllocationHeader{ resource, raw, total_size, alignof(std::max_align_t) };
    return aligned;
}

inline void deallocate_expected_task_frame(void* ptr) noexcept {
    if (!ptr) {
        return;
    }

    const std::size_t header_size = sizeof(ExpectedTaskAllocationHeader);
    auto* header = reinterpret_cast<ExpectedTaskAllocationHeader*>(
        static_cast<std::byte*>(ptr) - header_size);
    header->resource->deallocate(header->raw, header->raw_size, header->raw_alignment);
}

} // namespace detail

[[nodiscard]] inline ExpectedTaskMemoryResourcePolicy* expected_task_memory_resource_policy() noexcept {
    return detail::expected_task_memory_resource_policy_storage();
}

[[nodiscard]] inline ExpectedTaskTraceContextPolicy* expected_task_trace_context_policy() noexcept {
    return detail::expected_task_trace_context_policy_storage();
}

inline ExpectedTaskMemoryResourcePolicy*
set_expected_task_memory_resource_policy(ExpectedTaskMemoryResourcePolicy* policy) noexcept {
    auto*& current = detail::expected_task_memory_resource_policy_storage();
    auto* previous = current;
    current = policy;
    return previous;
}

inline ExpectedTaskTraceContextPolicy*
set_expected_task_trace_context_policy(ExpectedTaskTraceContextPolicy* policy) noexcept {
    auto*& current = detail::expected_task_trace_context_policy_storage();
    auto* previous = current;
    current = policy;
    return previous;
}

class ExpectedTaskMemoryResourcePolicyScope {
public:
    explicit ExpectedTaskMemoryResourcePolicyScope(ExpectedTaskMemoryResourcePolicy* policy) noexcept
        : previous_(set_expected_task_memory_resource_policy(policy)) {}

    ExpectedTaskMemoryResourcePolicyScope(const ExpectedTaskMemoryResourcePolicyScope&) = delete;
    ExpectedTaskMemoryResourcePolicyScope& operator=(const ExpectedTaskMemoryResourcePolicyScope&) = delete;

    ~ExpectedTaskMemoryResourcePolicyScope() {
        set_expected_task_memory_resource_policy(previous_);
    }

private:
    ExpectedTaskMemoryResourcePolicy* previous_{};
};

class ExpectedTaskTraceContextPolicyScope {
public:
    explicit ExpectedTaskTraceContextPolicyScope(ExpectedTaskTraceContextPolicy* policy) noexcept
        : previous_(set_expected_task_trace_context_policy(policy)) {}

    ExpectedTaskTraceContextPolicyScope(const ExpectedTaskTraceContextPolicyScope&) = delete;
    ExpectedTaskTraceContextPolicyScope& operator=(const ExpectedTaskTraceContextPolicyScope&) = delete;

    ~ExpectedTaskTraceContextPolicyScope() {
        set_expected_task_trace_context_policy(previous_);
    }

private:
    ExpectedTaskTraceContextPolicy* previous_{};
};

[[nodiscard]] inline std::pmr::memory_resource* expected_task_memory_resource() {
    return detail::active_expected_task_memory_resource();
}

inline std::pmr::memory_resource*
set_expected_task_memory_resource(std::pmr::memory_resource* resource) noexcept {
    std::pmr::memory_resource* previous = nullptr;
    if (auto* current_policy = detail::expected_task_memory_resource_policy_storage()) {
        previous = current_policy->get_memory_resource();
    }

    if (!resource) {
        detail::expected_task_memory_resource_policy_storage() = nullptr;
        return previous;
    }

    auto& legacy_policy = detail::expected_task_legacy_memory_resource_policy();
    legacy_policy.set_memory_resource(resource);
    detail::expected_task_memory_resource_policy_storage() = &legacy_policy;
    return previous;
}

inline void reset_expected_task_memory_resource() noexcept {
    set_expected_task_memory_resource_policy(nullptr);
}

class ExpectedTaskMemoryResourceScope {
public:
    explicit ExpectedTaskMemoryResourceScope(std::pmr::memory_resource* resource) noexcept
        : previous_(set_expected_task_memory_resource(resource)) {}

    ExpectedTaskMemoryResourceScope(const ExpectedTaskMemoryResourceScope&) = delete;
    ExpectedTaskMemoryResourceScope& operator=(const ExpectedTaskMemoryResourceScope&) = delete;

    ~ExpectedTaskMemoryResourceScope() {
        set_expected_task_memory_resource(previous_);
    }

private:
    std::pmr::memory_resource* previous_{};
};

struct CurrentTraceContextAwaiter {
    std::optional<TraceContext> context;

    bool await_ready() const noexcept { return false; }

    template<typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> current) noexcept {
        if constexpr (requires(Promise& promise) { promise.trace_context(); }) {
            context = current.promise().trace_context();
        }
        return false;
    }

    std::optional<TraceContext> await_resume() const noexcept {
        return context;
    }
};

[[nodiscard]] inline CurrentTraceContextAwaiter current_trace_context() noexcept {
    return {};
}

template<typename T>
class ExpectedTask {
public:
    struct promise_type {
        std::coroutine_handle<> continuation_{};
        std::expected<T,std::error_code> result_{ std::unexpected(std::make_error_code(std::errc::operation_not_permitted)) };
        std::latch* wait_latch_{nullptr};
        std::optional<TraceContext> trace_context_{};
        bool started_{false};

        static void* operator new(std::size_t bytes) {
            return detail::allocate_expected_task_frame(bytes, alignof(promise_type));
        }

        static void operator delete(void* ptr) noexcept {
            detail::deallocate_expected_task_frame(ptr);
        }

        static void operator delete(void* ptr, std::size_t) noexcept {
            detail::deallocate_expected_task_frame(ptr);
        }

        [[nodiscard]] std::optional<TraceContext>& trace_context() noexcept {
            return trace_context_;
        }

        [[nodiscard]] const std::optional<TraceContext>& trace_context() const noexcept {
            return trace_context_;
        }

        void inherit_trace_context(const std::optional<TraceContext>& parent_context) noexcept {
            if (!trace_context_ && parent_context) {
                trace_context_ = *parent_context;
            }
        }

        void seed_trace_context_if_missing() noexcept {
            if (!trace_context_) {
                if (auto* policy = detail::expected_task_trace_context_policy_storage()) {
                    trace_context_ = policy->get_trace_context();
                }
            }
        }

        ExpectedTask get_return_object() noexcept {
            return ExpectedTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }
    // Keep suspend_always initially: explicit start is possible,
    // but we now auto-start in await_suspend.
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct final_awaitable {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& p = h.promise();
                if (p.wait_latch_) {
                    p.wait_latch_->count_down();
                }
                if (p.continuation_) {
                    return p.continuation_;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        final_awaitable final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {
            result_ = std::unexpected(std::make_error_code(std::errc::io_error));
        }
        void return_value(std::expected<T,std::error_code> v) noexcept {
            result_ = std::move(v);
        }
    };

    using handle = std::coroutine_handle<promise_type>;
    explicit ExpectedTask(handle h) : h_(h) {}
    ExpectedTask(ExpectedTask&& o) noexcept : h_(o.h_) { o.h_ = {}; }
    ExpectedTask& operator=(ExpectedTask&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = o.h_;
            o.h_ = {};
        }
        return *this;
    }
    ExpectedTask(const ExpectedTask&) = delete;
    ExpectedTask& operator=(const ExpectedTask&) = delete;
    ~ExpectedTask() { if (h_) h_.destroy(); }

    bool await_ready() const noexcept { return !h_ || h_.done(); }

    // NEW: auto-start on first await
    template<typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> cont) noexcept {
        auto& p = h_.promise();
        p.continuation_ = cont;
        if constexpr (requires(Promise& promise) { promise.trace_context(); }) {
            p.inherit_trace_context(cont.promise().trace_context());
        }
        p.seed_trace_context_if_missing();
        if (!p.started_) {
            p.started_ = true;
        }
        return h_;
    }

    std::expected<T,std::error_code> await_resume() noexcept {
        return std::move(h_.promise().result_);
    }

    void set_trace_context(TraceContext trace_context) noexcept {
        if (h_) {
            h_.promise().trace_context() = std::move(trace_context);
        }
    }

    void clear_trace_context() noexcept {
        if (h_) {
            h_.promise().trace_context().reset();
        }
    }

    [[nodiscard]] std::optional<TraceContext> trace_context() const noexcept {
        if (!h_) {
            return std::nullopt;
        }
        return h_.promise().trace_context();
    }

    void start() noexcept {
        if (h_ && !h_.done() && !h_.promise().started_) {
            h_.promise().seed_trace_context_if_missing();
            h_.promise().started_ = true;
            h_.resume();
        }
    }

    std::expected<T,std::error_code> sync_wait() {
        if (!h_) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (h_.done()) {
            return std::move(h_.promise().result_);
        }
        std::latch done(1);
        h_.promise().wait_latch_ = &done;
        start();              // idempotent
        done.wait();
        return std::move(h_.promise().result_);
    }

private:
    template<typename U>
    friend class ExpectedTask;
    handle h_{};
};

// Alias (optional)
template<typename T>
using ExpectedTaskT = ExpectedTask<T>;

} // namespace modern_io