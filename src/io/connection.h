#pragma once

#include "base/buffer.h"
#include "io/async_operation.h"
#include "io/async_operation_processor.h"
#include "io/cancellation.h"
#include "io/promise.h"

#include <glog/logging.h>

#include <string>

namespace rdss {

class AwaitableRecv;
class AwaitableCancellableRecv;
class AwaitableSend;
class AwaitableCancellableSend;
// TODO
// class AwaitableClose;

class Connection {
public:
    Connection(int fd, AsyncOperationProcessor* processor)
      : fd_(fd)
      , processor_(processor) {
        VLOG(1) << "New connection at fd:" << fd_;
    }

    ~Connection();

    bool Active() const { return active_; }

    int GetFD() const { return fd_; }

    AwaitableRecv Recv(Buffer::SinkType buffer);
    AwaitableCancellableRecv CancellableRecv(Buffer::SinkType buffer, CancellationToken* token);
    AwaitableSend Send(std::string);
    AwaitableCancellableSend CancellableSend(std::string data, CancellationToken* token);
    void Close();
    // AwaitableClose Close();

private:
    bool active_ = true;
    int fd_;
    AsyncOperationProcessor* processor_;
};

} // namespace rdss
