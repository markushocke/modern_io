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

namespace net_io {

struct FdWaiters {
    // Store coroutine handle + strong ownership token. Keeping a strong
    // reference ensures the awaiter object remains alive until we resume it,
    // which prevents races where handles point at destroyed coroutine frames.
    std::deque<std::pair<std::coroutine_handle<>, std::shared_ptr<void>>> readers;
    std::deque<std::pair<std::coroutine_handle<>, std::shared_ptr<void>>> writers;
        uint32_t current_mask{0}; // epoll registration mask
};

export class EventLoop final : public EventReactor {
public:
    static EventLoop& instance() {
        static EventLoop loop;
        return loop;
    }

    // Start the event loop (idempotent)
    void start() {
        bool expected = false;
            if (!running_.compare_exchange_strong(expected, true)) 
            return;
        worker_ = std::thread([this]{ run(); });
    }

    // Stop the event loop and wake the poller
    void stop() {
        bool expected = true;
            if (!running_.compare_exchange_strong(expected, false)) 
            return;
#ifndef _WIN32
        if (wakeup_fd_ >= 0) {
            // attempt to set wake_pending_ and write only if not already set
            bool expected = false;
            if (wake_pending_.compare_exchange_strong(expected, true)) {
                uint64_t one = 1;
                (void)::write(wakeup_fd_, &one, sizeof(one));
            }
        }
#endif
        if (worker_.joinable()) worker_.join();
    // After stop: possibly annotate pending coroutines with an error (here: simply ignore)
    }

    // Manual wake to make newly registered FDs visible to the poller
    void wake() {
#ifndef _WIN32
        if (wakeup_fd_ >= 0) {
            bool expected = false;
            if (wake_pending_.compare_exchange_strong(expected, true)) {
                uint64_t one = 1;
                (void)::write(wakeup_fd_, &one, sizeof(one));
            }
        }
#endif
    }

    static inline bool debug_enabled() noexcept {
        static bool v = (std::getenv("NET_IO_DEBUG") != nullptr);
        return v;
    }

    static inline void debug_log(const std::string &s) {
        static std::mutex log_mtx;
        std::lock_guard<std::mutex> lk(log_mtx);
        std::cerr << s << std::endl;
        std::cerr.flush();
    }

    // Register read interest for fd
    void register_read(int fd, std::coroutine_handle<> h, std::shared_ptr<void> owner = {}) {
        if (fd < 0) { if (h && !h.done()) h.resume(); return; }
        std::unique_lock<std::mutex> lk(mtx_);
        auto& w = waiters_[fd];

        if (h && !h.done()) {
            // sanity: avoid duplicate handles
            for (auto &rh : w.readers) {
                if (rh.first.address() == h.address()) {
                    if (owner) rh.second = owner; // update owner
                    return;
                }
            }
            w.readers.emplace_back(h, owner);
        }

        // Always update epoll mask when a new reader arrives
        update_epoll_locked(fd, EPOLLIN, lk);

        // Wake the poller thread so it sees new registration
        wake();
    }

    // Register write interest for fd
    void register_write(int fd, std::coroutine_handle<> h, std::shared_ptr<void> owner = {}) {
        if (fd < 0) { if (h && !h.done()) h.resume(); return; }
        std::unique_lock<std::mutex> lk(mtx_);
        auto& w = waiters_[fd];

        if (h && !h.done()) {
            for (auto &wh : w.writers) {
                if (wh.first.address() == h.address()) {
                    if (owner) wh.second = owner;
                    return;
                }
            }
            w.writers.emplace_back(h, owner);
        }

        // Always update epoll mask when a new writer arrives
        update_epoll_locked(fd, EPOLLOUT, lk);

        // Wake the poller thread so it sees new registration
        wake();
    }

    void register_io(const IoRegistration& registration) override {
        if (has_event(registration.events, IOEvent::Read)
            || has_event(registration.events, IOEvent::Error)
            || has_event(registration.events, IOEvent::Hangup)) {
#ifdef _WIN32
            register_read(static_cast<int>(reinterpret_cast<std::uintptr_t>(registration.handle)), registration.resume_handle, registration.owner);
#else
            register_read(static_cast<int>(registration.handle), registration.resume_handle, registration.owner);
#endif
        }

        if (has_event(registration.events, IOEvent::Write)) {
#ifdef _WIN32
            register_write(static_cast<int>(reinterpret_cast<std::uintptr_t>(registration.handle)), registration.resume_handle, registration.owner);
#else
            register_write(static_cast<int>(registration.handle), registration.resume_handle, registration.owner);
#endif
        }
    }

    template<NativeIoHandle T>
    void register_io(T& io_device, IOEvent events, std::coroutine_handle<> resume_handle, std::shared_ptr<void> owner = {}) {
        register_io(make_io_registration(io_device, events, resume_handle, std::move(owner)));
    }

    // Remove all registrations for fd
    void deregister(int fd) override {
        if (fd < 0) return;
        std::scoped_lock lk(mtx_);
#ifndef _WIN32
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
        waiters_.erase(fd);
    }

    [[nodiscard]] bool is_debug_enabled() const noexcept override {
        return debug_enabled();
    }

