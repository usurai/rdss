// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include "runtime/ring_executor.h"

namespace rdss {

/// RingOperation allows to execute an async operation while the code is like blocking-style, with
/// the help of coroutine and io_uring. The execution is splitted into two parts:
/// 1. Suspension & Submission
/// - A certain RingOperation is co_await-ed, the Operation gets constructed, returns false from
/// await_ready(), which means the coroutine should be suspended.
/// - await_suspend() gets called, captures the coroutine_handle in Continuation, calls
/// executor.Initiate() to initiate 'this' against executor's ring.
/// - executor gets SQE from ring, calls this.Prepare() to prepare the SQE, Prepare() is
/// Implementation-dependent.
/// - SQE is then submitted, the execution of coroutine suspends.
/// 2. Completion & Resumption
/// - Once the operation specified in SQE completes, the corresponding CQE is generated. executor
/// reaps the CQE and fills result and flags into Continuation.
/// - The suspended coroutine_handle is then invoked, Implementation-dependent await_resume() is
/// called, whose result will be returned to the caller of co_await, the coroutine resumes.
template<typename Implementation>
struct RingOperation
  : public Continuation
  , public std::suspend_always {
    explicit RingOperation(RingExecutor* executor, bool use_direct_fd = false)
      : executor_(executor)
      , use_direct_fd_(use_direct_fd) {}

    void await_suspend(std::coroutine_handle<> h) {
        handle = std::move(h);
        executor_->Initiate(this);
    }

    // TODO: return Impl()->await_resume().
    auto await_resume() { return result; }

    void Prepare(io_uring_sqe* sqe) {
        Impl()->Prepare(sqe);
        if (use_direct_fd_) {
            sqe->flags |= IOSQE_FIXED_FILE;
        }
    }

protected:
    RingExecutor* GetExecutor() { return executor_; }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }

    const Implementation* Impl() const { return static_cast<const Implementation*>(this); }

private:
    RingExecutor* executor_;
    const bool use_direct_fd_ = false;
};

} // namespace rdss
