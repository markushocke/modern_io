module;

#ifndef _MSC_VER
#include <cstddef>
#else
export import <cstddef>;
#endif

export module modern_io.trace;

import modern.trace;
import modern_io.connection_arena;

export namespace modern::io {

using modern::trace::format_traceparent;
using modern::trace::parse_traceparent;
using modern::trace::TraceBackgroundDrainController;
using modern::trace::TraceBackgroundDrainWaker;
using modern::trace::TraceContext;
using modern::trace::TraceContextParseError;
using modern::trace::TraceDrainMode;
using modern::trace::TraceDrainPump;
using modern::trace::TraceDrainSignal;
using modern::trace::TraceDrainState;
using modern::trace::TraceDrainStepResult;
using modern::trace::TraceLogger;
using modern::trace::TraceOutputSink;
using modern::trace::TraceQueueError;

class InMemoryTraceBuffer : public modern::trace::InMemoryTraceBuffer {
public:
    using modern::trace::InMemoryTraceBuffer::InMemoryTraceBuffer;

    explicit InMemoryTraceBuffer(ConnectionArena& arena)
        : modern::trace::InMemoryTraceBuffer(arena.memory_resource()) {}
};

class SpscTraceDrainQueue : public modern::trace::SpscTraceDrainQueue {
public:
    using modern::trace::SpscTraceDrainQueue::SpscTraceDrainQueue;

    SpscTraceDrainQueue(std::size_t capacity, std::size_t max_record_bytes, ConnectionArena& arena)
        : modern::trace::SpscTraceDrainQueue(capacity, max_record_bytes, arena.memory_resource()) {}
};

class TraceLoggerFanIn : public modern::trace::TraceLoggerFanIn {
public:
    using modern::trace::TraceLoggerFanIn::TraceLoggerFanIn;

    explicit TraceLoggerFanIn(ConnectionArena& arena)
        : modern::trace::TraceLoggerFanIn(arena.memory_resource()) {}
};

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
using modern::io::TraceOutputSink;
using modern::io::TraceQueueError;
} // namespace modern_io