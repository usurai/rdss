#pragma once

#include "connection.h"

#include <memory>

namespace rdss {

class Listener {
public:
    static std::unique_ptr<Listener> Create(int port, RingExecutor* executor);

    /// Accepts and creates 'Connection' with its 'executor_' set as 'client_executor'.
    auto Accept(RingExecutor* conn_executor) {
        struct RingAccept : public RingOperation<RingAccept> {
            RingAccept(RingExecutor* executor, int fd, RingExecutor* client_executor)
              : RingOperation<RingAccept>(executor)
              , executor(executor)
              , fd(fd)
              , client_executor(client_executor) {}

            void Prepare(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0); }

            bool IsIoOperation() const { return false; }

            auto await_resume() {
                if (result > 0) {
                    return new Connection(result, client_executor);
                }
                LOG(FATAL) << "io_uring accept: " << strerror(-result);
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
