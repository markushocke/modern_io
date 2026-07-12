module;

#ifndef _MSC_VER
#include <coroutine>
#include <expected>
#include <memory>
#include <memory_resource>
#include <optional>
#include <system_error>
#else
export import <coroutine>;
export import <expected>;
export import <memory_resource>;
export import <optional>;
export import <system_error>;
#endif

export module modern_io.task;

import modern.runtime;
import modern.trace;

export namespace modern::io
{
class ExpectedTaskMemoryResourcePolicy
{
public:
    virtual ~ExpectedTaskMemoryResourcePolicy() = default;
    [[nodiscard]] virtual std::pmr::memory_resource* get_memory_resource() noexcept = 0;

    modern::task_environment current_environment() noexcept
    {
        modern::task_environment environment = modern::current_task_environment_value();
        environment.frame_resource = get_memory_resource();
        return environment;
    }
};

class ExpectedTaskTraceContextPolicy
{
public:
    virtual ~ExpectedTaskTraceContextPolicy() = default;
    [[nodiscard]] virtual std::optional<modern::trace::TraceContext> get_trace_context() noexcept = 0;

    modern::task_environment current_environment() noexcept
    {
        modern::task_environment environment = modern::current_task_environment_value();

        if (auto trace = get_trace_context())
            environment.trace_context = *trace;

        return environment;
    }
};

namespace detail
{
inline ExpectedTaskMemoryResourcePolicy*& expected_task_memory_policy_storage() noexcept
{
    thread_local ExpectedTaskMemoryResourcePolicy* value = nullptr;
    return value;
}

inline ExpectedTaskTraceContextPolicy*& expected_task_trace_policy_storage() noexcept
{
    thread_local ExpectedTaskTraceContextPolicy* value = nullptr;
    return value;
}

inline std::optional<modern::task_environment_scope>& expected_task_memory_resource_scope_storage() noexcept
{
    thread_local std::optional<modern::task_environment_scope> value;
    return value;
}
} // namespace detail

[[nodiscard]] inline ExpectedTaskMemoryResourcePolicy* expected_task_memory_resource_policy() noexcept
{
    return detail::expected_task_memory_policy_storage();
}

[[nodiscard]] inline ExpectedTaskTraceContextPolicy* expected_task_trace_context_policy() noexcept
{
    return detail::expected_task_trace_policy_storage();
}

inline ExpectedTaskMemoryResourcePolicy*
set_expected_task_memory_resource_policy(ExpectedTaskMemoryResourcePolicy* policy) noexcept
{
    auto* previous = expected_task_memory_resource_policy();
    detail::expected_task_memory_policy_storage() = policy;
    return previous;
}

inline ExpectedTaskTraceContextPolicy*
set_expected_task_trace_context_policy(ExpectedTaskTraceContextPolicy* policy) noexcept
{
    auto* previous = expected_task_trace_context_policy();
    detail::expected_task_trace_policy_storage() = policy;
    return previous;
}

class ExpectedTaskMemoryResourcePolicyScope
{
public:
    explicit ExpectedTaskMemoryResourcePolicyScope(ExpectedTaskMemoryResourcePolicy* policy) noexcept
        : previous_(set_expected_task_memory_resource_policy(policy)),
          scope_(policy ? policy->current_environment() : modern::current_task_environment_value())
    {
    }

    ~ExpectedTaskMemoryResourcePolicyScope() noexcept
    {
        set_expected_task_memory_resource_policy(previous_);
    }

    ExpectedTaskMemoryResourcePolicyScope(const ExpectedTaskMemoryResourcePolicyScope&) = delete;
    ExpectedTaskMemoryResourcePolicyScope& operator=(const ExpectedTaskMemoryResourcePolicyScope&) = delete;

private:
    ExpectedTaskMemoryResourcePolicy* previous_;
    modern::task_environment_scope scope_;
};

class ExpectedTaskTraceContextPolicyScope
{
public:
    explicit ExpectedTaskTraceContextPolicyScope(ExpectedTaskTraceContextPolicy* policy) noexcept
        : previous_(set_expected_task_trace_context_policy(policy)),
          scope_(policy ? policy->current_environment() : modern::current_task_environment_value())
    {
    }

    ~ExpectedTaskTraceContextPolicyScope() noexcept
    {
        set_expected_task_trace_context_policy(previous_);
    }

    ExpectedTaskTraceContextPolicyScope(const ExpectedTaskTraceContextPolicyScope&) = delete;
    ExpectedTaskTraceContextPolicyScope& operator=(const ExpectedTaskTraceContextPolicyScope&) = delete;

private:
    ExpectedTaskTraceContextPolicy* previous_;
    modern::task_environment_scope scope_;
};

using ExpectedTaskMemoryResourceScope = modern::frame_resource_scope;

[[nodiscard]] inline std::pmr::memory_resource* expected_task_memory_resource() noexcept
{
    return modern::current_task_environment_value().frame_resource;
}

inline std::pmr::memory_resource*
set_expected_task_memory_resource(std::pmr::memory_resource* resource) noexcept
{
    auto* previous = expected_task_memory_resource();
    modern::task_environment environment = modern::current_task_environment_value();
    environment.frame_resource = resource;
    detail::expected_task_memory_resource_scope_storage().emplace(environment);
    return previous;
}

inline void reset_expected_task_memory_resource() noexcept
{
    detail::expected_task_memory_resource_scope_storage().reset();
}

class CurrentTraceContextAwaiter
{
public:
    [[nodiscard]] bool await_ready() const noexcept
    {
        return false;
    }

    template<class Promise>
    bool await_suspend(std::coroutine_handle<Promise> current) noexcept
    {
        auto runtime_awaiter = current.promise().await_transform(modern::this_task::trace_context());
        runtime_awaiter.await_suspend(current);
        auto runtime_trace = runtime_awaiter.await_resume();

        if (runtime_trace)
            trace_context_ = *runtime_trace;
        else
            trace_context_.reset();

        return false;
    }

    [[nodiscard]] std::optional<modern::trace::TraceContext> await_resume() const noexcept
    {
        return trace_context_;
    }

private:
    std::optional<modern::trace::TraceContext> trace_context_;
};

[[nodiscard]] inline CurrentTraceContextAwaiter current_trace_context() noexcept
{
    return {};
}

template<typename T>
using ExpectedTask = modern::result_task<T, std::error_code>;

template<typename T>
using ExpectedTaskT = ExpectedTask<T>;
} // namespace modern::io

export namespace modern_io
{
using modern::io::CurrentTraceContextAwaiter;
using modern::io::current_trace_context;
using modern::io::expected_task_memory_resource;
using modern::io::expected_task_memory_resource_policy;
using modern::io::expected_task_trace_context_policy;
using modern::io::ExpectedTask;
template<typename T>
using ExpectedTaskT = modern::io::ExpectedTaskT<T>;
using modern::io::ExpectedTaskMemoryResourcePolicy;
using modern::io::ExpectedTaskMemoryResourcePolicyScope;
using modern::io::ExpectedTaskMemoryResourceScope;
using modern::io::ExpectedTaskTraceContextPolicy;
using modern::io::ExpectedTaskTraceContextPolicyScope;
using modern::io::reset_expected_task_memory_resource;
using modern::io::set_expected_task_memory_resource;
using modern::io::set_expected_task_memory_resource_policy;
using modern::io::set_expected_task_trace_context_policy;
} // namespace modern_io
