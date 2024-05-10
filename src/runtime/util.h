// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include "runtime/ring_operation.h"
#include "runtime/timeout.h"

namespace rdss {

/// Sets up 'exrs' to init buffer rings. Blocking waits all of them to finish setup before return.
void SetupInitBufRing(std::vector<std::unique_ptr<RingExecutor>>& exrs);

inline auto WaitFor(RingExecutor* exr, std::chrono::nanoseconds duration) {
    return Timeout(exr, duration);
}

/// Returns an awaitable that suspends the execution of the current coroutine and resumes it on the
/// specified executor, denoted as 'exr'. Internally, it leverages RingTransfer for this purpose. If
/// the current execution is already on the specified executor ('exr'), the execution will not be
/// suspended. If 'submit' is set, it will force the sending ring to submit the SQE, this is for
/// non-executor ring. For normal executors whoes worker thread is submitting the queue timely, this
/// might reduce the batching.
/// Note: This must be invoked on the thread with either 'tls_ring' or 'tls_exr' set.
detail::RingTransfer ResumeOn(RingExecutor* exr, bool submit = false);

} // namespace rdss
