module;

#include <coroutine>
#include <expected>
#include <system_error>
#include <utility>
#include <type_traits>
#include <memory>
#include <functional>

export module net_io.generic_awaiter;

import net_io.event_loop;
import net_io_base;

export namespace modern::net {

inline bool is_would_block(const std::error_code& ec) {
    using std::errc;
    return ec == errc::operation_would_block
        || ec == errc::resource_unavailable_try_again;
}

// Heap-allocated awaiter implementation
template<typename Ret, typename Op, typename Registrar>
class GenericAwaiterImpl {
    int fd_;
    Op op_;
    Registrar reg_;
    std::expected<Ret,std::error_code> result_;
    bool ready_{false};
    std::weak_ptr<void> self_;
    bool is_write_{false};
public:
    GenericAwaiterImpl(int fd, Op op, Registrar reg, bool is_write)
        : fd_(fd), op_(std::move(op)), reg_(std::move(reg)), is_write_(is_write) {}

    ~GenericAwaiterImpl() {
        if (runtime_debug_enabled()) {
            std::ostringstream oss; oss << "[GenericAwaiterImpl] dtor this=" << this << " fd=" << fd_; runtime_debug_log(oss.str());
        }
    }

    GenericAwaiterImpl(const GenericAwaiterImpl&) = delete;
    GenericAwaiterImpl& operator=(const GenericAwaiterImpl&) = delete;

    bool await_ready() noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> h) {
        auto owner = self_.lock();
        if (runtime_debug_enabled()) {
            fprintf(stderr, "[GenericAwaiterImpl] await_suspend entry fd=%d this=%p self_ptr=%p\n",
                    fd_, (void*)this, owner.get());
        }

        if (fd_ < 0) {
            result_ = std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
            ready_ = true;
            return false;
        }

        // First attempt
        auto r = op_(fd_);
        if (r) {
            result_ = *r;
            ready_ = true;
            return false; // no suspend, immediate result
        }

        auto ec = r.error();
        if (is_would_block(ec)) {
            // Register for readiness
            if (runtime_debug_enabled()) {
                std::ostringstream oss;
                oss << "[GenericAwaiterImpl] await_suspend registering fd=" << fd_
                    << " handle=" << h.address()
                    << " self_ptr=" << owner.get();
                runtime_debug_log(oss.str());
            }
            reg_(fd_, h, owner);

            // **Immediately re-check** in case data arrived before epoll_ctl took effect.
            // If op_ now returns a value, consume it and do NOT suspend.
            auto r2 = op_(fd_);
            if (r2) {
                result_ = *r2;
                ready_ = true;
                return false; // do not suspend — we have the data already
            }
            auto ec2 = r2.error();
            if (!is_would_block(ec2)) {
                // terminal error on re-check
                result_ = std::unexpected(ec2);
                ready_ = true;
                return false;
            }

            // still would_block -> actually suspend and wait for event
            return true;
        }

        // terminal error on first attempt
        result_ = std::unexpected(ec);
        ready_ = true;
        return false;
    }

    std::expected<Ret,std::error_code> await_resume() noexcept {
        auto owner = self_.lock();
        if (runtime_debug_enabled()) {
            std::ostringstream oss; oss << "[GenericAwaiterImpl] await_resume fd=" << fd_ << " this=" << this << " self_ptr=" << owner.get(); runtime_debug_log(oss.str());
        }
        if (!ready_) {
            if (runtime_debug_enabled()) {
                std::ostringstream oss; oss << "[GenericAwaiterImpl] await_resume retry fd=" << fd_ << " self_ptr=" << owner.get(); runtime_debug_log(oss.str());
            }
            auto r = op_(fd_);
            if (r) {
                result_ = *r;
            } else {
                result_ = std::unexpected(r.error());
            }
            ready_ = true;
        }
        return std::move(result_);
    }

