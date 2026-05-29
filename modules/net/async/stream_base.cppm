module;

#include <coroutine>
#include <expected>
#include <span>
#include <system_error>
#include <cstddef>

export module net_io.async_stream_base;

import net_io.event_loop; // for EventLoop
import net_io.task; // Task / IoTask alias
import net_io.async_utils; // keep utils available

export namespace modern::net {

class AsyncStreamBase {
public:
    [[nodiscard]] virtual IoTask<std::expected<std::size_t, std::error_code>>
    read_async(std::span<char> buf) = 0;

    [[nodiscard]] virtual IoTask<std::expected<std::size_t, std::error_code>>
    write_async(std::span<const char> buf) = 0;

    [[nodiscard]] virtual std::expected<void, std::error_code> flush() = 0;
    [[nodiscard]] virtual std::expected<bool, std::error_code> eof() = 0;

    virtual int native_handle() const noexcept = 0;
    virtual ~AsyncStreamBase() = default;

    // ---------- High-level wrappers ----------
    IoTask<std::expected<void, std::error_code>>
    read_exact_async(std::span<char> buf) {
        std::size_t done = 0;
        while (done < buf.size()) {
            auto r = co_await read_async(std::span<char>(buf.data() + done, buf.size() - done));
            if (!r) co_return std::unexpected(r.error());
            if (*r == 0)
                co_return std::unexpected(std::make_error_code(std::errc::io_error));
            done += *r;
        }
        co_return std::expected<void, std::error_code>{};
    }

    IoTask<std::expected<void, std::error_code>>
    write_all_async(std::span<const char> buf) {
        std::size_t done = 0;
        while (done < buf.size()) {
            auto w = co_await write_async(std::span<const char>(buf.data() + done, buf.size() - done));
            if (!w) co_return std::unexpected(w.error());
            if (*w == 0)
                co_return std::unexpected(std::make_error_code(std::errc::io_error));
            done += *w;
        }
        co_return std::expected<void, std::error_code>{};
    }
};

} // namespace modern::net

export namespace net_io {

using modern::net::AsyncStreamBase;

} // namespace net_io
