#pragma once

#include "async_operation_processor.h"
#include "buffer.h"

#include <glog/logging.h>

#include <coroutine>
#include <functional>
#include <liburing.h>
#include <memory>

namespace rdss {

class AsyncOperationProcessor;

class ContRes {
public:
    void SetResult(auto result) { result_ = result; }
    decltype(auto) GetResult() { return result_; }
    void OnCompleted() { continuation_(); }

protected:
    void SetContinuation(std::coroutine_handle<> continuation) {
        continuation_ = std::move(continuation);
    }

private:
    std::coroutine_handle<> continuation_;
    int result_;
};

template<typename Implementation>
class AwaitableOperation : public ContRes {
public:
    explicit AwaitableOperation(AsyncOperationProcessor* processor)
      : processor_(processor) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> continuation) noexcept {
        SetContinuation(std::move(continuation));
        processor_->Execute(this);
    }

    auto await_resume() noexcept { return GetResult(); }

    void PrepareSqe(io_uring_sqe* sqe) { Impl()->PrepareSqe(sqe); }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }

    AsyncOperationProcessor* processor_;
};

class AwaitableAccept : public AwaitableOperation<AwaitableAccept> {
public:
    AwaitableAccept(AsyncOperationProcessor* processor, int sockfd)
      : AwaitableOperation(processor)
      , sockfd_(sockfd) {}

    void PrepareSqe(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, sockfd_, nullptr, nullptr, 0); }

private:
    int sockfd_;
};

} // namespace rdss
