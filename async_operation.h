#pragma once

#include "async_operation_processor.h"
#include "buffer.h"
#include "connection.h"

#include <glog/logging.h>

#include <coroutine>
#include <functional>
#include <liburing.h>
#include <memory>
#include <string>

namespace rdss {

class AsyncOperationProcessor;
class Connection;

class Promise {
public:
    void Set(auto result) {
        result_ = result;
        continuation_();
    }

    decltype(auto) GetResult() { return result_; }

    void SetContinuation(std::coroutine_handle<> continuation) {
        continuation_ = std::move(continuation);
    }

private:
    std::coroutine_handle<> continuation_;
    int result_;
};

template<typename Implementation>
class AwaitableOperation {
public:
    explicit AwaitableOperation(AsyncOperationProcessor* processor)
      : processor_(processor) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> continuation) noexcept {
        promise_.SetContinuation(std::move(continuation));
        processor_->Execute(this);
    }

    void PrepareSqe(io_uring_sqe* sqe) {
        Impl()->PrepareSqe(sqe);
        io_uring_sqe_set_data(sqe, &promise_);
    }

    auto await_resume() noexcept { return promise_.GetResult(); }

    AsyncOperationProcessor* GetProcessor() { return processor_; }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }

    AsyncOperationProcessor* processor_;
    Promise promise_;
};

class AwaitableAccept : public AwaitableOperation<AwaitableAccept> {
public:
    AwaitableAccept(AsyncOperationProcessor* processor, int sockfd)
      : AwaitableOperation<AwaitableAccept>(processor)
      , sockfd_(sockfd) {}

    void PrepareSqe(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, sockfd_, nullptr, nullptr, 0); }

    Connection* await_resume() noexcept;

private:
    int sockfd_;
};

class AwaitableRecv : public AwaitableOperation<AwaitableRecv> {
public:
    AwaitableRecv(AsyncOperationProcessor* processor, int fd, Buffer::SinkType buffer)
      : AwaitableOperation<AwaitableRecv>(processor)
      , fd_(fd)
      , buffer_(std::move(buffer)) {}

    void PrepareSqe(io_uring_sqe* sqe) {
        io_uring_prep_recv(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

private:
    int fd_;
    Buffer::SinkType buffer_;
};

class AwaitableSend : public AwaitableOperation<AwaitableSend> {
public:
    AwaitableSend(AsyncOperationProcessor* processor, int fd, std::string data)
      : AwaitableOperation<AwaitableSend>(processor)
      , fd_(fd)
      , data_(std::move(data)) {}

    void PrepareSqe(io_uring_sqe* sqe) {
        io_uring_prep_send(sqe, fd_, data_.data(), data_.size(), 0);
    }

private:
    int fd_;
    std::string data_;
};

} // namespace rdss
