#pragma once

#include "connection.h"
#include "sys/system_error.h"

#include <memory>

namespace rdss {

class Listener {
public:
    static std::unique_ptr<Listener> Create(int port, RingExecutor* executor);

    /// Accepts and creates 'Connection' with its 'executor_' set as 'client_executor'.
    /// TODO: Ditch this, defer the executor assignment to Connection::Setup, as Listener doesn't
    /// need to know about it.
    auto Accept(RingExecutor* conn_executor) {
        struct RingAccept : public RingOperation<RingAccept> {
            RingAccept(RingExecutor* executor, int fd, RingExecutor* client_executor)
              : RingOperation<RingAccept>(executor)
              , executor(executor)
              , fd(fd)
              , client_executor(client_executor) {}

            void Prepare(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0); }

            bool IsIoOperation() const { return false; }

            auto await_resume() -> std::pair<std::error_code, Connection*> {
                if (result > 0) {
                    return {{}, new Connection(result, client_executor)};
                }
                return {ErrnoToErrorCode(-result), nullptr};
            }

            RingExecutor* executor;
            int fd;
            RingExecutor* client_executor;
        };
        return RingAccept(executor_, fd_, conn_executor);
    }

private:
    Listener(int listen_fd, RingExecutor* executor);

    int fd_;
    RingExecutor* executor_;
};

} // namespace rdss
