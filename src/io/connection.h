#pragma once

#include "base/buffer.h"
#include "runtime/ring_executor.h"
#include "sys/system_error.h"

namespace rdss::detail {

// TODO: Remove this.
template<typename Impl>
struct RingIO : public RingOperation<RingIO<Impl>> {
    RingIO(RingExecutor* executor, bool use_direct_fd)
      : RingOperation<RingIO<Impl>>(executor, use_direct_fd) {}

    bool IsIoOperation() const { return true; }

    void Prepare(io_uring_sqe* sqe) { static_cast<Impl*>(this)->Prepare(sqe); }

    auto await_resume() -> std::pair<std::error_code, size_t> {
        if (this->result >= 0) {
            return {{}, this->result};
        }
        return {ErrnoToErrorCode(-this->result), 0};
    }
};

struct Recv : public RingIO<Recv> {
    Recv(
      RingExecutor* executor,
      bool use_direct_fd,
      int fd,                          /* fd index if using direct fd, true fd othersise */
      std::optional<int> buffer_group, /* has value means using buffer ring, nullopt otherwise */
      Buffer* buffer)
      : RingIO(executor, use_direct_fd)
      , fd(fd)
      , buffer_group(buffer_group)
      , buffer(buffer) {}

    void Prepare(io_uring_sqe* sqe) {
        if (!UseRingBuf()) {
            const auto sink = buffer->Sink();
            io_uring_prep_recv(sqe, fd, sink.data(), sink.size(), 0);
        } else {
            io_uring_prep_recv(sqe, fd, nullptr, 0, 0);
            sqe->buf_group = buffer_group.value();
            io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
        }
        sqe->ioprio |= IORING_RECVSEND_POLL_FIRST;
    }

    auto await_resume() -> std::pair<std::error_code, RingExecutor::BufferView> {
        if (result == 0) {
            return {};
        }
        if (result < 0) {
            // TODO: Replenish if out of buffer.
            // if (UseRingBuf() && !(flags & IORING_CQE_F_BUFFER)) {}
            return {ErrnoToErrorCode(-result), {}};
        }

        if (UseRingBuf()) {
            const uint32_t entry_id = flags >> IORING_CQE_BUFFER_SHIFT;
            auto v = GetExecutor()->GetBufferView(entry_id, static_cast<size_t>(result));
            buffer->Produce(v.view);
            return {{}, v};
        }
        buffer->Produce(result);
        auto output = buffer->Source();
        return {{}, RingExecutor::BufferView{.view = output}};
    }

    bool UseRingBuf() const { return buffer_group != std::nullopt; }

    bool use_direct_fd;
    int fd;
    std::optional<int> buffer_group;
    Buffer* buffer;
};

} // namespace rdss::detail

namespace rdss {

class Connection {
public:
    explicit Connection(int fd)
      : fd_(fd) {}

    Connection(int fd, RingExecutor* executor)
      : fd_(fd)
      , executor_(executor) {}

    ~Connection() {
        Close();
        if (descripor_index_ >= 0) {
            executor_->UnregisterFd(descripor_index_);
        }
    }

    /// Sets the connection to use 'executor' as the executor to do I/O, and to use buffer ring if
    /// 'use_ring_buffer' is true, registers this connection's socket fd to the 'executor_'.
    /// Note: This function should be run inside 'executor_' to avoid data racing.
    void Setup(RingExecutor* executor, bool use_ring_buffer) {
        executor_ = executor;
        SetUseRingBuf(use_ring_buffer);
        if (!UsingDirectDescriptor()) {
            TryRegisterFD();
        }
    }

    bool UsingDirectDescriptor() const { return descripor_index_ >= 0; }

    bool UseRingBuf() const { return use_ring_buf_; }

    // Registration should be executed in the executor's thread.
    void TryRegisterFD() {
        assert(!UsingDirectDescriptor());
        descripor_index_ = executor_->RegisterFd(fd_);
    }

    /// Buffer ring agnostic recv. Takes 'buffer' to fill the received data, returns [error,
    /// buffer_view]. If 'this' uses ring buffer by setting 'SetUseRingBuf', performs buffer ring
    /// based recv: now 'buffer' should be 'virtual_view'.
    auto Recv(Buffer* buffer) {
        return detail::Recv(
          executor_,
          // TODO: UseDirectFD()
          descripor_index_ != -1,
          (descripor_index_ == -1 ? fd_ : descripor_index_),
          buffer_group_,
          buffer);
    }

