module;

#ifndef _MSC_VER
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#else
export import <atomic>;
export import <array>;
export import <cstddef>;
export import <cstdint>;
export import <expected>;
export import <limits>;
export import <memory_resource>;
export import <span>;
export import <string>;
export import <string_view>;
export import <vector>;
#endif

export module modern_io.trace;

import modern_io.concepts;
import modern_io.connection_arena;

export namespace modern::io {

enum class TraceContextParseError {
    InvalidLength,
    InvalidDelimiter,
    InvalidVersion,
    InvalidHex,
    ZeroTraceId,
    ZeroSpanId,
};

enum class TraceQueueError {
    BufferFull,
    RecordTooLarge,
};

enum class TraceDrainMode {
    Flush,
    NoFlush,
};

enum class TraceDrainState {
    Idle,
    Requested,
};

struct TraceDrainStepResult {
    std::size_t drained_records = 0;
    TraceDrainState next_state = TraceDrainState::Idle;

    [[nodiscard]] bool should_reschedule() const noexcept {
        return next_state == TraceDrainState::Requested;
    }
};

struct TraceContext {
    std::uint8_t version = 0;
    std::array<std::byte, 16> trace_id{};
    std::array<std::byte, 8> span_id{};
    std::uint8_t trace_flags = 1;

    friend bool operator==(const TraceContext&, const TraceContext&) = default;

