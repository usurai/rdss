#pragma once

#include "runtime/ring_operation.h"

namespace rdss::detail {

struct Timeout : public RingOperation<Timeout> {
    // TODO: Support generalized duration.
    Timeout(RingExecutor* executor, std::chrono::nanoseconds nanoseconds)
      : RingOperation<Timeout>(executor)
      , ts_{.tv_sec = 0, .tv_nsec = nanoseconds.count()} {}

    void Prepare(io_uring_sqe* sqe) { io_uring_prep_timeout(sqe, &ts_, 0, 0); }

    void await_resume() {
        if (result != -ETIME && result != 0) {
            LOG(FATAL) << "io_uring timeout: " << strerror(-result);
        }
    }

    __kernel_timespec ts_;
};

struct Awaitable
  : public Continuation
  , public std::suspend_always {
    void await_suspend(std::coroutine_handle<> h) {
        handle = std::move(h);
        io_uring_sqe* sqe;
        while ((sqe = io_uring_get_sqe(ring)) == nullptr) {
        }
        io_uring_prep_msg_ring(sqe, to_fd, 0, reinterpret_cast<uint64_t>(this), 0);
        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
        if (submit) {
            auto ret = io_uring_submit(ring);
            if (ret < 0) {
                LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
            }
        }
    }

    io_uring* ring;
    int to_fd;
    bool submit;
};

} // namespace rdss::detail

namespace rdss {

// TODO: Deprecate this after change to EchoServer.
template<typename FuncType>
Task<void> ScheduleOn(io_uring* src_ring, RingExecutor* dest_exr, FuncType func) {
    co_await detail::Awaitable{.ring = src_ring, .to_fd = dest_exr->RingFD(), .submit = true};
    func();
}

/// Sets up 'exrs' to init buffer rings. Blocking waits all of them to finish setup before return.
void SetupInitBufRing(std::vector<std::unique_ptr<RingExecutor>>& exrs);

inline auto WaitFor(RingExecutor* exr, std::chrono::nanoseconds duration) {
    return detail::Timeout(exr, duration);
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

        RingExecutor* re;
    };
    return RingResume(re);
}

} // namespace rdss
