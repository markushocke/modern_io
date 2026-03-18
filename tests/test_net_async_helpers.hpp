#pragma once

#include "tests/test_net_helpers.hpp"

import net_io.task;

namespace test_helpers {

template<typename Awaitable>
auto run_awaitable(Awaitable awaitable) -> net_io::Task<decltype(awaitable.await_resume())> {
    co_return co_await awaitable;
}

} // namespace test_helpers