    [[nodiscard]] bool has_zero_trace_id() const noexcept {
        for (auto byte : trace_id) {
            if (byte != std::byte{}) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool has_zero_span_id() const noexcept {
        for (auto byte : span_id) {
            if (byte != std::byte{}) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return version != 0xff && !has_zero_trace_id() && !has_zero_span_id();
    }
};

namespace detail {

inline constexpr std::string_view traceparent_prefix = "{\"traceparent\":\"";
inline constexpr std::string_view name_prefix = "\",\"name\":\"";
inline constexpr std::string_view message_prefix = "\",\"message\":\"";
inline constexpr std::string_view record_suffix = "\"}\n";

[[nodiscard]] constexpr int hex_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

template<std::size_t N>
[[nodiscard]] std::expected<std::array<std::byte, N>, TraceContextParseError>
parse_hex_bytes(std::string_view text) noexcept {
    if (text.size() != N * 2) {
        return std::unexpected(TraceContextParseError::InvalidLength);
    }

    std::array<std::byte, N> bytes{};
    for (std::size_t index = 0; index < N; ++index) {
        const int hi = hex_value(text[index * 2]);
        const int lo = hex_value(text[index * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return std::unexpected(TraceContextParseError::InvalidHex);
        }
        bytes[index] = static_cast<std::byte>((hi << 4) | lo);
    }

    return bytes;
}

template<typename Buffer>
inline void append_hex_byte(Buffer& out, std::byte value) {
    constexpr char digits[] = "0123456789abcdef";
    const auto byte = std::to_integer<unsigned char>(value);
    out.push_back(digits[(byte >> 4) & 0x0f]);
    out.push_back(digits[byte & 0x0f]);
}

template<typename Buffer>
inline void append_text(Buffer& out, std::string_view text) {
    for (char ch : text) {
        out.push_back(ch);
    }
}

template<std::size_t N>
inline void append_hex_bytes(auto& out, const std::array<std::byte, N>& bytes) {
    for (auto byte : bytes) {
        append_hex_byte(out, byte);
    }
}

inline void append_traceparent(auto& out, const TraceContext& context) {
    append_hex_byte(out, static_cast<std::byte>(context.version));
    out.push_back('-');
    append_hex_bytes(out, context.trace_id);
    out.push_back('-');
    append_hex_bytes(out, context.span_id);
    out.push_back('-');
    append_hex_byte(out, static_cast<std::byte>(context.trace_flags));
}

inline std::size_t escaped_json_size(std::string_view text) noexcept {
    std::size_t size = 0;
    for (char ch : text) {
        switch (ch) {
            case '\\':
            case '"':
            case '\n':
            case '\r':
            case '\t':
                size += 2;
                break;
            default:
                ++size;
                break;
        }
    }
    return size;
}

inline std::size_t json_record_size(std::string_view name, std::string_view message) noexcept {
    return traceparent_prefix.size() + 55 + name_prefix.size() + escaped_json_size(name)
        + message_prefix.size() + escaped_json_size(message) + record_suffix.size();
}

inline void append_json_escaped(auto& out, std::string_view text) {
    for (char ch : text) {
        switch (ch) {
            case '\\':
                append_text(out, "\\\\");
                break;
            case '"':
                append_text(out, "\\\"");
                break;
            case '\n':
                append_text(out, "\\n");
                break;
            case '\r':
                append_text(out, "\\r");
                break;
            case '\t':
                append_text(out, "\\t");
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
}

inline void append_json_record(auto& out, const TraceContext& context, std::string_view name, std::string_view message) {
    append_text(out, traceparent_prefix);
    append_traceparent(out, context);
    append_text(out, name_prefix);
    append_json_escaped(out, name);
    append_text(out, message_prefix);
    append_json_escaped(out, message);
    append_text(out, record_suffix);
}

struct FixedBufferWriter {
    char* data;
    std::size_t capacity;
    std::size_t size_{0};

    void push_back(char ch) {
        data[size_++] = ch;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
};

} // namespace detail

class InMemoryTraceBuffer {
public:
    explicit InMemoryTraceBuffer(
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : buffer_(resource) {}

    explicit InMemoryTraceBuffer(ConnectionArena& arena)
        : InMemoryTraceBuffer(arena.memory_resource()) {}

    void append_json(const TraceContext& context, std::string_view name, std::string_view message) {
        detail::append_json_record(buffer_, context, name, message);
    }

    template<OutputStream S>
    void flush_to(S& sink) {
        if (!buffer_.empty()) {
            sink.write(buffer_.data(), buffer_.size());
            buffer_.clear();
        }
        sink.flush();
    }

    void clear() noexcept {
        buffer_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return buffer_.empty();
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept {
        return buffer_.size();
    }

    [[nodiscard]] std::span<const char> view() const noexcept {
        return std::span<const char>(buffer_.data(), buffer_.size());
    }

private:
    std::pmr::vector<char> buffer_;
};

class SpscTraceDrainQueue {
public:
    SpscTraceDrainQueue(
        std::size_t capacity,
        std::size_t max_record_bytes,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : storage_(resource),
          sizes_(resource),
          capacity_(capacity == 0 ? 1 : capacity),
          max_record_bytes_(max_record_bytes == 0 ? 1 : max_record_bytes) {
        storage_.resize(capacity_ * max_record_bytes_);
        sizes_.resize(capacity_);
    }

    SpscTraceDrainQueue(std::size_t capacity, std::size_t max_record_bytes, ConnectionArena& arena)
        : SpscTraceDrainQueue(capacity, max_record_bytes, arena.memory_resource()) {}

    [[nodiscard]] std::expected<void, TraceQueueError>
    try_push_json(const TraceContext& context, std::string_view name, std::string_view message) {
        const auto required_size = detail::json_record_size(name, message);
        if (required_size > max_record_bytes_) {
            return std::unexpected(TraceQueueError::RecordTooLarge);
        }

        const auto read = read_index_.load(std::memory_order_acquire);
        const auto write = write_index_.load(std::memory_order_relaxed);
        if (write - read >= capacity_) {
            return std::unexpected(TraceQueueError::BufferFull);
        }

        const auto slot = write % capacity_;
        detail::FixedBufferWriter writer{ slot_ptr(slot), max_record_bytes_ };
        detail::append_json_record(writer, context, name, message);
        sizes_[slot] = writer.size();
        write_index_.store(write + 1, std::memory_order_release);
        return {};
    }

    template<OutputStream S>
    std::size_t drain_to(
        S& sink,
        std::size_t max_records = std::numeric_limits<std::size_t>::max(),
        TraceDrainMode mode = TraceDrainMode::Flush) {
        std::size_t drained = 0;
        auto read = read_index_.load(std::memory_order_relaxed);
        const auto write = write_index_.load(std::memory_order_acquire);

        while (read != write && drained < max_records) {
            const auto slot = read % capacity_;
            sink.write(slot_ptr(slot), sizes_[slot]);
            ++read;
            ++drained;
        }

        if (drained > 0 && mode == TraceDrainMode::Flush) {
            sink.flush();
        }

        if (drained > 0) {
            read_index_.store(read, std::memory_order_release);
        }

        return drained;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto write = write_index_.load(std::memory_order_acquire);
        const auto read = read_index_.load(std::memory_order_acquire);
        return write - read;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] std::size_t max_record_bytes() const noexcept {
        return max_record_bytes_;
    }

private:
    [[nodiscard]] char* slot_ptr(std::size_t slot) noexcept {
        return storage_.data() + (slot * max_record_bytes_);
    }

    [[nodiscard]] const char* slot_ptr(std::size_t slot) const noexcept {
        return storage_.data() + (slot * max_record_bytes_);
    }

    std::pmr::vector<char> storage_;
    std::pmr::vector<std::size_t> sizes_;
    std::size_t capacity_;
    std::size_t max_record_bytes_;
    std::atomic<std::size_t> read_index_{0};
    std::atomic<std::size_t> write_index_{0};
};

class TraceDrainSignal {
public:
    void request() noexcept {
        requested_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool is_requested() const noexcept {
        return requested_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool consume() noexcept {
        return requested_.exchange(false, std::memory_order_acq_rel);
    }

private:
    std::atomic<bool> requested_{false};
};

class TraceBackgroundDrainWaker {
public:
    virtual ~TraceBackgroundDrainWaker() = default;

    virtual void wake_trace_background_drain() = 0;
};

class TraceBackgroundDrainController;

class TraceLogger {
public:
    explicit TraceLogger(SpscTraceDrainQueue& queue) noexcept
        : queue_(&queue) {}

    TraceLogger(SpscTraceDrainQueue& queue, TraceDrainSignal& signal) noexcept
        : queue_(&queue), signal_(&signal) {}

    TraceLogger(SpscTraceDrainQueue& queue, TraceBackgroundDrainController& background) noexcept
        : queue_(&queue), background_(&background) {}

    [[nodiscard]] std::expected<void, TraceQueueError>
    try_log_json(const TraceContext& context, std::string_view name, std::string_view message);

    template<OutputStream S>
    std::size_t drain_to(
        S& sink,
        std::size_t max_records = std::numeric_limits<std::size_t>::max(),
        TraceDrainMode mode = TraceDrainMode::Flush) {
        return queue_->drain_to(sink, max_records, mode);
    }

    [[nodiscard]] bool empty() const noexcept {
        return queue_->empty();
    }

private:
    SpscTraceDrainQueue* queue_;
    TraceDrainSignal* signal_{nullptr};
    TraceBackgroundDrainController* background_{nullptr};
};

class TraceLoggerFanIn {
public:
    explicit TraceLoggerFanIn(
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : loggers_(resource) {}

    explicit TraceLoggerFanIn(ConnectionArena& arena)
        : TraceLoggerFanIn(arena.memory_resource()) {}

    void add_logger(TraceLogger& logger) {
        loggers_.push_back(&logger);
    }

    [[nodiscard]] std::size_t source_count() const noexcept {
        return loggers_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        for (const auto* logger : loggers_) {
            if (!logger->empty()) {
                return false;
            }
        }
        return true;
    }

    template<OutputStream S>
    std::size_t drain_to(
        S& sink,
        std::size_t max_records = std::numeric_limits<std::size_t>::max(),
        TraceDrainMode mode = TraceDrainMode::Flush) {
        if (loggers_.empty() || max_records == 0) {
            return 0;
        }

        std::size_t drained = 0;
        std::size_t idle_sources = 0;

        while (drained < max_records && idle_sources < loggers_.size()) {
            const auto index = next_index_;
            next_index_ = (next_index_ + 1) % loggers_.size();

            const auto drained_now = loggers_[index]->drain_to(sink, 1, TraceDrainMode::NoFlush);
            if (drained_now == 0) {
                ++idle_sources;
                continue;
            }

            drained += drained_now;
            idle_sources = 0;
        }

        if (drained > 0 && mode == TraceDrainMode::Flush) {
            sink.flush();
        }

        return drained;
    }

private:
    std::pmr::vector<TraceLogger*> loggers_;
    std::size_t next_index_{0};
};

class TraceDrainPump {
public:
    explicit TraceDrainPump(TraceLoggerFanIn& fan_in) noexcept
        : fan_in_(&fan_in) {}

    TraceDrainPump(TraceLoggerFanIn& fan_in, TraceDrainSignal& signal) noexcept
        : fan_in_(&fan_in), signal_(&signal) {}

    [[nodiscard]] bool drain_requested() const noexcept {
        return signal_ != nullptr && signal_->is_requested();
    }

    template<OutputStream S>
    TraceDrainStepResult drain_step(
        S& sink,
        std::size_t max_records = std::numeric_limits<std::size_t>::max(),
        TraceDrainMode mode = TraceDrainMode::Flush) {
        if (signal_ != nullptr) {
            (void)signal_->consume();
        }

        const auto drained = fan_in_->drain_to(sink, max_records, mode);
        const auto has_more = !fan_in_->empty();
        if (signal_ != nullptr && has_more) {
            signal_->request();
        }

        const auto next_requested = signal_ != nullptr ? signal_->is_requested() : has_more;

        return TraceDrainStepResult{
            .drained_records = drained,
            .next_state = next_requested ? TraceDrainState::Requested : TraceDrainState::Idle,
        };
    }

private:
    TraceLoggerFanIn* fan_in_;
    TraceDrainSignal* signal_{nullptr};
};

class TraceBackgroundDrainController {
public:
    TraceBackgroundDrainController(TraceDrainSignal& signal, TraceDrainPump& pump) noexcept
        : signal_(&signal), pump_(&pump) {}

    TraceBackgroundDrainController(
        TraceDrainSignal& signal,
        TraceDrainPump& pump,
        TraceBackgroundDrainWaker& waker) noexcept
        : signal_(&signal), pump_(&pump), waker_(&waker) {}

    void bind_waker(TraceBackgroundDrainWaker& waker) noexcept {
        waker_ = &waker;
    }

    void request_background_drain() noexcept {
        signal_->request();
        arm_background_drain();
    }

    [[nodiscard]] bool drain_requested() const noexcept {
        return signal_->is_requested();
    }

    [[nodiscard]] bool background_drain_armed() const noexcept {
        return background_drain_armed_.load(std::memory_order_acquire);
    }

    template<OutputStream S>
    TraceDrainStepResult drain_once(
        S& sink,
        std::size_t max_records = std::numeric_limits<std::size_t>::max(),
        TraceDrainMode mode = TraceDrainMode::Flush) {
        const auto step = pump_->drain_step(sink, max_records, mode);
        background_drain_armed_.store(false, std::memory_order_release);

        if (step.should_reschedule() || signal_->is_requested()) {
            arm_background_drain();
        }

        return step;
    }

private:
    void arm_background_drain() noexcept {
        bool expected = false;
        if (background_drain_armed_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)
            && waker_ != nullptr) {
            waker_->wake_trace_background_drain();
        }
    }

    TraceDrainSignal* signal_;
    TraceDrainPump* pump_;
    TraceBackgroundDrainWaker* waker_{nullptr};
    std::atomic<bool> background_drain_armed_{false};
};

inline std::expected<void, TraceQueueError>
TraceLogger::try_log_json(const TraceContext& context, std::string_view name, std::string_view message) {
    auto result = queue_->try_push_json(context, name, message);
    if (!result.has_value()) {
        return result;
    }

    if (background_ != nullptr) {
        background_->request_background_drain();
    } else if (signal_ != nullptr) {
        signal_->request();
    }

    return result;
}

[[nodiscard]] inline std::expected<TraceContext, TraceContextParseError>
parse_traceparent(std::string_view header) noexcept {
    if (header.size() != 55) {
        return std::unexpected(TraceContextParseError::InvalidLength);
    }

    if (header[2] != '-' || header[35] != '-' || header[52] != '-') {
        return std::unexpected(TraceContextParseError::InvalidDelimiter);
    }

    auto version = detail::parse_hex_bytes<1>(header.substr(0, 2));
    if (!version) {
        return std::unexpected(version.error() == TraceContextParseError::InvalidHex
            ? TraceContextParseError::InvalidVersion
            : version.error());
    }

    TraceContext context{};
    context.version = std::to_integer<std::uint8_t>(version.value()[0]);
    if (context.version == 0xff) {
        return std::unexpected(TraceContextParseError::InvalidVersion);
    }

    auto trace_id = detail::parse_hex_bytes<16>(header.substr(3, 32));
    if (!trace_id) {
        return std::unexpected(trace_id.error());
    }
    context.trace_id = trace_id.value();
    if (context.has_zero_trace_id()) {
        return std::unexpected(TraceContextParseError::ZeroTraceId);
    }

    auto span_id = detail::parse_hex_bytes<8>(header.substr(36, 16));
    if (!span_id) {
        return std::unexpected(span_id.error());
    }
    context.span_id = span_id.value();
    if (context.has_zero_span_id()) {
        return std::unexpected(TraceContextParseError::ZeroSpanId);
    }

    auto trace_flags = detail::parse_hex_bytes<1>(header.substr(53, 2));
    if (!trace_flags) {
        return std::unexpected(trace_flags.error());
    }
    context.trace_flags = std::to_integer<std::uint8_t>(trace_flags.value()[0]);
    return context;
}

[[nodiscard]] inline std::string format_traceparent(const TraceContext& context) {
    std::string result;
    result.reserve(55);
    detail::append_hex_byte(result, static_cast<std::byte>(context.version));
    result.push_back('-');
    detail::append_hex_bytes(result, context.trace_id);
    result.push_back('-');
    detail::append_hex_bytes(result, context.span_id);
    result.push_back('-');
    detail::append_hex_byte(result, static_cast<std::byte>(context.trace_flags));
    return result;
}

} // namespace modern::io

export namespace modern_io {

using modern::io::format_traceparent;
using modern::io::InMemoryTraceBuffer;
using modern::io::parse_traceparent;
using modern::io::SpscTraceDrainQueue;
using modern::io::TraceBackgroundDrainController;
using modern::io::TraceBackgroundDrainWaker;
using modern::io::TraceContext;
using modern::io::TraceContextParseError;
using modern::io::TraceDrainMode;
using modern::io::TraceDrainPump;
using modern::io::TraceDrainSignal;
using modern::io::TraceDrainState;
using modern::io::TraceDrainStepResult;
using modern::io::TraceLogger;
using modern::io::TraceLoggerFanIn;
using modern::io::TraceQueueError;

} // namespace modern_io