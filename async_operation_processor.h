#pragma once

#include "async_operation.h"

#include <liburing.h>
#include <memory>

namespace rdss {

class AcceptOperation;

class AsyncOperationProcessor {
public:
    void Execute(AcceptOperation* operation);

    static std::unique_ptr<AsyncOperationProcessor> Create();

    io_uring* GetRing() { return &ring_; }

private:
    explicit AsyncOperationProcessor(io_uring ring)
      : ring_(std::move(ring)) {}

    io_uring ring_;
};
} // namespace rdss
