#pragma once

#include "base/buffer.h"
#include "runtime/ring_executor.h"

namespace rdss {

class Connection {
public:
    Connection(int fd, RingExecutor* executor)
      : fd_(fd)
      , executor_(executor) {}

    ~Connection() { Close(); }

    auto Recv(Buffer::SinkType buffer) {
        struct RingRecv : public RingOperation<RingRecv> {
            RingRecv(RingExecutor* executor, int fd, Buffer::SinkType buffer)
              : RingOperation<RingRecv>(executor)
              , fd(fd)
              , buffer(buffer) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_recv(sqe, fd, buffer.data(), buffer.size(), 0);
            }

            bool IsIoOperation() const { return true; }

            int fd;
            Buffer::SinkType buffer;
        };
        return RingRecv(executor_, fd_, buffer);
    }

    auto Send(std::string_view data) {
        struct RingSend : public RingOperation<RingSend> {
            RingSend(RingExecutor* executor, int fd, std::string_view data)
              : RingOperation<RingSend>(executor)
              , fd(fd)
              , data(data) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_send(sqe, fd, data.data(), data.size(), 0);
            }

            bool IsIoOperation() const { return true; }

            int fd;
            std::string_view data;
        };
        return RingSend(executor_, fd_, data);
    }

    auto Writev(std::span<iovec> iovecs) {
        struct RingWritev : public RingOperation<RingWritev> {
            RingWritev(RingExecutor* executor, int fd, std::span<iovec> iovecs)
              : RingOperation<RingWritev>(executor)
              , fd(fd)
              , iovecs(iovecs) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_writev(sqe, fd, iovecs.data(), iovecs.size(), 0);
            }

            bool IsIoOperation() const { return true; }

            int fd;
            std::span<iovec> iovecs;
        };
        return RingWritev(executor_, fd_, iovecs);
    }

    void Close() {
        if (!active_) {
            return;
        }
        close(fd_);
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
