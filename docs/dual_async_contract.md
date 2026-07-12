# Async Contract

This document is normative for the dual async architecture in `modern_io` + `modern_runtime`.

## 1. Scope and Goal

The project uses one task core with two usage styles:

- `co_await` style
- fluent style (`then`, `catching`, `finally`)

The canonical core remains:

- `modern::task<T>`
- `modern::result_task<T, E>`

`modern::io::ExpectedTask<T>` and `modern::net::Task<T>` stay domain aliases over the runtime core.

## 2. Hard Decisions

### 2.1 `then()` Implementation Model

`then()` is coroutine sugar over the runtime task core.

It is not a second independent continuation-graph runtime.

Design consequence:

- one state-machine model
- one cancellation model
- one scheduler model
- one lifetime model

### 2.2 Start Semantics

Tasks start when constructed.

Activation points consume or observe already-created task state:

- `co_await t`
- fluent activation (`then` chain binding)
- `get()`
- explicit `start()` as a compatibility no-op

Task construction captures the current task environment.

### 2.3 Unified Task Surface

`task<T>` and `result_task<T, E>` use the same runtime task type.

- `task<T>`: exception-channel operators (`then`, `catching`, `finally`)
- `result_task<T, E>`: alias for `task<std::expected<T, E>>`; expected-channel handling is explicit inside `then`

No silent conversion or channel loss is allowed.

## 3. Contract Domains

### 3.1 Cancellation

Define behavior for:

1. cancel before start
2. cancel during suspend/await
3. cancel after completion

Define fluent behavior when parent is canceled:

- continuation skipped
- continuation canceled callback path
- or continuation receives explicit cancel token

Chosen behavior must be documented per operator.

### 3.2 Lifetime and Ownership

Fluent operators are consuming (`&&`) unless explicitly documented otherwise.

Completion must be idempotent under races.

Awaiter owner tokens for reactor registrations must remain valid until completion or cancellation handoff.

### 3.3 Scheduling

Default scheduler inheritance and explicit `then_on(...)` override must be deterministic.

`EventReactor` integration must not rely on hidden singleton behavior for new code paths.

### 3.4 Error Propagation

Bridge layers (`bind_io`, `as_task`, sender bridge) must define explicit conversions:

- exception -> expected
- expected -> exception

No silent conversion or channel loss is allowed.

### 3.5 Fluent Surface

Fluent operators must preserve the same cancellation, lifetime, scheduling, and error contracts as `co_await` usage.

## 4. Stream Model (First-Class)

The target model includes stream semantics in the same architecture:

- `Task<T>`: 0..1 result
- `Stream<T>`: 0..N results

`Stream<T>` must align with the same cancellation/scheduling/error rules and support coroutine-first consumption (`for co_await (...)`).

## 5. Non-Goals

- No permanent second public task core.
- No reintroduction of two independent cancellation models.
- No API additions that force users to mix expected and exception channels implicitly.

## 6. Conformance Criteria

A change is contract-compliant only if it provides:

1. tests for cancellation behavior (before start, during suspend/await, after completion)
2. tests for eager start semantics and environment capture
3. tests for unified fluent operators on `task` and `result_task`
4. explicit bridge conversion tests for error channels
5. documentation updates when public semantics change
