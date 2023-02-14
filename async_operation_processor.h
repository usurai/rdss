#pragma once

#include <glog/logging.h>

#include <liburing.h>
#include <memory>

namespace rdss {

template<typename T>
class AwaitableOperation;

class AwaitableCancellableRecv;

class AsyncOperationProcessor {
public:
    template<typename T>
    void Execute(AwaitableOperation<T>* operation) {
        VLOG(1) << "AsyncOperationProcessor::Execute(" << operation->ToString() << ')';
        auto sqe = io_uring_get_sqe(&ring_);
        operation->PrepareSqe(sqe);
        io_uring_submit(&ring_);
    }

    void Execute(AwaitableCancellableRecv* operation);

    static std::unique_ptr<AsyncOperationProcessor> Create();

    io_uring* GetRing() { return &ring_; }

private:
    explicit AsyncOperationProcessor(io_uring ring)
      : ring_(std::move(ring)) {}

    io_uring ring_;
};
} // namespace rdss
