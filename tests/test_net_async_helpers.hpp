#pragma once

#include "tests/test_net_helpers.hpp"

#include <type_traits>
#include <utility>

import net_io.task;

namespace test_helpers {

template<typename Awaitable, typename = void>
struct await_result {
    using type = decltype(std::declval<Awaitable&>().await_resume());
};

template<typename Awaitable>
struct await_result<Awaitable, std::void_t<decltype(std::declval<Awaitable&&>().operator co_await())>> {
    using type = decltype(std::declval<Awaitable&&>().operator co_await().await_resume());
};

template<typename Awaitable>
auto run_awaitable(Awaitable awaitable) -> net_io::Task<typename await_result<Awaitable>::type> {
    co_return co_await std::move(awaitable);
}

} // namespace test_helpers
