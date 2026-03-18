module;

#include <coroutine>
#include <chrono>

export module net_io.scheduler;

export namespace net_io {

struct IScheduler {
    virtual ~IScheduler() = default;
    virtual void schedule(std::coroutine_handle<> h) = 0;
    virtual void schedule_at(std::chrono::steady_clock::time_point t, std::coroutine_handle<> h) = 0;
    virtual void register_read(int fd, void* context) = 0;
    virtual void register_write(int fd, void* context) = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
};

} // namespace net_io
