module;

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

module net_io.event_loop;

import net_io.async_concepts;

namespace modern::net {

EventLoop& EventLoop::instance() {
    static EventLoop loop;
    return loop;
}

void EventLoop::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread([this] { run(); });
}

void EventLoop::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }
#ifndef _WIN32
    if (wakeup_fd_ >= 0) {
        bool expected_wake = false;
        if (wake_pending_.compare_exchange_strong(expected_wake, true)) {
            uint64_t one = 1;
            (void)::write(wakeup_fd_, &one, sizeof(one));
        }
    }
#endif
    if (worker_.joinable()) {
        worker_.join();
    }
}

void EventLoop::wake() {
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

bool EventLoop::debug_enabled() noexcept {
    static bool value = (std::getenv("NET_IO_DEBUG") != nullptr);
    return value;
}

void EventLoop::debug_log(const std::string& message) {
    static std::mutex log_mtx;
    std::lock_guard<std::mutex> lock(log_mtx);
    std::cerr << message << std::endl;
    std::cerr.flush();
}

void EventLoop::register_read(int fd, std::coroutine_handle<> h, std::shared_ptr<void> owner) {
    if (fd < 0) {
        if (h && !h.done()) {
            h.resume();
        }
        return;
    }
    std::unique_lock<std::mutex> lock(mtx_);
    auto& waiters = waiters_[fd];

    if (h && !h.done()) {
        for (auto& reader : waiters.readers) {
            if (reader.first.address() == h.address()) {
                if (owner) {
                    reader.second = owner;
                }
                return;
            }
        }
        waiters.readers.emplace_back(h, owner);
    }

#ifndef _WIN32
    update_epoll_locked(fd, EPOLLIN, lock);
#endif
    wake();
}

void EventLoop::register_write(int fd, std::coroutine_handle<> h, std::shared_ptr<void> owner) {
    if (fd < 0) {
        if (h && !h.done()) {
            h.resume();
        }
        return;
    }
    std::unique_lock<std::mutex> lock(mtx_);
    auto& waiters = waiters_[fd];

    if (h && !h.done()) {
        for (auto& writer : waiters.writers) {
            if (writer.first.address() == h.address()) {
                if (owner) {
                    writer.second = owner;
                }
                return;
            }
        }
        waiters.writers.emplace_back(h, owner);
    }

#ifndef _WIN32
    update_epoll_locked(fd, EPOLLOUT, lock);
#endif
    wake();
}

void EventLoop::register_io(const IoRegistration& registration) {
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

void EventLoop::deregister(int fd) {
    if (fd < 0) {
        return;
    }
    std::scoped_lock lock(mtx_);
#ifndef _WIN32
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
    waiters_.erase(fd);
}

bool EventLoop::is_debug_enabled() const noexcept {
    return debug_enabled();
}

EventLoop::EventLoop() {
#ifndef _WIN32
    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) {
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

EventLoop::~EventLoop() {
    stop();
#ifndef _WIN32
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
#endif
}

void EventLoop::run() {
#ifndef _WIN32
    constexpr int max_events = 64;
    std::vector<epoll_event> events(max_events);

    while (running_) {
        if (debug_enabled()) {
            std::ostringstream oss;
            oss << "[EventLoop] epoll_wait entering...";
            debug_log(oss.str());
        }

        auto start = std::chrono::steady_clock::now();
        int n = ::epoll_wait(epoll_fd_, events.data(), max_events, 1000);
        auto end = std::chrono::steady_clock::now();

        if (debug_enabled()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::ostringstream oss;
            oss << "[EventLoop] epoll_wait returned=" << n << " elapsed_ms=" << ms << " errno=" << errno;
            debug_log(oss.str());
        }

        if (n < 0) {
            if (errno == EINTR) {
                drain_wake_fd();
                continue;
            }
            ::perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == wakeup_fd_) {
                drain_wake_fd();
                continue;
            }
            dispatch_fd(fd, events[i].events);
        }

        if (n == 0) {
            drain_wake_fd();
        }
    }

    finish_all();
#else
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    finish_all();
#endif
}

void EventLoop::drain_wake_fd() {
#ifndef _WIN32
    if (wakeup_fd_ < 0) {
        return;
    }
    uint64_t dummy;
    while (true) {
        ssize_t r = ::read(wakeup_fd_, &dummy, sizeof(dummy));
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
    }
    wake_pending_.store(false);
#endif
}

void EventLoop::dispatch_fd(int fd, uint32_t revents) {
    std::vector<std::coroutine_handle<>> to_resume;
    std::vector<std::shared_ptr<void>> owners_keepalive;
    {
        std::scoped_lock lock(mtx_);
        auto it = waiters_.find(fd);
        if (it == waiters_.end()) {
            return;
        }
        auto& waiters = it->second;
        if (debug_enabled()) {
            std::ostringstream oss;
            oss << "[EventLoop] dispatch_fd fd=" << fd << " events=" << revents << " readers=" << waiters.readers.size() << " writers=" << waiters.writers.size();
            oss << " reader_handles=[";
            for (auto& reader : waiters.readers) oss << reader.first.address() << ",";
            oss << "] writer_handles=[";
            for (auto& writer : waiters.writers) oss << writer.first.address() << ",";
            oss << "]";
            debug_log(oss.str());
        }
#ifndef _WIN32
        if (revents & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
            for (auto reader = waiters.readers.begin(); reader != waiters.readers.end(); ++reader) {
                auto& handle = reader->first;
                auto& owner = reader->second;
                if (debug_enabled()) {
                    std::ostringstream oss;
                    oss << "[EventLoop] dispatch_fd: reader owner_ptr=" << owner.get() << " handle=" << handle.address();
                    debug_log(oss.str());
                }
                if (owner) {
                    owners_keepalive.push_back(owner);
                }
                to_resume.push_back(handle);
                waiters.readers.erase(reader);
                break;
            }
        }
        if (revents & (EPOLLOUT | EPOLLERR)) {
            for (auto& writer : waiters.writers) {
                auto& handle = writer.first;
                auto& owner = writer.second;
                if (debug_enabled()) {
                    std::ostringstream oss;
                    oss << "[EventLoop] dispatch_fd: writer owner_ptr=" << owner.get() << " handle=" << handle.address();
                    debug_log(oss.str());
                }
                if (owner) {
                    owners_keepalive.push_back(owner);
                }
                to_resume.push_back(handle);
            }
            waiters.writers.clear();
        }

        uint32_t new_mask = 0;
        if (!waiters.readers.empty()) {
            new_mask |= EPOLLIN;
        }
        if (!waiters.writers.empty()) {
            new_mask |= EPOLLOUT;
        }

        if (new_mask == 0) {
            if (waiters.current_mask != 0) {
                ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            }
            waiters_.erase(it);
        } else if (new_mask != waiters.current_mask) {
            epoll_event ev{};
            ev.events = new_mask;
            ev.data.fd = fd;
            if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0) {
                waiters.current_mask = new_mask;
            } else {
                waiters_.erase(it);
            }
        }
#else
        for (auto& reader : waiters.readers) {
            if (reader.second) {
                owners_keepalive.push_back(reader.second);
            }
            to_resume.push_back(reader.first);
        }
        for (auto& writer : waiters.writers) {
            if (writer.second) {
                owners_keepalive.push_back(writer.second);
            }
            to_resume.push_back(writer.first);
        }
        waiters_.erase(it);
#endif
    }

    std::unordered_set<const void*> seen;
    std::vector<std::coroutine_handle<>> filtered;
    for (auto handle : to_resume) {
        const void* address = handle.address();
        if (seen.find(address) != seen.end()) {
            if (debug_enabled()) {
                std::ostringstream oss;
                oss << "[EventLoop] duplicate to_resume skipped handle=" << address;
                debug_log(oss.str());
            }
            continue;
        }
        seen.insert(address);
        filtered.push_back(handle);
    }
    to_resume.swap(filtered);

    if (debug_enabled()) {
        std::ostringstream oss;
        oss << "[EventLoop] about to resume " << to_resume.size() << " handles for fd=" << fd << " handles=[";
        for (auto handle : to_resume) oss << handle.address() << ",";
        oss << "]";
        debug_log(oss.str());
    }

    for (auto handle : to_resume) {
        const void* address = handle.address();
        bool done = false;
        try {
            done = handle.done();
        } catch (...) {
            done = false;
        }
        if (debug_enabled()) {
            std::ostringstream oss;
            oss << "[EventLoop] resume-candidate handle=" << address << " done=" << done;
            debug_log(oss.str());
        }
        if (handle && !done) {
            try {
                handle.resume();
            } catch (...) {
                if (debug_enabled()) {
                    std::ostringstream oss;
                    oss << "[EventLoop] exception while resuming handle=" << address;
                    debug_log(oss.str());
                }
            }
        }
    }
}

void EventLoop::finish_all() {
    std::vector<std::coroutine_handle<>> all;
    {
        std::scoped_lock lock(mtx_);
        for (auto& [fd, waiters] : waiters_) {
            if (debug_enabled()) {
                std::ostringstream oss;
                oss << "[EventLoop] finish_all fd=" << fd << " readers=" << waiters.readers.size() << " writers=" << waiters.writers.size();
                oss << " reader_handles=[";
                for (auto& reader : waiters.readers) oss << reader.first.address() << ",";
                oss << "] writer_handles=[";
                for (auto& writer : waiters.writers) oss << writer.first.address() << ",";
                oss << "]";
                debug_log(oss.str());
            }
            for (auto& reader : waiters.readers) {
                auto& handle = reader.first;
                auto& owner = reader.second;
                if (debug_enabled()) {
                    std::ostringstream oss;
                    oss << "[EventLoop] finish_all reader handle=" << handle.address() << " owner_ptr=" << owner.get();
                    debug_log(oss.str());
                }
                if (handle && !handle.done()) {
                    all.push_back(handle);
                }
            }
            for (auto& writer : waiters.writers) {
                auto& handle = writer.first;
                auto& owner = writer.second;
                if (debug_enabled()) {
                    std::ostringstream oss;
                    oss << "[EventLoop] finish_all writer handle=" << handle.address() << " owner_ptr=" << owner.get();
                    debug_log(oss.str());
                }
                if (handle && !handle.done()) {
                    all.push_back(handle);
                }
            }
        }
        waiters_.clear();
    }
    for (auto handle : all) {
        if (handle && !handle.done()) {
            handle.resume();
        }
    }
}

void EventLoop::update_epoll_locked(int fd, uint32_t add_flags, std::unique_lock<std::mutex>& lock) {
#ifndef _WIN32
    auto& waiters = waiters_[fd];
    uint32_t new_mask = waiters.current_mask | add_flags;

    epoll_event ev{};
    ev.events = new_mask;
    ev.data.fd = fd;

    int op = (waiters.current_mask == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    if (::epoll_ctl(epoll_fd_, op, fd, &ev) == 0) {
        waiters.current_mask = new_mask;

        if (wakeup_fd_ >= 0) {
            bool expected = false;
            if (wake_pending_.compare_exchange_strong(expected, true)) {
                uint64_t one = 1;
                (void)::write(wakeup_fd_, &one, sizeof(one));
            }
        }
    } else {
        std::vector<std::coroutine_handle<>> to_resume;
        std::vector<std::shared_ptr<void>> owners_keepalive;
        for (auto& reader : waiters.readers) {
            if (reader.second) {
                owners_keepalive.push_back(reader.second);
            }
            to_resume.push_back(reader.first);
        }
        for (auto& writer : waiters.writers) {
            if (writer.second) {
                owners_keepalive.push_back(writer.second);
            }
            to_resume.push_back(writer.first);
        }
        waiters_.erase(fd);
        lock.unlock();
        for (auto handle : to_resume) {
            if (handle && !handle.done()) {
                handle.resume();
            }
        }
        lock.lock();
    }
#else
    (void)fd;
    (void)add_flags;
    (void)lock;
#endif
}

EventReactor& default_event_reactor() {
    return EventLoop::instance();
}

bool runtime_debug_enabled() {
    return default_event_reactor().is_debug_enabled();
}

void runtime_debug_log(const std::string& message) {
    EventLoop::debug_log(message);
}

} // namespace modern::net