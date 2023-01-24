#pragma once

#include "async_operation_processor.h"

#include <glog/logging.h>

#include <coroutine>
#include <liburing.h>
#include <memory>

namespace rdss {

class AsyncOperationProcessor;

template<typename Operation>
class Awaitable {
public:
    bool await_ready() const noexcept { return false; }

    void await_suspend([[maybe_unused]] std::coroutine_handle<> continuation) noexcept {
        Derived()->Initiate(std::move(continuation));
    }

    auto await_resume() noexcept { return Derived()->GetResult(); }

private:
    Operation* Derived() noexcept { return static_cast<Operation*>(this); }
};

class AcceptOperation : public Awaitable<AcceptOperation> {
public:
    using ResultType = int;

public:
    AcceptOperation(int sockfd, AsyncOperationProcessor* processor)
      : sockfd_(sockfd)
      , processor_(processor) {}

    void Initiate(std::coroutine_handle<> continuation);

    void SetContinuation(std::coroutine_handle<> continuation) {
        continuation_ = std::move(continuation);
    }

    void PrepareSqe(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, sockfd_, nullptr, nullptr, 0); }

    void SetResult(ResultType result) { result_ = std::move(result); }

    ResultType GetResult() const { return result_; }

    void OnCompleted() { continuation_(); }

private:
    int sockfd_;
    AsyncOperationProcessor* processor_;
    std::coroutine_handle<> continuation_;
    ResultType result_;
};

} // namespace rdss
