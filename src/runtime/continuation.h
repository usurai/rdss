#pragma once

#include <coroutine>
#include <cstdint>

namespace rdss {

struct Continuation {
    uint32_t flags;
    int result;
    std::coroutine_handle<> handle;
};

} // namespace rdss
