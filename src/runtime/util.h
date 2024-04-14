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

} // namespace rdss::detail

namespace rdss {

/// Sets up 'exrs' to init buffer rings. Blocking waits all of them to finish setup before return.
void SetupInitBufRing(std::vector<std::unique_ptr<RingExecutor>>& exrs);

inline auto WaitFor(RingExecutor* exr, std::chrono::nanoseconds duration) {
    return detail::Timeout(exr, duration);
}

/// Returns an awaitable that suspends the execution of the current coroutine and resumes it on the
/// specified executor, denoted as 'exr'. Internally, it leverages RingTransfer for this purpose. If
/// the current execution is already on the specified executor ('exr'), the execution will not be
/// suspended.
/// Note: This must be invoked on the thread with either 'tls_ring' or 'tls_exr' set.
detail::RingTransfer ResumeOn(RingExecutor* exr);

} // namespace rdss
