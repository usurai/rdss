#pragma once

#include <glog/logging.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstring>
#include <liburing.h>
#include <optional>
#include <thread>

namespace rdss {

struct Continuation {
    uint32_t flags;
    int result;
    std::coroutine_handle<> handle;
};

class RingExecutor;

template<typename Implementation>
struct RingOperation
  : public Continuation
  , public std::suspend_always {
    explicit RingOperation(RingExecutor* executor, bool use_direct_fd = false)
      : executor_(executor)
      , use_direct_fd_(use_direct_fd) {}

    void await_suspend(std::coroutine_handle<> h);

    auto await_resume() { return result; }

    void Prepare(io_uring_sqe* sqe) {
        Impl()->Prepare(sqe);
        if (use_direct_fd_) {
            sqe->flags |= IOSQE_FIXED_FILE;
        }
    }

    bool IsIoOperation() const { return Impl()->IsIoOperation(); }

    RingExecutor* GetExecutor() { return executor_; }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }

    const Implementation* Impl() const { return static_cast<const Implementation*>(this); }

    RingExecutor* executor_;
    const bool use_direct_fd_ = false;
};

struct RingTimeout : public RingOperation<RingTimeout> {
    // TODO: Support generalized duration.
    RingTimeout(RingExecutor* executor, std::chrono::nanoseconds nanoseconds)
      : RingOperation<RingTimeout>(executor)
      , ts_{.tv_sec = 0, .tv_nsec = nanoseconds.count()} {}

    void Prepare(io_uring_sqe* sqe) { io_uring_prep_timeout(sqe, &ts_, 0, 0); }

    bool IsIoOperation() const { return false; }

    void await_resume() {
        if (result != -ETIME && result != 0) {
            LOG(FATAL) << "io_uring timeout: " << strerror(-result);
        }
    }

    __kernel_timespec ts_;
};

struct RingConfig {
    size_t sq_entries = 4096;
    size_t cq_entries = 4096 * 16;
    bool sqpoll = false;
    bool async_sqe = false;
    uint32_t max_unbound_workers = 5;
    size_t submit_batch_size = 32;
    size_t wait_batch_size = 1;
    size_t max_direct_descriptors = 4096;
    bool register_ring_fd = true;
};

class RingExecutor {
public:
    RingExecutor(std::string name = "", RingConfig config = RingConfig{}, size_t cpu = 0);

    RingExecutor(const RingExecutor&) = delete;
    RingExecutor(RingExecutor&&) = delete;
    RingExecutor& operator=(const RingExecutor&) = delete;
    RingExecutor& operator=(RingExecutor&&) = delete;

    ~RingExecutor();

    io_uring* Ring() { return &ring_; }
    int RingFD() const { return fd_; }

    /// To stop the executor, one needs to first call 'Deactivate()', which sets 'active_' flag of
    /// executor to false and then sends a ring msg with user_data set as 0 to wake the worker
    /// thread of the executor. This will terminate the worker thread. After that, one needs to call
    /// 'Shutdown()' to blocking wait for the worker thread to terminate. Finally, call the
    /// destructor implicitly to exit the io_uring.
    /// The caller can provide the io_uring for this function to send the ring message, or make the
    /// function to create the ring itself by passing nullptr.
    void Deactivate(io_uring* ring = nullptr);

    void Shutdown();

    template<typename Operation>
    void Initiate(Operation* operation);

    auto Timeout(std::chrono::nanoseconds nanoseconds) { return RingTimeout(this, nanoseconds); }

    bool AsyncSqe() const { return config_.async_sqe; }

    // Registers 'fd' into the executor. Returns the index into the registered fd if successful,
    // returns -1 otherwise.
    int RegisterFd(int fd);

    void UnregisterFd(int fd_slot_index);

    void InitBufRing();

    // BufferView is the view of the recv result stored in the ring buffer. If the referenced buffer
    // is consecutive, 'view' is simply the string_view over that buffer. If the referenced buffer
    // is non-consecutive, 'RingExecutor' creates a temporary string to and copies the two parts
    // into the string, then 'view' is string_view over the temporary string.
    struct BufferView {
        std::string_view view;

        // If the referenced buffer are non-adjacent, i.e. one at the last and one at the first,
        // then 'separated_entry_id' is the first entry id of the entry at the last part of the
        // buffer. nullopt otherwise.:w
        std::optional<uint32_t> separated_entry_id = std::nullopt;
    };

    BufferView GetBufferView(uint32_t entry_id, size_t length);

    void PutBufferView(BufferView&& buffer_view);

private:
    void EventLoop();

    const std::string name_;
    const RingConfig config_;
    std::atomic<bool> active_ = true;
    io_uring ring_;
    int fd_;
    std::thread thread_;
    std::vector<int> fd_slot_indices_;

    io_uring_buf_ring* buf_ring_{nullptr};
    char* buf_{nullptr};
    // TODO: 1. move somewhere 2. rename to kXXX
    static constexpr size_t buf_entry_size = 2048 * 2;
    size_t buf_entries_{0};
    std::string concat_str_;
};

template<typename Operation>
void RingExecutor::Initiate(Operation* operation) {
    io_uring_sqe* sqe{nullptr};
    while ((sqe = io_uring_get_sqe(Ring())) == nullptr) {
        if (config_.sqpoll) {
            auto ret = io_uring_sqring_wait(Ring());
            if (ret < 0) {
                LOG(FATAL) << "io_uring_sqring_wait:" << strerror(-ret);
            }
        } else {
            io_uring_submit(Ring());
        }
    }
    operation->Prepare(sqe);
    if (AsyncSqe() && operation->IsIoOperation()) {
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    }
    io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(operation));
}

template<typename Implementation>
void RingOperation<Implementation>::await_suspend(std::coroutine_handle<> h) {
    handle = std::move(h);
    executor_->Initiate(this);
}

/// Transfers the execution of the calling coroutine from executor 'from' to 'to'. Underlying, a
/// ring message with the continuation of the calling coroutine set as user_data is sent from ring
/// of 'from' to the ring of 'to'.
inline auto Transfer(RingExecutor* src, RingExecutor* dest) {
    struct RingTransfer : public RingOperation<RingTransfer> {
        RingTransfer(RingExecutor* src, RingExecutor* dest)
          : RingOperation<RingTransfer>(src)
          , dest(dest) {}

        void Prepare(io_uring_sqe* sqe) {
            io_uring_prep_msg_ring(sqe, dest->RingFD(), 0, reinterpret_cast<uint64_t>(this), 0);
            io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
        }

        bool IsIoOperation() const { return false; }

        RingExecutor* dest;
    };
    return RingTransfer(src, dest);
}

/// Resumes the execution from non-executor thread to executor 're'. This function should be called
/// before 're' gettings active because underlying this works by submitting a nop with user_data set
/// as Continuation of the coroutine. If 're' is active, data race happens.
inline auto ResumeOn(RingExecutor* re) {
    struct RingResume : public RingOperation<RingResume> {
        RingResume(RingExecutor* re)
          : RingOperation<RingResume>(re) {}

        void Prepare(io_uring_sqe* sqe) { io_uring_prep_nop(sqe); }

        bool IsIoOperation() const { return false; }

        RingExecutor* re;
    };
    return RingResume(re);
}

} // namespace rdss
