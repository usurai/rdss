// Copyright (c) usurai.
// Licensed under the MIT license.
#include "async_operation.h"

namespace rdss {

Connection* AwaitableAccept::await_resume() noexcept {
    const auto fd = AwaitableOperation<AwaitableAccept>::await_resume();
    return new Connection(fd, GetProcessor());
}

void AwaitableTimeout::await_resume() noexcept {
    const auto result = AwaitableOperation<AwaitableTimeout>::await_resume();
    VLOG(1) << "AwaitableTimeout, result:" << strerror(result);
}

} // namespace rdss
