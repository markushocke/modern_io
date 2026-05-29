import modern_io;

#include <gtest/gtest.h>
#include <memory_resource>
#include <sstream>
#include <string>

namespace {

class CountingMemoryResource : public std::pmr::memory_resource {
public:
    std::size_t allocation_count = 0;
    std::size_t deallocation_count = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocation_count;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        ++deallocation_count;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

class CountingTraceBackgroundDrainWaker final : public modern_io::TraceBackgroundDrainWaker {
public:
    std::size_t wake_count = 0;

    void wake_trace_background_drain() override {
        ++wake_count;
    }
};

modern_io::TraceContext make_trace_context() {
    modern_io::TraceContext context;
    context.version = 0x00;
    context.trace_id = {
        std::byte{0x4b}, std::byte{0xf9}, std::byte{0x2f}, std::byte{0x35},
        std::byte{0x77}, std::byte{0xb3}, std::byte{0x4d}, std::byte{0xa6},
        std::byte{0xa3}, std::byte{0xce}, std::byte{0x92}, std::byte{0x9d},
        std::byte{0x0e}, std::byte{0x0e}, std::byte{0x47}, std::byte{0x36},
    };
    context.span_id = {
        std::byte{0x00}, std::byte{0xf0}, std::byte{0x67}, std::byte{0xaa},
        std::byte{0x0b}, std::byte{0xa9}, std::byte{0x02}, std::byte{0xb7},
    };
    context.flags = 0x01;
    return context;
}

} // namespace

TEST(TraceContextTest, ParsesValidTraceparent) {
    auto parsed = modern_io::parse_traceparent(
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version, 0x00);
    EXPECT_EQ(parsed->flags, 0x01);
    EXPECT_FALSE(parsed->has_zero_trace_id());
    EXPECT_FALSE(parsed->has_zero_span_id());
    EXPECT_TRUE(parsed->is_valid());
}

TEST(TraceContextTest, FormatsCanonicalTraceparent) {
    const auto context = make_trace_context();

    EXPECT_EQ(
        modern_io::format_traceparent(context),
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
}

TEST(TraceContextTest, RoundtripsUppercaseInputToCanonicalLowercase) {
    auto parsed = modern_io::parse_traceparent(
        "00-4BF92F3577B34DA6A3CE929D0E0E4736-00F067AA0BA902B7-01");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(
        modern_io::format_traceparent(parsed.value()),
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
}

TEST(TraceContextTest, RejectsZeroTraceAndSpanIds) {
    auto zero_trace_id = modern_io::parse_traceparent(
        "00-00000000000000000000000000000000-00f067aa0ba902b7-01");
    ASSERT_FALSE(zero_trace_id.has_value());
    EXPECT_EQ(zero_trace_id.error(), modern_io::TraceContextParseError::ZeroTraceId);

    auto zero_span_id = modern_io::parse_traceparent(
        "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01");
    ASSERT_FALSE(zero_span_id.has_value());
    EXPECT_EQ(zero_span_id.error(), modern_io::TraceContextParseError::ZeroSpanId);
}

TEST(TraceContextTest, RejectsMalformedTraceparent) {
    auto invalid_version = modern_io::parse_traceparent(
        "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
    ASSERT_FALSE(invalid_version.has_value());
    EXPECT_EQ(invalid_version.error(), modern_io::TraceContextParseError::InvalidVersion);

    auto invalid_hex = modern_io::parse_traceparent(
        "00-4bf92f3577b34da6a3ce929d0e0e473g-00f067aa0ba902b7-01");
    ASSERT_FALSE(invalid_hex.has_value());
    EXPECT_EQ(invalid_hex.error(), modern_io::TraceContextParseError::InvalidHex);
}

TEST(TraceContextTest, InMemoryTraceBufferFlushesJsonToOutputStream) {
    modern_io::InMemoryTraceBuffer buffer;
    const auto context = make_trace_context();

    buffer.append_json(context, "accept", "connected");
    buffer.append_json(context, "read", "payload");

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    buffer.flush_to(sink);

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"connected\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"read\",\"message\":\"payload\"}\n");
}

TEST(TraceContextTest, InMemoryTraceBufferEscapesJsonStrings) {
    modern_io::InMemoryTraceBuffer buffer;
    const auto context = make_trace_context();

    buffer.append_json(context, "say \"hi\"", "line1\nline2\\tail");

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    buffer.flush_to(sink);

    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"say \\\"hi\\\"\",\"message\":\"line1\\nline2\\\\tail\"}\n");
}

TEST(TraceContextTest, InMemoryTraceBufferUsesConnectionArenaWithoutUpstreamAllocations) {
    CountingMemoryResource upstream;
    modern_io::ConnectionArena arena(512, &upstream);
    modern_io::InMemoryTraceBuffer buffer(arena);

    buffer.append_json(make_trace_context(), "accept", "connected");

    EXPECT_GT(buffer.size_bytes(), 0u);
    EXPECT_EQ(upstream.allocation_count, 0u);
}

TEST(TraceContextTest, SpscTraceDrainQueueDrainsRecordsInOrder) {
    modern_io::SpscTraceDrainQueue queue(4, 256);
    const auto context = make_trace_context();

    ASSERT_TRUE(queue.try_push_json(context, "accept", "connected").has_value());
    ASSERT_TRUE(queue.try_push_json(context, "read", "payload").has_value());

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    EXPECT_EQ(queue.drain_to(sink), 2u);
    EXPECT_TRUE(queue.empty());

    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"connected\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"read\",\"message\":\"payload\"}\n");
}

TEST(TraceContextTest, SpscTraceDrainQueueReportsBufferFull) {
    modern_io::SpscTraceDrainQueue queue(1, 256);
    const auto context = make_trace_context();

    ASSERT_TRUE(queue.try_push_json(context, "accept", "connected").has_value());

    auto second = queue.try_push_json(context, "read", "payload");
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), modern_io::TraceQueueError::BufferFull);
}

