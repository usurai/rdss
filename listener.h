#pragma once

#include "async_operation.h"
#include "async_operation_processor.h"

namespace rdss {

class Listener {
public:
    AcceptOperation Accept();

    static std::unique_ptr<Listener> Create(int port, AsyncOperationProcessor* processor);

private:
    Listener(int listen_fd, AsyncOperationProcessor* processor);

    int listened_fd_;
    AsyncOperationProcessor* processor_;
};

} // namespace rdss