    void retry(std::coroutine_handle<> h) {
        auto owner = self_.lock();
        if (runtime_debug_enabled()) {
            std::ostringstream oss;
            oss << "[GenericAwaiterImpl] retry fd=" << fd_
                << " this=" << this
                << " self_ptr=" << owner.get();
            runtime_debug_log(oss.str());
        }

        if (ready_) return;

        auto r = op_(fd_);
        if (r) {
            result_ = *r;
            ready_ = true;
            h.resume();
            return;
        }

        auto ec = r.error();
    if (is_would_block(ec)) {
            if (runtime_debug_enabled()) {
                std::ostringstream oss;
                oss << "[GenericAwaiterImpl] retry register fd=" << fd_
                    << " handle=" << h.address()
                    << " self_ptr=" << owner.get();
                runtime_debug_log(oss.str());
            }
            reg_(fd_, h, owner);
            return;
        }

        result_ = std::unexpected(ec);
        ready_ = true;
        h.resume();
    }

    void set_self(const std::shared_ptr<void>& self) { self_ = self; }
};

// Proxy awaiter type
template<typename Ret, typename Op, typename Registrar>
class GenericAwaiter {
    std::shared_ptr<GenericAwaiterImpl<Ret, Op, Registrar>> impl_;
public:
    GenericAwaiter(int fd, Op op, Registrar reg, bool is_write = false)
        : impl_(std::make_shared<GenericAwaiterImpl<Ret, Op, Registrar>>(fd, std::move(op), std::move(reg), is_write))
    {
        impl_->set_self(impl_);
    }
    bool await_ready() noexcept { return impl_->await_ready(); }
    bool await_suspend(std::coroutine_handle<> h) {
        if (runtime_debug_enabled()) {
            std::ostringstream oss; oss << "[GenericAwaiter] await_suspend proxy impl_=" << impl_.get();
            try { if (impl_) oss << " impl_use_count=" << impl_.use_count(); } catch(...) {}
            runtime_debug_log(oss.str());
        }
        return impl_->await_suspend(h);
    }
    std::expected<Ret,std::error_code> await_resume() noexcept { return impl_->await_resume(); }
    void retry(std::coroutine_handle<> h) { impl_->retry(h); }
};

// Hilfs-Trait zur Ableitung des value_type aus std::expected<..>
template<typename T>
struct expected_value_type;
template<typename V, typename E>
struct expected_value_type<std::expected<V,E>> { using type = V; };

// Revised factory: deduces Ret from the return type of Op::operator()
template<typename FD, typename Op, typename Reg>
auto make_awaiter(FD fd, Op op, Reg reg, bool is_write = false) {
    using ExpectedT = std::invoke_result_t<Op&, FD&>;
    using Ret = typename expected_value_type<ExpectedT>::type;
    return GenericAwaiter<Ret, Op, Reg>(fd, std::move(op), std::move(reg), is_write);
}

} // namespace modern::net

// --- New lightweight awaiter with start/finish hooks (for migration) ---
export namespace modern::net {

template<typename ResultT>
struct GenericAwaiterHooks {
    std::function<void(std::coroutine_handle<>)> start_hook; // register with scheduler
    std::function<ResultT()> finish_hook; // extract result after readiness

    GenericAwaiterHooks(std::function<void(std::coroutine_handle<>)> s,
                       std::function<ResultT()> f)
        : start_hook(std::move(s)), finish_hook(std::move(f)) {}

    bool await_ready() const noexcept {
        // We do not know if result is ready; let user-provided finish_hook decide
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        if (start_hook) start_hook(h);
    }

    ResultT await_resume() {
        if (finish_hook) return finish_hook();
        return ResultT{};
    }
};

template<typename ResultT>
auto make_awaitable_with_hooks(std::function<void(std::coroutine_handle<>)> start,
                              std::function<ResultT()> finish) {
    return GenericAwaiterHooks<ResultT>{std::move(start), std::move(finish)};
}

} // namespace modern::net

export namespace net_io {

using modern::net::GenericAwaiterHooks;
using modern::net::make_awaiter;
using modern::net::make_awaitable_with_hooks;

} // namespace net_io