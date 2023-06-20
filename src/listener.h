#pragma once

#include "io/async_operation.h"
#include "io/async_operation_processor.h"

namespace rdss {

class Listener {
public:
    AwaitableAccept Accept();

    AwaitableCancellableAccept CancellableAccept(CancellationToken* token);

    static std::unique_ptr<Listener> Create(int port, AsyncOperationProcessor* processor);

private:
    Listener(int listen_fd, AsyncOperationProcessor* processor);

    int listened_fd_;
    AsyncOperationProcessor* processor_;
};

} // namespace rdss
