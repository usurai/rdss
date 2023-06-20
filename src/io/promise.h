#pragma once

#include <coroutine>

namespace rdss {

// Temporary Task/Promise struct as interface of coroutine/non-coroutine function.
template<typename T>
struct Task;

template<typename T>
struct TaskPromise {
    Task<T> get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() {}
    void return_void() {}
};

template<typename T>
struct Task {
    using promise_type = TaskPromise<T>;
};

} // namespace rdss
