module;

#ifndef _MSC_VER
#include <coroutine>
#include <expected>
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
import modern_io.trace;

export namespace modern::io
{
namespace detail
{
[[nodiscard]] inline modern::runtime::TraceContext to_runtime_trace_context(const TraceContext& trace) noexcept
{
    return trace;
}

[[nodiscard]] inline TraceContext from_runtime_trace_context(const modern::runtime::TraceContext& trace) noexcept
{
    return trace;
}
} // namespace detail

class ExpectedTaskMemoryResourcePolicy : public modern::runtime::TaskEnvironmentPolicy
{
public:
    virtual ~ExpectedTaskMemoryResourcePolicy() = default;
    [[nodiscard]] virtual std::pmr::memory_resource* get_memory_resource() noexcept = 0;

    modern::runtime::TaskEnvironment current_environment() noexcept override
    {
        modern::runtime::TaskEnvironment environment;
        environment.frame_resource = get_memory_resource();
        return environment;
    }
};

class ExpectedTaskTraceContextPolicy : public modern::runtime::TaskEnvironmentPolicy
{
public:
    virtual ~ExpectedTaskTraceContextPolicy() = default;
    [[nodiscard]] virtual std::optional<TraceContext> get_trace_context() noexcept = 0;

    modern::runtime::TaskEnvironment current_environment() noexcept override
    {
        modern::runtime::TaskEnvironment environment;

        if (auto trace = get_trace_context())
            environment.trace_context = detail::to_runtime_trace_context(*trace);

        return environment;
    }
};

using ExpectedTaskMemoryResourcePolicyScope = modern::runtime::TaskEnvironmentScope;
using ExpectedTaskMemoryResourceScope = modern::runtime::FrameMemoryResourceScope;
using ExpectedTaskTraceContextPolicyScope = modern::runtime::TaskEnvironmentScope;

[[nodiscard]] inline ExpectedTaskMemoryResourcePolicy* expected_task_memory_resource_policy() noexcept
{
    return dynamic_cast<ExpectedTaskMemoryResourcePolicy*>(modern::runtime::task_environment_policy());
}

[[nodiscard]] inline ExpectedTaskTraceContextPolicy* expected_task_trace_context_policy() noexcept
{
    return dynamic_cast<ExpectedTaskTraceContextPolicy*>(modern::runtime::task_environment_policy());
}

inline ExpectedTaskMemoryResourcePolicy*
set_expected_task_memory_resource_policy(ExpectedTaskMemoryResourcePolicy* policy) noexcept
{
    auto* previous = expected_task_memory_resource_policy();
    modern::runtime::set_task_environment_policy(policy);
    return previous;
}

inline ExpectedTaskTraceContextPolicy*
set_expected_task_trace_context_policy(ExpectedTaskTraceContextPolicy* policy) noexcept
{
    auto* previous = expected_task_trace_context_policy();
    modern::runtime::set_task_environment_policy(policy);
    return previous;
}

[[nodiscard]] inline std::pmr::memory_resource* expected_task_memory_resource() noexcept
{
    return modern::runtime::frame_memory_resource();
}

inline std::pmr::memory_resource*
set_expected_task_memory_resource(std::pmr::memory_resource* resource) noexcept
{
    return modern::runtime::set_frame_memory_resource(resource);
}

inline void reset_expected_task_memory_resource() noexcept
{
    modern::runtime::set_frame_memory_resource(nullptr);
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
        auto runtime_awaiter = modern::runtime::current_trace_context();
        runtime_awaiter.await_suspend(current);
        auto runtime_trace = runtime_awaiter.await_resume();

        if (runtime_trace)
            trace_context_ = detail::from_runtime_trace_context(*runtime_trace);
        else
            trace_context_.reset();

        return false;
    }

    [[nodiscard]] std::optional<TraceContext> await_resume() const noexcept
    {
        return trace_context_;
    }

private:
    std::optional<TraceContext> trace_context_;
};

[[nodiscard]] inline CurrentTraceContextAwaiter current_trace_context() noexcept
{
    return {};
}

template<typename T>
using ExpectedTask = modern::runtime::ResultTask<T, std::error_code>;

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