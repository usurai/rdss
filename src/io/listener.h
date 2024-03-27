#pragma once

#include "connection.h"
#include "sys/system_error.h"

#include <memory>

namespace rdss {

class Listener {
public:
    static std::unique_ptr<Listener> Create(int port, RingExecutor* executor);

    auto Accept() {
        struct RingAccept : public RingOperation<RingAccept> {
            RingAccept(RingExecutor* executor, int fd)
              : RingOperation<RingAccept>(executor)
              , executor(executor)
              , fd(fd) {}

            void Prepare(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0); }

            auto await_resume() -> std::pair<std::error_code, Connection*> {
                if (result > 0) {
                    return {{}, new Connection(result)};
                }
                return {ErrnoToErrorCode(-result), nullptr};
            }

            RingExecutor* executor;
            int fd;
        };
        return RingAccept(executor_, fd_);
    }

private:
    Listener(int listen_fd, RingExecutor* executor);

    int fd_;
    RingExecutor* executor_;
};

} // namespace rdss
