#pragma once

#include "async_operation_processor.h"
#include "buffer.h"
#include "cancellation.h"
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
class CancellationToken;

class CompletionHandler {
public:
    CompletionHandler() { VLOG(1) << "CompletionHandler::ctor()"; }
    CompletionHandler(const CompletionHandler&) = delete;
    CompletionHandler& operator=(const CompletionHandler&) = delete;
    CompletionHandler(CompletionHandler&&) = delete;
    CompletionHandler& operator=(CompletionHandler&&) = delete;
    ~CompletionHandler() { VLOG(1) << "CompletionHandler::dtor()"; }

    void SetCallback(std::function<void()> callback) { callback_ = std::move(callback); }

    void Complete(int result) {
        VLOG(1) << "CompletionHandler::Complete(" << result << ')';
        completed = true;
        result_ = result;
        callback_();
    }

    int GetResult() const {
        assert(completed == true);
        return result_;
    }

private:
    bool completed = false;
    int result_;
    std::function<void()> callback_;
};

template<typename Implementation>
class AwaitableOperation {
public:
    explicit AwaitableOperation(AsyncOperationProcessor* processor)
      : processor_(processor) {
        VLOG(1) << "AwaitableOperation::ctor()";
    }
    ~AwaitableOperation() { VLOG(1) << "AwaitableOperation::dtor()"; }

    bool await_ready() const noexcept {
        VLOG(1) << ToString() << "::await_ready()";
        return false;
    }

    void await_suspend(std::coroutine_handle<> continuation) noexcept {
        VLOG(1) << ToString() << "::await_suspend()";
        completion_handler_.SetCallback([c = std::move(continuation)]() {
            VLOG(1) << "CompletionHandler::callback()";
            c();
        });
        processor_->Execute(this);
    }

    auto await_resume() noexcept {
        VLOG(1) << ToString() << "::await_resume(), result:" << completion_handler_.GetResult();
        return completion_handler_.GetResult();
    }

    void PrepareSqe(io_uring_sqe* sqe) {
        Impl()->PrepareSqe(sqe);
        io_uring_sqe_set_data(sqe, &completion_handler_);
    }

    AsyncOperationProcessor* GetProcessor() { return processor_; }

    std::string ToString() const { return Impl()->ToString(); }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }
    const Implementation* Impl() const { return static_cast<const Implementation*>(this); }

    AsyncOperationProcessor* processor_;
    CompletionHandler completion_handler_;
};

template<typename Implementation>
class AwaitableCancellableOperation {
public:
    AwaitableCancellableOperation(
      AsyncOperationProcessor* processor, int fd, CancellationToken* token)
      : processor_(processor)
      , fd_(fd)
      , token_(token) {
        VLOG(1) << "AwaitableCancellableOperation::ctor()";
        if (token_->CancelRequested()) {
            VLOG(1) << "Cancelled.";
            state_ = State::kCancelled;
        } else {
            token_->RegisterCancellationCallback([this]() {
                VLOG(1) << "CancellationHandler::callback()";
                this->OnCancellationRequested();
            });
        }
    }

    virtual ~AwaitableCancellableOperation() { VLOG(1) << "AwaitableCancellableOperation::dtor()"; }

    bool await_ready() {
        VLOG(1) << Impl()->ToString() << "::await_ready()";
        return state_ != State::kNotStarted;
    }

    void await_suspend(std::coroutine_handle<> continuation) noexcept {
        VLOG(1) << Impl()->ToString() << "::await_suspend()";

        state_ = State::kStarted;

        completion_handler_.SetCallback([callback = std::move(continuation)]() { callback(); });
        processor_->Execute(this);
    }

