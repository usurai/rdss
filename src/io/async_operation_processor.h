// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <glog/logging.h>

#include <liburing.h>
#include <memory>

namespace rdss {

template<typename T>
class AwaitableOperation;

template<typename T>
class AwaitableCancellableOperation;

struct Config;

class AsyncOperationProcessor {
public:
    // TODO: Try to batch the submit.
    template<typename T>
    void Execute(AwaitableOperation<T>* operation) {
        VLOG(1) << "AsyncOperationProcessor::Execute(" << operation->ToString() << ')';
        auto sqe = io_uring_get_sqe(&ring_);
        operation->PrepareSqe(sqe);
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
        io_uring_submit(&ring_);
    }

    template<typename T>
    void Execute(AwaitableCancellableOperation<T>* operation) {
        VLOG(1) << "AsyncOperationProcessor::Execute(" << operation->ToString() << ')';
        auto sqe = io_uring_get_sqe(&ring_);
        operation->PrepareSqe(sqe);
        io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
        io_uring_submit(&ring_);
    }

    static std::unique_ptr<AsyncOperationProcessor> Create(Config* config);

    io_uring* GetRing() { return &ring_; }

private:
    explicit AsyncOperationProcessor(io_uring ring)
      : ring_(std::move(ring)) {}

    io_uring ring_;
};
} // namespace rdss
