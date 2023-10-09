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
    explicit RingOperation(RingExecutor* executor)
      : executor_(executor) {}

    void await_suspend(std::coroutine_handle<> h);

    auto await_resume() { return result; }

    void Prepare(io_uring_sqe* sqe) { Impl()->Prepare(sqe); }

    bool IsIoOperation() const { return Impl()->IsIoOperation(); }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }

    const Implementation* Impl() const { return static_cast<const Implementation*>(this); }

    RingExecutor* executor_;
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
    bool sqpoll = true;
    bool async_sqe = true;
    uint32_t max_unbound_workers = 5;
    size_t submit_batch_size = 10;
};

class RingExecutor {
public:
    RingExecutor(std::string name = "", RingConfig config = RingConfig{});

    io_uring* Ring() { return &ring_; }

    void Shutdown();

    template<typename Operation>
    void Initiate(Operation* operation);

    auto Timeout(std::chrono::nanoseconds nanoseconds) { return RingTimeout(this, nanoseconds); }

    bool AsyncSqe() const { return config_.async_sqe; }

private:
    void Loop();
    void LoopTimeoutWait();

    void MaybeSubmit();

    const std::string name_;
    const RingConfig config_;
    std::atomic<bool> active_ = true;
    io_uring ring_;
    std::thread thread_;
};

template<typename Operation>
void RingExecutor::Initiate(Operation* operation) {
    auto sqe = io_uring_get_sqe(Ring());
    if (sqe == nullptr) {
        LOG(FATAL) << "io_uring_sqe";
    }
    operation->Prepare(sqe);
    if (AsyncSqe() && operation->IsIoOperation()) {
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    }
    io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(operation));
    MaybeSubmit();
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
