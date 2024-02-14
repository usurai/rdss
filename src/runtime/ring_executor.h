#pragma once

#include <glog/logging.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstring>
#include <liburing.h>
#include <thread>

namespace rdss {

struct Continuation {
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
            io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
        }
    }

    bool IsIoOperation() const { return Impl()->IsIoOperation(); }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }

    const Implementation* Impl() const { return static_cast<const Implementation*>(this); }

    RingExecutor* executor_;
    bool use_direct_fd_;
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
    size_t wait_batch_size = 1;
    size_t max_direct_descriptors = 4096;
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

private:
    void LoopNew();
    void Loop();
    void LoopTimeoutWait();

    const std::string name_;
    const RingConfig config_;
    std::atomic<bool> active_ = true;
    io_uring ring_;
    std::thread thread_;
    std::vector<int> fd_slot_indices_;
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
            io_uring_prep_msg_ring(
              sqe, dest->Ring()->enter_ring_fd, 0, reinterpret_cast<uint64_t>(this), 0);
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
