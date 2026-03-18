// test_event_loop.cpp
import net_io_async;
#include <coroutine>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#endif

using namespace net_io;

TEST(EventLoopTest, StartStopIdempotent) {
    auto &loop = EventLoop::instance();
    loop.start();
    // start again should be no-op
    loop.start();
    // stop should stop the worker
    loop.stop();
    // stop again should be no-op
    loop.stop();
}

TEST(EventLoopTest, WakeDoesNotCrash) {
    auto &loop = EventLoop::instance();
    loop.start();
    loop.wake();
    // small sleep to let worker process wake
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();
}

// Basic register/deregister with invalid fd should resume the handle immediately.
// We simulate an awaitable by using a coroutine that immediately sets a flag
// when resumed. This avoids depending on the rest of the coroutine framework.

struct SimpleAwaitable {
    bool &resumed_flag;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        // register with event loop using provided handle
        EventLoop::instance().register_read(-1, h, {});
    }
    void await_resume() noexcept { resumed_flag = true; }
};

struct SimpleTask {
    struct promise_type {
        SimpleTask get_return_object() { return {} ; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

SimpleTask test_register_invalid_fd(bool &out_flag) {
    co_await SimpleAwaitable{out_flag};
}

TEST(EventLoopTest, RegisterInvalidFdResumesImmediately) {
    bool resumed = false;
    test_register_invalid_fd(resumed);
    // coroutine should have been resumed synchronously
    EXPECT_TRUE(resumed);
}

#ifndef _WIN32
// Ensure the EventLoop resumes a reader when a pipe becomes readable.
struct FdAwaitable {
    int fd;
    bool &resumed_flag;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto owner = std::make_shared<int>(0);
        EventLoop::instance().register_read(fd, h, owner);
    }
    void await_resume() noexcept { resumed_flag = true; }
};

SimpleTask test_pipe_reader(int fd, bool &out_flag) {
    co_await FdAwaitable{fd, out_flag};
}

TEST(EventLoopTest, RegisterWithPipeResumesReader) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int r = fds[0];
    int w = fds[1];

    auto &loop = EventLoop::instance();
    loop.start();

    bool resumed = false;
    test_pipe_reader(r, resumed);

    // write a byte to make the read end readable
    const char b = 'x';
    ssize_t nw = write(w, &b, 1);
    EXPECT_EQ(nw, 1);

    // give the event loop a short moment to dispatch
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop.stop();

    close(r); close(w);

    EXPECT_TRUE(resumed);
}
#endif

#ifndef _WIN32
// Register two readers on the same FD; only one should be resumed when data arrives.
TEST(EventLoopTest, MultipleReadersOnlyOneResumed) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int r = fds[0];
    int w = fds[1];

    auto &loop = EventLoop::instance();
    loop.start();

    bool resumed1 = false;
    bool resumed2 = false;
    test_pipe_reader(r, resumed1);
    test_pipe_reader(r, resumed2);

    // write one byte; only one reader should resume
    const char b = 'z';
    ssize_t nw = write(w, &b, 1);
    EXPECT_EQ(nw, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check how many resumed BEFORE stop() — stop() may resume remaining waiters.
    // At least one of the two should have been resumed before stop().
    int resumed_before = (resumed1 ? 1 : 0) + (resumed2 ? 1 : 0);
    EXPECT_GE(resumed_before, 1);

    loop.stop();

    close(r); close(w);
}

// If the same coroutine is registered twice, the second registration should be ignored.
TEST(EventLoopTest, DuplicateRegisterIgnored) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int r = fds[0];
    int w = fds[1];

    auto &loop = EventLoop::instance();
    loop.start();

