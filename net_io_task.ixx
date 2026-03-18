module;

#include <coroutine>
#include <optional>
#include <exception>
#include <future>
#include <sstream>
#include <memory>

export module net_io.task;

import net_io.event_loop;

export namespace net_io {

template<typename T>
struct Task {
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr eptr;
        std::promise<void> done_promise_;
        std::future<void> done_future_;
        std::coroutine_handle<> continuation_{};
        bool started_ = false;
        int fd_ = -1;
        bool write_ = false;

        promise_type() : done_future_(done_promise_.get_future()) {}

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        struct init_suspend {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type>) noexcept {
                if (EventLoop::instance().debug_enabled()) {
                    EventLoop::debug_log("[promise_type] initial_suspend called (init phase)");
                }
            }
            void await_resume() noexcept {}
        };
        init_suspend initial_suspend() noexcept { return {}; }

        struct final_awaitable {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& p = h.promise();
                try { p.done_promise_.set_value(); } catch(...) {}
                if (p.continuation_) {
                    return p.continuation_;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        final_awaitable final_suspend() noexcept { return {}; }

        template<typename U>
        void return_value(U&& v) noexcept(std::is_nothrow_constructible_v<T,U&&>) {
            value = std::forward<U>(v);
        }
        void unhandled_exception() {
            eptr = std::current_exception();
        }

        void set_io_metadata(int fd, bool write) noexcept {
            fd_ = fd;
            write_ = write;
            std::ostringstream oss;
            oss << "[promise_type] set_io_metadata fd=" << fd << " write=" << write;
            EventLoop::debug_log(oss.str());
        }
    };

    using handle = std::coroutine_handle<promise_type>;
    explicit Task(handle h) : coro(h) {}
    Task(Task&& o) noexcept : coro(o.coro) { o.coro = {}; }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { if (coro) coro.destroy(); }

    bool await_ready() const noexcept { return !coro || coro.done(); }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        if (!coro) return std::noop_coroutine();
        auto& p = coro.promise();
        p.continuation_ = awaiting;
        p.started_ = true;
        return coro;
    }

    T await_resume() {
        if (coro.promise().eptr) std::rethrow_exception(coro.promise().eptr);
        return std::move(coro.promise().value.value());
    }

    [[nodiscard]] T get() {
        if (!coro) return await_resume();
        auto &p = coro.promise();
        if (!coro.done()) {
            if (!p.started_) {
                p.started_ = true;
            }
            coro.resume();
            if (!coro.done()) {
                p.done_future_.wait();
            }
        }
        return await_resume();
    }

    // Expose the underlying coroutine handle for advanced use.
    handle native_handle() const noexcept { return coro; }
    promise_type& promise() noexcept { return coro.promise(); }

    handle coro;
};

// void specialization
template<>
struct Task<void> {
    struct promise_type {
        std::exception_ptr eptr;
        std::promise<void> done_promise_;
        std::future<void> done_future_;
        std::coroutine_handle<> continuation_{};
        bool started_ = false;
        int fd_ = -1;
        bool write_ = false;

        promise_type() : done_future_(done_promise_.get_future()) {}

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        struct init_suspend {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type>) noexcept {
                if (EventLoop::instance().debug_enabled()) {
                    EventLoop::debug_log("[promise_type for void] initial_suspend called (init phase)");
                }
            }
            void await_resume() noexcept {}
        };
        init_suspend initial_suspend() noexcept { return {}; }
        struct final_awaitable {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& p = h.promise();
                try { p.done_promise_.set_value(); } catch(...) {}
                if (p.continuation_) {
                    return p.continuation_;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        final_awaitable final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {
            eptr = std::current_exception();
        }

        void return_void() noexcept {
        }
        void set_io_metadata(int fd, bool write) noexcept {
            fd_ = fd;
            write_ = write;
            std::ostringstream oss;
            oss << "[promise_type] set_io_metadata fd=" << fd << " write=" << write;
            EventLoop::debug_log(oss.str());
        }
    };

    using handle = std::coroutine_handle<promise_type>;
    explicit Task(handle h) : coro(h) {}
    Task(Task&& o) noexcept : coro(o.coro) { o.coro = {}; }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { if (coro) coro.destroy(); }

    bool await_ready() const noexcept { return !coro || coro.done(); }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        if (!coro) return std::noop_coroutine();
        auto& p = coro.promise();
        p.continuation_ = awaiting;
        p.started_ = true;
        return coro;
    }

    void await_resume() {
        if (coro.promise().eptr) std::rethrow_exception(coro.promise().eptr);
    }

    void get() {
        if (!coro) return await_resume();
        auto &p = coro.promise();
        if (!coro.done()) {
            if (!p.started_) {
                p.started_ = true;
            }
            coro.resume();
            if (!coro.done()) {
                p.done_future_.wait();
            }
        }
        await_resume();
    }

    // Expose the underlying coroutine handle for advanced use.
    handle native_handle() const noexcept { return coro; }
    promise_type& promise() noexcept { return coro.promise(); }

    handle coro;
};

template<typename T>
using IoTask = Task<T>;

} // namespace net_io
