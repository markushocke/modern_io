#include <gtest/gtest.h>

#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <type_traits>
#include <utility>

import modern.runtime;

namespace {

struct ErrorCodeSender {
    std::error_code ec;

    template<class Receiver>
    struct operation {
        Receiver receiver;
        std::error_code ec;

        void start() {
            set_error(std::move(receiver), ec);
        }
    };

    template<class Receiver>
    auto connect(Receiver&& receiver) {
        using receiver_type = std::remove_cvref_t<Receiver>;
        return operation<receiver_type>{std::forward<Receiver>(receiver), ec};
    }
};

struct StoppedSender {
    template<class Receiver>
    struct operation {
        Receiver receiver;

        void start() {
            set_stopped(std::move(receiver));
        }
    };

    template<class Receiver>
    auto connect(Receiver&& receiver) {
        using receiver_type = std::remove_cvref_t<Receiver>;
        return operation<receiver_type>{std::forward<Receiver>(receiver)};
    }
};

} // namespace

TEST(RuntimeBridgeTest, SenderBridgeConvertsErrorCodeToSystemError) {
    auto task = modern::as_task<int>(ErrorCodeSender{std::make_error_code(std::errc::permission_denied)});

    try {
        (void)task.get();
        FAIL() << "expected std::system_error";
    } catch (const std::system_error& e) {
        EXPECT_EQ(e.code(), std::make_error_code(std::errc::permission_denied));
    } catch (...) {
        FAIL() << "unexpected exception type";
    }
}

TEST(RuntimeBridgeTest, SenderBridgeStoppedMapsToCancellationException) {
    auto task = modern::as_task<void>(StoppedSender{});

    try {
        task.get();
        FAIL() << "expected cancellation exception";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "operation cancelled");
    } catch (...) {
        FAIL() << "unexpected exception type";
    }
}

TEST(RuntimeBridgeTest, IoBridgeForwardsThrownExceptionFromStarter) {
    auto task = modern::bind_io<int>(modern::inline_scheduler(), [](modern::io_task_completion<int>) {
        throw std::runtime_error("io starter failure");
    });

    EXPECT_THROW((void)task.get(), std::runtime_error);
}

TEST(RuntimeBridgeTest, IoBridgeSupportsExplicitCompletionException) {
    auto task = modern::bind_io<int>(modern::inline_scheduler(), [](modern::io_task_completion<int> completion) {
        completion.set_exception(std::make_exception_ptr(std::runtime_error("io completion failure")));
    });

    EXPECT_THROW((void)task.get(), std::runtime_error);
}

TEST(RuntimeBridgeTest, IoBridgePreCancelledTokenSkipsStarterAndCancelsTask) {
    std::stop_source source;
    source.request_stop();

    int calls = 0;
    auto task = modern::bind_io<int>(
        modern::inline_scheduler(),
        source.get_token(),
        [&](modern::io_task_completion<int>) {
            ++calls;
        });

    EXPECT_EQ(calls, 0);

    try {
        (void)task.get();
        FAIL() << "expected cancellation exception";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "operation cancelled");
    } catch (...) {
        FAIL() << "unexpected exception type";
    }
}
