#pragma once

#include "io/promise.h"
#include "ring_executor.h"

namespace rdss::detail {

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

template<typename FuncType>
Task<void> ScheduleOn(io_uring* src_ring, RingExecutor* dest_exr, FuncType func) {
    co_await detail::Awaitable{.ring = src_ring, .to_fd = dest_exr->RingFD(), .submit = true};
    func();
}

void SetupInitBufRing(io_uring* src_ring, std::vector<std::unique_ptr<RingExecutor>>& exrs);

} // namespace rdss
