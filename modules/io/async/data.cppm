module;

#ifndef _MSC_VER
#include <vector>
#include <cstring>
#include <limits>
#include <bit>
#include <span>
#include <stdint.h>
#include <string>
#include <expected>
#include <coroutine>
#include <system_error>
#include <utility>
#include <array>
#endif

export module modern_io.async_data;
import modern_io.concepts;
import modern_io.task;
#ifdef _MSC_VER
import <vector>;
import <cstring>;
import <limits>;
import <bit>;
import <span>;
import <expected>;
import <coroutine>;
import <system_error>;
import <utility>;
import <array>;
#endif

namespace modern::io
{

// Asynchronous DataOutputStream in its own module.
export
template<AsyncOutputStream S>
class AsyncDataOutputStream
{
public:
    explicit AsyncDataOutputStream(S sink, std::endian order = std::endian::big)
      : sink_(std::move(sink)), order_(order) {}

    // Simple forwarding operations return the underlying awaiter directly.
    auto write_bytes(std::span<const std::byte> data) { return sink_.write_async(data); }
    auto write_bytes(const std::vector<std::byte>& data) {
        return sink_.write_async(std::span<const std::byte>(data.data(), data.size()));
    }
    std::expected<void,std::error_code> flush() { return sink_.flush(); }
    auto flush_async()
        requires requires(S& sink) { sink.flush_async(); }
    {
        return sink_.flush_async();
    }

    ExpectedTask<std::size_t> write_int32(int32_t v) {
        std::array<std::byte, 4> buf{};
        if (order_ == std::endian::big) {
            buf[0]=std::byte((v>>24)&0xFF); buf[1]=std::byte((v>>16)&0xFF);
            buf[2]=std::byte((v>> 8)&0xFF); buf[3]=std::byte((v    )&0xFF);
        } else {
            buf[3]=std::byte((v>>24)&0xFF); buf[2]=std::byte((v>>16)&0xFF);
            buf[1]=std::byte((v>> 8)&0xFF); buf[0]=std::byte((v    )&0xFF);
        }
        co_return co_await write_all(std::span<const std::byte>(buf));
    }
    auto write_uint32(uint32_t v) { return write_int32(static_cast<int32_t>(v)); }
    ExpectedTask<std::size_t> write_int64(int64_t v) {
        std::array<std::byte, 8> buf{};
        if (order_ == std::endian::big)
            for(int i=0;i<8;++i) buf[i]=std::byte((v>>(56-8*i))&0xFF);
        else
            for(int i=0;i<8;++i) buf[7-i]=std::byte((v>>(56-8*i))&0xFF);
        co_return co_await write_all(std::span<const std::byte>(buf));
    }
    auto write_uint64(uint64_t v){ return write_int64(static_cast<int64_t>(v)); }
    auto write_float(float f){
        uint32_t u; std::memcpy(&u,&f,4);
        return write_uint32(u);
    }
    auto write_double(double d){
        uint64_t u; std::memcpy(&u,&d,8);
        return write_uint64(u);
    }

    // Multi-step operation implemented as a coroutine.
    ExpectedTask<std::size_t> write_string(std::string s) {
        std::size_t total = 0;
        auto lenAw = write_int32(static_cast<int32_t>(s.size()));
        auto lr = co_await std::move(lenAw);
        if (!lr) { co_return std::unexpected(lr.error()); }
        total += *lr;
        auto br = co_await write_all(std::span<const char>(s.data(), s.size()));
        if (!br) { co_return std::unexpected(br.error()); }
        total += *br;
        co_return std::expected<std::size_t,std::error_code>{total};
    }

private:
    template<class Element>
    ExpectedTask<std::size_t> write_all(std::span<const Element> data) {
        std::size_t written = 0;
        while (written < data.size()) {
            auto aw = sink_.write_async(data.subspan(written));
            auto result = co_await std::move(aw);
            if (!result) {
                co_return std::unexpected(result.error());
            }
            if (*result == 0) {
                co_return std::unexpected(std::make_error_code(std::errc::io_error));
            }
            written += *result;
        }
        co_return std::expected<std::size_t, std::error_code>{written};
    }

