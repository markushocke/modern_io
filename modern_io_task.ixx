module;

#ifndef _MSC_VER
#include <expected>
#include <coroutine>
#include <system_error>
#include <latch>
#include <optional>
#else
export import <expected>;
export import <coroutine>;
export import <system_error>;
export import <latch>;
export import <optional>;
#endif

export module modern_io.task;

export namespace modern_io {

template<typename T>
class ExpectedTask {
public:
    struct promise_type {
        std::coroutine_handle<> continuation_{};
        std::expected<T,std::error_code> result_{ std::unexpected(std::make_error_code(std::errc::operation_not_permitted)) };
        std::latch* wait_latch_{nullptr};
    bool started_{false}; // NEW: start status

        ExpectedTask get_return_object() noexcept {
            return ExpectedTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }
    // Keep suspend_always initially: explicit start is possible,
    // but we now auto-start in await_suspend.
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct final_awaitable {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& p = h.promise();
                if (p.wait_latch_) {
                    p.wait_latch_->count_down();
                }
                if (p.continuation_) {
                    p.continuation_.resume();
                }
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
    bool await_suspend(std::coroutine_handle<> cont) noexcept {
        auto& p = h_.promise();
        p.continuation_ = cont;
        if (!p.started_) {
            p.started_ = true;
            h_.resume();                // läuft bis erstes suspend / Ende
        }
        // If already finished (completed synchronously), do not suspend
        return !h_.done();
    }

    std::expected<T,std::error_code> await_resume() noexcept {
        return std::move(h_.promise().result_);
    }

    void start() noexcept {
        if (h_ && !h_.done() && !h_.promise().started_) {
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