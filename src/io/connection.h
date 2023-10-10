#pragma once

#include "base/buffer.h"
#include "runtime/ring_executor.h"
#include "sys/system_error.h"

namespace rdss::detail {

template<typename Impl>
struct RingIO : public RingOperation<RingIO<Impl>> {
    RingIO(RingExecutor* executor)
      : RingOperation<RingIO<Impl>>(executor) {}

    bool IsIoOperation() const { return true; }

    void Prepare(io_uring_sqe* sqe) { static_cast<Impl*>(this)->Prepare(sqe); }

    auto await_resume() -> std::pair<std::error_code, size_t> {
        if (this->result >= 0) {
            return {{}, this->result};
        }
        return {ErrnoToErrorCode(-this->result), 0};
    }
};

} // namespace rdss::detail

namespace rdss {

class Connection {
public:
    Connection(int fd, RingExecutor* executor)
      : fd_(fd)
      , executor_(executor) {}

    ~Connection() { Close(); }

    auto Recv(Buffer::SinkType buffer) {
        struct RingRecv : public detail::RingIO<RingRecv> {
            RingRecv(RingExecutor* executor, int fd, Buffer::SinkType buffer)
              : RingIO<RingRecv>(executor)
              , fd(fd)
              , buffer(buffer) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_recv(sqe, fd, buffer.data(), buffer.size(), 0);
            }

            int fd;
            Buffer::SinkType buffer;
        };
        return RingRecv(executor_, fd_, buffer);
    }

    auto Send(std::string_view data) {
        struct RingSend : public detail::RingIO<RingSend> {
            RingSend(RingExecutor* executor, int fd, std::string_view data)
              : RingIO<RingSend>(executor)
              , fd(fd)
              , data(data) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_send(sqe, fd, data.data(), data.size(), 0);
            }

            int fd;
            std::string_view data;
        };
        return RingSend(executor_, fd_, data);
    }

    auto Writev(std::span<iovec> iovecs) {
        struct RingWritev : public detail::RingIO<RingWritev> {
            RingWritev(RingExecutor* executor, int fd, std::span<iovec> iovecs)
              : RingIO<RingWritev>(executor)
              , fd(fd)
              , iovecs(iovecs) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_writev(sqe, fd, iovecs.data(), iovecs.size(), 0);
            }

            int fd;
            std::span<iovec> iovecs;
        };
        return RingWritev(executor_, fd_, iovecs);
    }

    void Close() {
        if (!active_) {
            return;
        }
        if (close(fd_) != 0) {
            LOG(ERROR) << "close: " << strerror(errno);
        }
        active_ = false;
    }

    int GetFD() const { return fd_; }

    RingExecutor* GetExecutor() { return executor_; }

private:
    bool active_ = true;
    int fd_;
    RingExecutor* executor_;
};

} // namespace rdss