    S sink_;
    std::endian order_;
};

// AsyncDataInputStream in its own module.
export
template<AsyncInputStream S>
class AsyncDataInputStream
{
public:
    explicit AsyncDataInputStream(S source, std::endian order = std::endian::big)
      : source_(std::move(source)), order_(order) {}

    ExpectedTask<std::vector<std::byte>> read_bytes(std::size_t n) {
        std::vector<std::byte> buf(n);
        std::size_t filled = 0;
        while (filled < n) {
            auto aw = source_.read_async(reinterpret_cast<char*>(buf.data()) + filled, n - filled);
            auto r  = co_await std::move(aw);
            if (!r) { co_return std::unexpected(r.error()); }
            if (*r == 0) {
                co_return std::unexpected(std::make_error_code(std::errc::result_out_of_range));
            }
            filled += *r;
        }
        co_return std::expected<std::vector<std::byte>,std::error_code>{ std::move(buf) };
    }

    ExpectedTask<int32_t> read_int32() {
        auto br = co_await read_bytes(4);
        if (!br) co_return std::unexpected(br.error());
        auto ptr = reinterpret_cast<const uint8_t*>(br->data());
        int32_t v;
        if (order_ == std::endian::big)
            v = (ptr[0]<<24)|(ptr[1]<<16)|(ptr[2]<<8)|ptr[3];
        else
            v = (ptr[3]<<24)|(ptr[2]<<16)|(ptr[1]<<8)|ptr[0];
        co_return std::expected<int32_t,std::error_code>{v};
    }

    ExpectedTask<uint32_t> read_uint32() {
        auto r = co_await read_int32();
        if (!r) co_return std::unexpected(r.error());
        co_return std::expected<uint32_t,std::error_code>{ static_cast<uint32_t>(*r) };
    }

    ExpectedTask<int64_t> read_int64() {
        auto br = co_await read_bytes(8);
        if (!br) co_return std::unexpected(br.error());
        auto p = reinterpret_cast<const uint8_t*>(br->data());
        int64_t v=0;
        if (order_ == std::endian::big)
            for(int i=0;i<8;++i) v |= int64_t(p[i]) << (56-8*i);
        else
            for(int i=0;i<8;++i) v |= int64_t(p[7-i]) << (56-8*i);
        co_return std::expected<int64_t,std::error_code>{v};
    }

    ExpectedTask<uint64_t> read_uint64() {
        auto r = co_await read_int64();
        if (!r) co_return std::unexpected(r.error());
        co_return std::expected<uint64_t,std::error_code>{ static_cast<uint64_t>(*r) };
    }

    ExpectedTask<float> read_float() {
        auto r = co_await read_uint32();
        if (!r) co_return std::unexpected(r.error());
        float f; std::memcpy(&f,&*r,4);
        co_return std::expected<float,std::error_code>{f};
    }

    ExpectedTask<double> read_double() {
        auto r = co_await read_uint64();
        if (!r) co_return std::unexpected(r.error());
        double d; std::memcpy(&d,&*r,8);
        co_return std::expected<double,std::error_code>{d};
    }

    ExpectedTask<std::string> read_string() {
        auto lenr = co_await read_int32();
        if (!lenr) co_return std::unexpected(lenr.error());
        int32_t len = *lenr;
        if (len < 0 || len > 1'000'000)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        auto br = co_await read_bytes(static_cast<std::size_t>(len));
        if (!br) co_return std::unexpected(br.error());
        std::string s(reinterpret_cast<char*>(br->data()), br->size());
        co_return std::expected<std::string,std::error_code>{ std::move(s) };
    }

private:
    S source_;
    std::endian order_;
};

// Deduction guides.
template<typename Stream>
AsyncDataOutputStream(Stream&&, std::endian) -> AsyncDataOutputStream<std::decay_t<Stream>>;

template<typename Stream>
AsyncDataInputStream(Stream&&, std::endian) -> AsyncDataInputStream<std::decay_t<Stream>>;

} // namespace modern::io

export namespace modern_io {

using modern::io::AsyncDataInputStream;
using modern::io::AsyncDataOutputStream;

} // namespace modern_io
