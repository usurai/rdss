#include "async_operation.h"

namespace rdss {

Connection AwaitableAccept::await_resume() noexcept {
    const auto fd = AwaitableOperation<AwaitableAccept>::await_resume();
    return Connection(fd, GetProcessor());
}

} // namespace rdss