    void PrepareSqe(io_uring_sqe* sqe) {
        VLOG(1) << Impl()->ToString() << "::PrepareSqe()";
        if (state_ == State::kStarted) {
            LOG(INFO) << "Initiating recv SQE.";
            Impl()->PrepareSqe(sqe);
            io_uring_sqe_set_data(sqe, &completion_handler_);
        } else {
            assert(state_ == State::kCancelled);
            LOG(INFO) << "Initiating cancel SQE.";
            io_uring_prep_cancel_fd(sqe, fd_, 0);
            cancel_handler_ = new CompletionHandler;
            cancel_handler_->SetCallback([handler = cancel_handler_]() mutable {
                LOG(INFO) << "cancel_handler_ is called, res:" << strerror(-handler->GetResult());
                delete handler;
                // this->completion_handler_.Complete(0);
            });
            io_uring_sqe_set_data(sqe, cancel_handler_);
        }
    }

    std::pair<bool, size_t> await_resume() noexcept {
        VLOG(1) << Impl()->ToString() << "::await_resume()";
        token_->DeregisterCancellationCallback();
        if (state_ == State::kCancelled) {
            return {true, {}};
        }
        assert(state_ == State::kStarted);
        state_ = State::kCompleted;
        return {false, completion_handler_.GetResult()};
    }

    void OnCancellationRequested() {
        VLOG(1) << "AwaitableCancellableOperation::OnCancellationRequested()";
        assert(state_ == State::kStarted);
        state_ = State::kCancelled;
        processor_->Execute(this);
    }

    std::string ToString() const { return Impl()->ToString(); }

protected:
    int GetFD() const { return fd_; }

private:
    Implementation* Impl() { return static_cast<Implementation*>(this); }
    const Implementation* Impl() const { return static_cast<const Implementation*>(this); }

    enum class State { kNotStarted, kStarted, kCompleted, kCancelled };

    AsyncOperationProcessor* processor_;
    CompletionHandler completion_handler_;
    int fd_;

    State state_ = State::kNotStarted;
    CancellationToken* token_;
    CompletionHandler* cancel_handler_ = nullptr;
};

class AwaitableAccept : public AwaitableOperation<AwaitableAccept> {
public:
    AwaitableAccept(AsyncOperationProcessor* processor, int sockfd)
      : AwaitableOperation<AwaitableAccept>(processor)
      , sockfd_(sockfd) {}

    void PrepareSqe(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, sockfd_, nullptr, nullptr, 0); }

    Connection* await_resume() noexcept;

    std::string ToString() const { return "AwaitableAccept"; }

private:
    int sockfd_;
};

class AwaitableCancellableAccept
  : public AwaitableCancellableOperation<AwaitableCancellableAccept> {
public:
    AwaitableCancellableAccept(AsyncOperationProcessor* processor, int fd, CancellationToken* token)
      : AwaitableCancellableOperation<AwaitableCancellableAccept>(processor, fd, token) {}

    ~AwaitableCancellableAccept() { VLOG(1) << "AwaitableCancellableAccept::dtor()"; }

    void PrepareSqe(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, GetFD(), nullptr, nullptr, 0); }

    std::string ToString() const { return "AwaitableCancellableAccept"; }
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

    std::string ToString() const { return "AwaitableRecv"; }

private:
    int fd_;
    Buffer::SinkType buffer_;
};

class AwaitableCancellableRecv : public AwaitableCancellableOperation<AwaitableCancellableRecv> {
public:
    AwaitableCancellableRecv(
      AsyncOperationProcessor* processor, int fd, CancellationToken* token, Buffer::SinkType buffer)
      : AwaitableCancellableOperation<AwaitableCancellableRecv>(processor, fd, token)
      , buffer_(std::move(buffer)) {}

    ~AwaitableCancellableRecv() { VLOG(1) << "AwaitableCancellableRecv::dtor()"; }

    void PrepareSqe(io_uring_sqe* sqe) {
        io_uring_prep_recv(sqe, GetFD(), buffer_.data(), buffer_.size(), 0);
    }

    std::string ToString() const { return "AwaitableCancellableRecv"; }

private:
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

    std::string ToString() const { return "AwaitableSend"; }

private:
    int fd_;
    std::string data_;
};

} // namespace rdss
