#pragma once

#include "base/buffer.h"
#include "io/async_operation_processor.h"
#include "io/cancellation.h"

#include <glog/logging.h>
#include <sys/uio.h>

#include <chrono>
#include <coroutine>
#include <functional>
#include <liburing.h>
#include <memory>
#include <string>

namespace rdss {

class AsyncOperationProcessor;
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
            VLOG(1) << "Initiating recv SQE.";
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

class AwaitableCancellableAccept
  : public AwaitableCancellableOperation<AwaitableCancellableAccept> {
public:
    AwaitableCancellableAccept(AsyncOperationProcessor* processor, int fd, CancellationToken* token)
      : AwaitableCancellableOperation<AwaitableCancellableAccept>(processor, fd, token) {}

    ~AwaitableCancellableAccept() { VLOG(1) << "AwaitableCancellableAccept::dtor()"; }

    void PrepareSqe(io_uring_sqe* sqe) { io_uring_prep_accept(sqe, GetFD(), nullptr, nullptr, 0); }

    std::string ToString() const { return "AwaitableCancellableAccept"; }
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

class AwaitableCancellableSend : public AwaitableCancellableOperation<AwaitableCancellableSend> {
public:
    AwaitableCancellableSend(
      AsyncOperationProcessor* processor, int fd, CancellationToken* token, std::string data)
      : AwaitableCancellableOperation<AwaitableCancellableSend>(processor, fd, token)
      , data_(std::move(data)) {}

    void PrepareSqe(io_uring_sqe* sqe) {
        io_uring_prep_send(sqe, GetFD(), data_.data(), data_.size(), 0);
    }

    std::string ToString() const { return "AwaitableCancellableSend"; }

private:
    std::string data_;
};

} // namespace rdss