TEST(TraceContextTest, SpscTraceDrainQueueRejectsOversizedRecord) {
    modern_io::SpscTraceDrainQueue queue(2, 96);
    const auto context = make_trace_context();
    std::string oversized_message(128, 'x');

    auto result = queue.try_push_json(context, "accept", oversized_message);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), modern_io::TraceQueueError::RecordTooLarge);
    EXPECT_TRUE(queue.empty());
}

TEST(TraceContextTest, SpscTraceDrainQueueUsesConnectionArenaWithoutUpstreamAllocations) {
    CountingMemoryResource upstream;
    modern_io::ConnectionArena arena(2048, &upstream);
    modern_io::SpscTraceDrainQueue queue(4, 256, arena);

    auto result = queue.try_push_json(make_trace_context(), "accept", "connected");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(upstream.allocation_count, 0u);
}

TEST(TraceContextTest, TraceLoggerDrainsQueueInBatches) {
    modern_io::SpscTraceDrainQueue queue(4, 256);
    modern_io::TraceLogger logger(queue);
    const auto context = make_trace_context();

    ASSERT_TRUE(logger.try_log_json(context, "accept", "connected").has_value());
    ASSERT_TRUE(logger.try_log_json(context, "read", "payload").has_value());

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    EXPECT_EQ(logger.drain_to(sink, 1), 1u);
    EXPECT_FALSE(logger.empty());
    EXPECT_EQ(logger.drain_to(sink), 1u);
    EXPECT_TRUE(logger.empty());

    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"connected\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"read\",\"message\":\"payload\"}\n");
}

TEST(TraceContextTest, TraceLoggerFanInDrainsSourcesRoundRobin) {
    modern_io::SpscTraceDrainQueue queue_a(4, 256);
    modern_io::SpscTraceDrainQueue queue_b(4, 256);
    modern_io::TraceLogger logger_a(queue_a);
    modern_io::TraceLogger logger_b(queue_b);
    modern_io::TraceLoggerFanIn fan_in;
    const auto context = make_trace_context();

    fan_in.add_logger(logger_a);
    fan_in.add_logger(logger_b);

    ASSERT_TRUE(logger_a.try_log_json(context, "accept", "client-a").has_value());
    ASSERT_TRUE(logger_a.try_log_json(context, "read", "payload-a").has_value());
    ASSERT_TRUE(logger_b.try_log_json(context, "accept", "client-b").has_value());
    ASSERT_TRUE(logger_b.try_log_json(context, "write", "payload-b").has_value());

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    EXPECT_EQ(fan_in.drain_to(sink, 3), 3u);
    EXPECT_FALSE(fan_in.empty());
    EXPECT_EQ(fan_in.drain_to(sink), 1u);
    EXPECT_TRUE(fan_in.empty());

    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"client-a\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"client-b\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"read\",\"message\":\"payload-a\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"write\",\"message\":\"payload-b\"}\n");
}

TEST(TraceContextTest, TraceLoggerFanInUsesConnectionArenaWithoutUpstreamAllocations) {
    CountingMemoryResource upstream;
    modern_io::ConnectionArena arena(512, &upstream);
    modern_io::SpscTraceDrainQueue queue_a(2, 256);
    modern_io::SpscTraceDrainQueue queue_b(2, 256);
    modern_io::TraceLogger logger_a(queue_a);
    modern_io::TraceLogger logger_b(queue_b);
    modern_io::TraceLoggerFanIn fan_in(arena);

    fan_in.add_logger(logger_a);
    fan_in.add_logger(logger_b);

    EXPECT_EQ(fan_in.source_count(), 2u);
    EXPECT_EQ(upstream.allocation_count, 0u);
}

TEST(TraceContextTest, TraceLoggerSignalsDrainRequestOnSuccessfulLog) {
    modern_io::SpscTraceDrainQueue queue(2, 256);
    modern_io::TraceDrainSignal signal;
    modern_io::TraceLogger logger(queue, signal);

    EXPECT_FALSE(signal.is_requested());
    ASSERT_TRUE(logger.try_log_json(make_trace_context(), "accept", "connected").has_value());
    EXPECT_TRUE(signal.is_requested());
}

