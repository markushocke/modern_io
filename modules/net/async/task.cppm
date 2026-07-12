module;

export module net_io.task;

import modern.runtime;

export namespace modern::net
{
template<typename T>
using Task = modern::task<T>;

template<typename T>
using IoTask = Task<T>;
} // namespace modern::net

export namespace net_io
{
template<typename T>
using Task = modern::net::Task<T>;

template<typename T>
using IoTask = modern::net::IoTask<T>;
} // namespace net_io
