// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include "runtime/ring_operation.h"

namespace rdss {

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

} // namespace rdss