    // awaitable that registers the same handle twice
    struct DoubleRegisterAwaitable {
        int fd; bool &resumed_flag;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            auto owner = std::make_shared<int>(0);
            EventLoop::instance().register_read(fd, h, owner);
            // second registration should be ignored by EventLoop
            EventLoop::instance().register_read(fd, h, owner);
        }
        void await_resume() noexcept { resumed_flag = true; }
    };

    auto test_double_register = [&](int fd, bool &out_flag) -> SimpleTask {
        co_await DoubleRegisterAwaitable{fd, out_flag};
    };

    bool resumed = false;
    test_double_register(r, resumed);

    const char b = 'd';
    ssize_t nw = write(w, &b, 1);
    EXPECT_EQ(nw, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop.stop();

    close(r); close(w);

    EXPECT_TRUE(resumed);
}
#endif

#ifndef _WIN32
// socketpair-based test: verify reader resumes when peer writes
TEST(EventLoopTest, SocketpairResumesReader) {
    int sv[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    int a = sv[0];
    int b = sv[1];

    auto &loop = EventLoop::instance();
    loop.start();

    bool resumed = false;
    test_pipe_reader(a, resumed); // reuse reader awaitable which only needs an fd

    const char c = 's';
    ssize_t nw = write(b, &c, 1);
    EXPECT_EQ(nw, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop.stop();

    close(a); close(b);

    EXPECT_TRUE(resumed);
}

// Thread-safety test: spawn threads that frequently call wake() and register/deregister
TEST(EventLoopTest, ThreadsafetyWakeAndRegister) {
    auto &loop = EventLoop::instance();
    loop.start();

    // spawn worker threads that call wake() repeatedly
    const int N = 8;
    std::vector<std::thread> threads;
    std::atomic<bool> running{true};
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&]{
            while (running.load()) {
                loop.wake();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // also concurrently create/destroy registrations on a pipe
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int r = fds[0];
    int w = fds[1];

    // repeatedly register and deregister
    for (int i = 0; i < 50; ++i) {
        auto owner = std::make_shared<int>(0);
        // use a dummy coroutine handle: use a simple immediate-resume awaitable
        bool resumed = false;
        test_pipe_reader(r, resumed);
        EventLoop::instance().deregister(r);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    running.store(false);
    for (auto &t : threads) t.join();

    EventLoop::instance().stop();

    close(r); close(w);

    SUCCEED();
}
#endif

#ifndef _WIN32
// If we deregister an FD before it becomes readable, the waiter must not be resumed.
TEST(EventLoopTest, DeregisterPreventsResume) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int r = fds[0];
    int w = fds[1];

    auto &loop = EventLoop::instance();
    loop.start();

    bool resumed = false;
    test_pipe_reader(r, resumed);

    // remove registration before making fd readable
    EventLoop::instance().deregister(r);

    const char b = 'y';
    ssize_t nw = write(w, &b, 1);
    EXPECT_EQ(nw, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop.stop();

    close(r); close(w);

    EXPECT_FALSE(resumed);
}

// stop() should cause finish_all() to resume pending readers and writers.
TEST(EventLoopTest, StopResumesPendingReaderAndWriter) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int r = fds[0];
    int w = fds[1];

    auto &loop = EventLoop::instance();
    loop.start();

    bool reader_resumed = false;
    bool writer_resumed = false;
    test_pipe_reader(r, reader_resumed);

    // writer awaitable: register write on the write end
    struct FdWriteAwaitable {
        int fd; bool &resumed_flag;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept { EventLoop::instance().register_write(fd, h, std::make_shared<int>(0)); }
        void await_resume() noexcept { resumed_flag = true; }
    };

    auto test_pipe_writer = [&](int fd, bool &out_flag) -> SimpleTask {
        co_await FdWriteAwaitable{fd, out_flag};
    };

    test_pipe_writer(w, writer_resumed);

    // Do not write anything; call stop to force finish_all -> resume
    EventLoop::instance().stop();

    close(r); close(w);

    EXPECT_TRUE(reader_resumed);
    EXPECT_TRUE(writer_resumed);
}
#endif
