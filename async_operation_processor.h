#pragma once

#include <liburing.h>
#include <memory>

namespace rdss {

template<typename T>
class AwaitableOperation;

class AsyncOperationProcessor {
public:
    template<typename T>
    void Execute(AwaitableOperation<T>* operation) {
        auto sqe = io_uring_get_sqe(&ring_);
        operation->PrepareSqe(sqe);
        io_uring_submit(&ring_);
    }

    static std::unique_ptr<AsyncOperationProcessor> Create();

    io_uring* GetRing() { return &ring_; }

private:
    explicit AsyncOperationProcessor(io_uring ring)
      : ring_(std::move(ring)) {}

    io_uring ring_;
};
} // namespace rdss