TEST(TraceContextTest, TraceDrainPumpRearmsSignalWhileMoreWorkRemains) {
    modern_io::SpscTraceDrainQueue queue_a(4, 256);
    modern_io::SpscTraceDrainQueue queue_b(4, 256);
    modern_io::TraceDrainSignal signal;
    modern_io::TraceLogger logger_a(queue_a, signal);
    modern_io::TraceLogger logger_b(queue_b, signal);
    modern_io::TraceLoggerFanIn fan_in;
    modern_io::TraceDrainPump pump(fan_in, signal);
    const auto context = make_trace_context();

    fan_in.add_logger(logger_a);
    fan_in.add_logger(logger_b);

    ASSERT_TRUE(logger_a.try_log_json(context, "accept", "client-a").has_value());
    ASSERT_TRUE(logger_b.try_log_json(context, "accept", "client-b").has_value());
    ASSERT_TRUE(logger_a.try_log_json(context, "read", "payload-a").has_value());
    EXPECT_TRUE(pump.drain_requested());

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    const auto first = pump.drain_step(sink, 2);
    EXPECT_EQ(first.drained_records, 2u);
    EXPECT_TRUE(first.should_reschedule());
    EXPECT_TRUE(pump.drain_requested());

    const auto second = pump.drain_step(sink, 2);
    EXPECT_EQ(second.drained_records, 1u);
    EXPECT_FALSE(second.should_reschedule());
    EXPECT_FALSE(pump.drain_requested());

    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"client-a\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"client-b\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"read\",\"message\":\"payload-a\"}\n");
}

TEST(TraceContextTest, TraceDrainPumpStaysIdleWithoutPendingRecords) {
    modern_io::TraceLoggerFanIn fan_in;
    modern_io::TraceDrainSignal signal;
    modern_io::TraceDrainPump pump(fan_in, signal);

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    const auto result = pump.drain_step(sink, 4);

    EXPECT_EQ(result.drained_records, 0u);
    EXPECT_FALSE(result.should_reschedule());
    EXPECT_FALSE(pump.drain_requested());
    EXPECT_TRUE(oss.str().empty());
}

TEST(TraceContextTest, TraceBackgroundDrainControllerWakesOnlyOnceWhileArmed) {
    modern_io::SpscTraceDrainQueue queue(4, 256);
    modern_io::TraceDrainSignal signal;
    modern_io::TraceLoggerFanIn fan_in;
    modern_io::TraceDrainPump pump(fan_in, signal);
    CountingTraceBackgroundDrainWaker waker;
    modern_io::TraceBackgroundDrainController background(signal, pump, waker);
    modern_io::TraceLogger logger(queue, background);

    fan_in.add_logger(logger);

    ASSERT_TRUE(logger.try_log_json(make_trace_context(), "accept", "client-a").has_value());
    ASSERT_TRUE(logger.try_log_json(make_trace_context(), "read", "payload-a").has_value());

    EXPECT_TRUE(background.drain_requested());
    EXPECT_TRUE(background.background_drain_armed());
    EXPECT_EQ(waker.wake_count, 1u);
}

TEST(TraceContextTest, TraceBackgroundDrainControllerRearmsAfterPartialDrain) {
    modern_io::SpscTraceDrainQueue queue(4, 256);
    modern_io::TraceDrainSignal signal;
    modern_io::TraceLoggerFanIn fan_in;
    modern_io::TraceDrainPump pump(fan_in, signal);
    CountingTraceBackgroundDrainWaker waker;
    modern_io::TraceBackgroundDrainController background(signal, pump, waker);
    modern_io::TraceLogger logger(queue, background);
    const auto context = make_trace_context();

    fan_in.add_logger(logger);

    ASSERT_TRUE(logger.try_log_json(context, "accept", "client-a").has_value());
    ASSERT_TRUE(logger.try_log_json(context, "read", "payload-a").has_value());
    ASSERT_TRUE(logger.try_log_json(context, "write", "payload-b").has_value());
    EXPECT_EQ(waker.wake_count, 1u);

    std::ostringstream oss;
    modern_io::OstreamOutputStream sink(oss);
    const auto first = background.drain_once(sink, 2);
    EXPECT_EQ(first.drained_records, 2u);
    EXPECT_TRUE(first.should_reschedule());
    EXPECT_TRUE(background.drain_requested());
    EXPECT_TRUE(background.background_drain_armed());
    EXPECT_EQ(waker.wake_count, 2u);

    const auto second = background.drain_once(sink, 2);
    EXPECT_EQ(second.drained_records, 1u);
    EXPECT_FALSE(second.should_reschedule());
    EXPECT_FALSE(background.drain_requested());
    EXPECT_FALSE(background.background_drain_armed());
    EXPECT_EQ(waker.wake_count, 2u);

    EXPECT_EQ(
        oss.str(),
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"accept\",\"message\":\"client-a\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"read\",\"message\":\"payload-a\"}\n"
        "{\"traceparent\":\"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\",\"name\":\"write\",\"message\":\"payload-b\"}\n");
}