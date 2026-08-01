module;

#include <coroutine>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <system_error>
#include <vector>
#include <deque>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <unordered_set>
#include <sstream>
#include <mutex>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <windows.h>
#else
    #include <sys/epoll.h>
    #include <unistd.h>
    #include <fcntl.h>
  #include <errno.h>
  #include <sys/eventfd.h>
#endif

export module net_io.event_loop;

export import net_io.async_concepts;

namespace modern::net {

struct FdWaiters {
    // Store coroutine handle + strong ownership token. Keeping a strong
    // reference ensures the awaiter object remains alive until we resume it,
    // which prevents races where handles point at destroyed coroutine frames.
    struct waiter {
        IoRegistrationToken token{};
        std::coroutine_handle<> handle{};
        std::shared_ptr<void> owner{};
        modern::scheduler completion_scheduler{};
        std::shared_ptr<IoOperationState> operation_state{};
        std::shared_ptr<void> cancellation_callback{};
    };
    std::deque<waiter> readers;
    std::deque<waiter> writers;
        uint32_t current_mask{0}; // epoll registration mask
};

export class EventLoop final : public EventReactor {
public:
    // Legacy convenience singleton for existing call sites.
    // New integrations should inject an EventReactor explicitly.
    static EventLoop& instance();

    // Start the event loop (idempotent)
    void start();

    // Stop the event loop and wake the poller
    void stop();

    // Manual wake to make newly registered FDs visible to the poller
    void wake();

    static bool debug_enabled() noexcept;

    static void debug_log(const std::string &s);

    // Register read interest for fd
    [[nodiscard]] IoRegistrationToken register_read(
        int fd, std::coroutine_handle<> h, std::shared_ptr<void> owner = {},
        modern::scheduler completion_scheduler = {}, IoRegistrationToken token = {},
        std::shared_ptr<IoOperationState> operation_state = {});

    // Register write interest for fd
    [[nodiscard]] IoRegistrationToken register_write(
        int fd, std::coroutine_handle<> h, std::shared_ptr<void> owner = {},
        modern::scheduler completion_scheduler = {}, IoRegistrationToken token = {},
        std::shared_ptr<IoOperationState> operation_state = {});

    [[nodiscard]] IoRegistrationToken register_io(const IoRegistration& registration) override;

    template<NativeIoHandle T>
    [[nodiscard]] IoRegistrationToken register_io(T& io_device, IOEvent events, std::coroutine_handle<> resume_handle, std::shared_ptr<void> owner = {}) {
        return register_io(make_io_registration(io_device, events, resume_handle, std::move(owner)));
    }

    void deregister(IoRegistrationToken token) override;

    // Remove all registrations for fd
    void deregister(int fd) override;

    [[nodiscard]] bool is_debug_enabled() const noexcept override;

    EventLoop();

    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

private:

    void run();

    // Hilfsfunktion zum Entleeren von wakeup_fd
    void drain_wake_fd();

    void dispatch_fd(int fd, uint32_t revents);

    void cancel_registration(IoRegistrationToken token);
    void enqueue_cancellation(IoRegistrationToken token);
    void process_cancellations();

    void finish_all();

    void update_epoll_locked(int fd, uint32_t add_flags, std::unique_lock<std::mutex>& lk);

private:
    std::mutex mtx_;
    std::unordered_map<int, FdWaiters> waiters_;
    std::atomic<bool> running_{false};
#ifndef _WIN32
    int epoll_fd_{-1};
    int wakeup_fd_{-1};
#else
    // Windows: HANDLE IOCP etc. (TODO)
#endif
    std::thread worker_;
    std::atomic<bool> wake_pending_{false};
    std::atomic<std::uint64_t> next_registration_{1};
    std::mutex cancellation_mtx_;
    std::vector<IoRegistrationToken> pending_cancellations_;
};

// Compatibility default reactor used by legacy convenience overloads.
// Runtime integrations should prefer explicit EventReactor injection.
export [[nodiscard]] EventReactor& default_event_reactor();

export [[nodiscard]] bool runtime_debug_enabled();

export void runtime_debug_log(const std::string& message);

} // namespace modern::net

export namespace net_io {

using modern::net::default_event_reactor;
using modern::net::EventLoop;
using modern::net::runtime_debug_enabled;
using modern::net::runtime_debug_log;

} // namespace net_io