    // TODO: Remove this after echo_server can make use of Connection::Setup
    auto Recv(Buffer::SinkType buffer) {
        struct RingRecv : public detail::RingIO<RingRecv> {
            RingRecv(RingExecutor* executor, bool direct_fd, int fd, Buffer::SinkType buffer)
              : RingIO<RingRecv>(executor, direct_fd)
              , fd(fd)
              , buffer(buffer) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_recv(sqe, fd, buffer.data(), buffer.size(), 0);
                sqe->ioprio |= IORING_RECVSEND_POLL_FIRST;
            }

            int fd;
            Buffer::SinkType buffer;
        };
        return (
          (descripor_index_ >= 0) ? RingRecv(executor_, true, descripor_index_, buffer)
                                  : RingRecv(executor_, false, fd_, buffer));
    }

    // TODO: Remove this after echo_server can make use of Connection::Setup
    auto BufRecv(int buf_group_id) {
        struct RingBufferRecv : public detail::RingIO<RingBufferRecv> {
            RingBufferRecv(RingExecutor* executor, bool direct_fd, int fd, int buf_group_id)
              : RingIO<RingBufferRecv>(executor, direct_fd)
              , fd(fd)
              , buf_group_id(buf_group_id) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_recv(sqe, fd, nullptr, 0, 0);
                sqe->buf_group = buf_group_id;
                io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
                sqe->ioprio |= IORING_RECVSEND_POLL_FIRST;
            }

            RingExecutor::BufferView await_resume() {
                if (!(flags & IORING_CQE_F_BUFFER)) {
                    // TODO: Replenish if out of buffer.
                    if (result != 0) {
                        LOG(INFO) << "No buffer selected:" << strerror(-result);
                    }
                    return {};
                }

                const uint32_t entry_id = flags >> IORING_CQE_BUFFER_SHIFT;
                return GetExecutor()->GetBufferView(entry_id, static_cast<size_t>(result));
            }

            int fd;
            int buf_group_id;
        };

        return (
          (descripor_index_ >= 0) ? RingBufferRecv(executor_, true, descripor_index_, buf_group_id)
                                  : RingBufferRecv(executor_, false, fd_, buf_group_id));
    };

    auto Send(std::string_view data) {
        struct RingSend : public detail::RingIO<RingSend> {
            RingSend(RingExecutor* executor, bool direct_fd, int fd, std::string_view data)
              : RingIO<RingSend>(executor, direct_fd)
              , fd(fd)
              , data(data) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_send(sqe, fd, data.data(), data.size(), 0);
            }

            int fd;
            std::string_view data;
        };
        return (
          (descripor_index_ >= 0) ? RingSend(executor_, true, descripor_index_, data)
                                  : RingSend(executor_, false, fd_, data));
    }

    auto Writev(std::span<iovec> iovecs) {
        struct RingWritev : public detail::RingIO<RingWritev> {
            RingWritev(RingExecutor* executor, bool direct_fd, int fd, std::span<iovec> iovecs)
              : RingIO<RingWritev>(executor, direct_fd)
              , fd(fd)
              , iovecs(iovecs) {}

            void Prepare(io_uring_sqe* sqe) {
                io_uring_prep_writev(sqe, fd, iovecs.data(), iovecs.size(), 0);
            }

            int fd;
            std::span<iovec> iovecs;
        };
        return (
          (descripor_index_ >= 0) ? RingWritev(executor_, true, descripor_index_, iovecs)
                                  : RingWritev(executor_, false, fd_, iovecs));
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

    void PutBufferView(RingExecutor::BufferView&& view) {
        if (use_ring_buf_) {
            executor_->PutBufferView(std::move(view));
        }
    }

private:
    void SetUseRingBuf(bool use) {
        use_ring_buf_ = use;
        if (use) {
            buffer_group_ = 0;
        } else {
            buffer_group_ = std::nullopt;
        }
    }

    bool active_ = true;
    int fd_;
    RingExecutor* executor_;
    // Index into 'executor_'s registered fds. Equals -1 if unregistered.
    int descripor_index_ = -1;
    bool use_ring_buf_ = false;
    std::optional<int> buffer_group_;
};

} // namespace rdss