    EventLoop() {
#ifndef _WIN32
        epoll_fd_ = ::epoll_create1(0);
        if (epoll_fd_ < 0) {
            // Fatal error creating epoll instance
            std::abort();
        }
        wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeup_fd_ >= 0) {
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = wakeup_fd_;
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev);
        }
#endif
    }

    ~EventLoop() {
        stop();
#ifndef _WIN32
        if (wakeup_fd_ >= 0) ::close(wakeup_fd_);
        if (epoll_fd_ >= 0) ::close(epoll_fd_);
#endif
    }

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

private:

    void run() {
    #ifndef _WIN32
        constexpr int MAX_EVENTS = 64;
        std::vector<epoll_event> events(MAX_EVENTS);

        while (running_) {
            if (debug_enabled()) {
                std::ostringstream oss;
                oss << "[EventLoop] epoll_wait entering...";
                debug_log(oss.str());
            }

            auto start = std::chrono::steady_clock::now();
            int n = ::epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, 1000);
            auto end = std::chrono::steady_clock::now();

            if (debug_enabled()) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                std::ostringstream oss;
                oss << "[EventLoop] epoll_wait returned=" << n << " elapsed_ms=" << ms << " errno=" << errno;
                debug_log(oss.str());
            }

            // EINTR: einfach wiederholen, aber wake_fd prüfen
            if (n < 0) {
                if (errno == EINTR) {
                    drain_wake_fd();
                    continue;
                }
                perror("epoll_wait");
                break;
            }

            // Verarbeite alle Events
            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                if (fd == wakeup_fd_) {
                    drain_wake_fd();
                    continue;
                }
                dispatch_fd(fd, events[i].events);
            }

            // epoll_wait kann 0 zurückgeben (Timeout): wake_fd trotzdem prüfen
            if (n == 0) {
                drain_wake_fd();
            }
        }

        // Nach dem Stop alle noch wartenden Coroutinen aufwecken
        finish_all();

    #else
        // TODO: Windows IOCP
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        finish_all();
    #endif
    }

    // Hilfsfunktion zum Entleeren von wakeup_fd
    void drain_wake_fd() {
    #ifndef _WIN32
        if (wakeup_fd_ < 0) return;
        uint64_t dummy;
        while (true) {
            ssize_t r = ::read(wakeup_fd_, &dummy, sizeof(dummy));
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }
        }
        wake_pending_.store(false);
    #endif
    }

    void dispatch_fd(int fd, uint32_t revents) {
        std::vector<std::coroutine_handle<>> to_resume;
        std::vector<std::shared_ptr<void>> owners_keepalive;
        {
            std::scoped_lock lk(mtx_);
            auto it = waiters_.find(fd);
            if (it == waiters_.end()) return;
            auto& w = it->second;
            if (debug_enabled()) {
                std::ostringstream oss;
                oss << "[EventLoop] dispatch_fd fd=" << fd << " events=" << revents << " readers=" << w.readers.size() << " writers=" << w.writers.size();
                oss << " reader_handles=[";
                for (auto &rh : w.readers) oss << rh.first.address() << ",";
                oss << "] writer_handles=[";
                for (auto &wh : w.writers) oss << wh.first.address() << ",";
                oss << "]";
                debug_log(oss.str());
            }
            if (revents & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                // Resume at most one reader for this fd to avoid multiple
                // awaiters racing to accept the same incoming connection.
                for (auto it = w.readers.begin(); it != w.readers.end(); ++it) {
                    auto &h = it->first;
                    auto &sp = it->second;
                    if (debug_enabled()) {
                        std::ostringstream oss;
                        oss << "[EventLoop] dispatch_fd: reader owner_ptr=" << sp.get() << " handle=" << h.address();
                        debug_log(oss.str());
                    }
                    if (sp) {
                        owners_keepalive.push_back(sp);
                    }
                    to_resume.push_back(h);
                    // remove this reader from the queue
                    w.readers.erase(it);
                    break;
                }
            }
            if (revents & (EPOLLOUT | EPOLLERR)) {
                for (auto &p : w.writers) {
                    auto &h = p.first;
                    auto &sp = p.second;
                    if (debug_enabled()) {
                        std::ostringstream oss;
                        oss << "[EventLoop] dispatch_fd: writer owner_ptr=" << sp.get() << " handle=" << h.address();
                        debug_log(oss.str());
                    }
                    if (sp) {
                        owners_keepalive.push_back(sp);
                    }
                    to_resume.push_back(h);
                }
                w.writers.clear();
            }
 #ifndef _WIN32
            uint32_t new_mask = 0;
            if (!w.readers.empty()) {
                new_mask |= EPOLLIN;
            }
            if (!w.writers.empty()) {
                new_mask |= EPOLLOUT;
            }

            if (new_mask == 0) {
                if (w.current_mask != 0) {
                    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                }
                waiters_.erase(it);
            } else {
                if (new_mask != w.current_mask) {
                    epoll_event ev{};
                    ev.events = new_mask;
                    ev.data.fd = fd;
                    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0) {
                        w.current_mask = new_mask;
                    } else {
                        waiters_.erase(it);
                    }
                }
            }
#else
            if (w.readers.empty() && w.writers.empty()) {
                waiters_.erase(it);
            }
#endif
        }
        // Resume collected handles outside of the lock
        std::unordered_set<const void*> seen;
        std::vector<std::coroutine_handle<>> filtered;
        for (auto h : to_resume) {
            const void* addr = h.address();
            if (seen.find(addr) != seen.end()) {
                if (debug_enabled()) { std::ostringstream oss; oss << "[EventLoop] duplicate to_resume skipped handle=" << addr; debug_log(oss.str()); }
                continue;
            }
            seen.insert(addr);
            filtered.push_back(h);
        }
        to_resume.swap(filtered);
        // Before resuming, log owner weak_ptr status where available
        if (debug_enabled()) {
            std::ostringstream oss;
            oss << "[EventLoop] about to resume " << to_resume.size() << " handles for fd=" << fd << " handles=[";
            for (auto h : to_resume) oss << h.address() << ",";
            oss << "]";
            debug_log(oss.str());
        }
        for (auto h : to_resume) {
            const void* addr = h.address();
            bool done = false;
            try { done = h.done(); } catch(...) { done = false; }
            if (debug_enabled()) {
                std::ostringstream oss; oss << "[EventLoop] resume-candidate handle=" << addr << " done=" << done; debug_log(oss.str()); }
            if (h && !done) {
                try {
                    h.resume();
                } catch(...) {
                    if (debug_enabled()) { std::ostringstream oss; oss << "[EventLoop] exception while resuming handle=" << addr; debug_log(oss.str()); }
                }
            }
        }
    }

    void finish_all() {
        std::vector<std::coroutine_handle<>> all;
        {
            std::scoped_lock lk(mtx_);
            for (auto& [fd, w] : waiters_) {
                if (debug_enabled()) {
                    std::ostringstream oss;
                    oss << "[EventLoop] finish_all fd=" << fd << " readers=" << w.readers.size() << " writers=" << w.writers.size();
                    oss << " reader_handles=[";
                    for (auto &p : w.readers) oss << p.first.address() << ",";
                    oss << "] writer_handles=[";
                    for (auto &p : w.writers) oss << p.first.address() << ",";
                    oss << "]";
                    debug_log(oss.str());
                }
                for (auto &p : w.readers) {
                    auto &h = p.first; auto &sp = p.second;
                    if (debug_enabled()) {
                        std::ostringstream oss;
                        oss << "[EventLoop] finish_all reader handle=" << h.address() << " owner_ptr=" << sp.get();
                        debug_log(oss.str());
                    }
                    if (h && !h.done()) all.push_back(h);
                }
                for (auto &p : w.writers) {
                    auto &h = p.first; auto &sp = p.second;
                    if (debug_enabled()) {
                        std::ostringstream oss;
                        oss << "[EventLoop] finish_all writer handle=" << h.address() << " owner_ptr=" << sp.get();
                        debug_log(oss.str());
                    }
                    if (h && !h.done()) all.push_back(h);
                }
            }
            waiters_.clear();
        }
        for (auto h : all) {
            if (h && !h.done()) h.resume();
        }
    }

#if !defined(_WIN32)
void update_epoll_locked(int fd, uint32_t add_flags, std::unique_lock<std::mutex>& lk) {
    auto& w = waiters_[fd];
    uint32_t new_mask = w.current_mask | add_flags;

    epoll_event ev{};
    ev.events = new_mask; // level-triggered
    ev.data.fd = fd;

    int op = (w.current_mask == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    if (::epoll_ctl(epoll_fd_, op, fd, &ev) == 0) {
        w.current_mask = new_mask;

        // Wake the poller to notice new registration immediately
        if (wakeup_fd_ >= 0) {
            bool expected = false;
            if (wake_pending_.compare_exchange_strong(expected, true)) {
                uint64_t one = 1;
                (void)::write(wakeup_fd_, &one, sizeof(one));
            }
        }
    } else {
        // epoll_ctl failed: remove waiters to avoid dangling coroutines
        std::vector<std::coroutine_handle<>> to_resume;
        std::vector<std::shared_ptr<void>> owners_keepalive;
        for (auto &p : w.readers) {
            if (p.second) {
                owners_keepalive.push_back(p.second);
            }
            to_resume.push_back(p.first);
        }
        for (auto &p : w.writers) {
            if (p.second) {
                owners_keepalive.push_back(p.second);
            }
            to_resume.push_back(p.first);
        }
        waiters_.erase(fd);
        lk.unlock();
        for (auto h : to_resume) if (h && !h.done()) h.resume();
        lk.lock();
    }
}
#else
    void update_epoll_locked(int, uint32_t, std::unique_lock<std::mutex>&) {}
#endif

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
};

export [[nodiscard]] inline EventReactor& default_event_reactor() {
    return EventLoop::instance();
}

export [[nodiscard]] inline bool runtime_debug_enabled() {
    return default_event_reactor().is_debug_enabled();
}

export inline void runtime_debug_log(const std::string& message) {
    EventLoop::debug_log(message);
}

} // namespace net_io
