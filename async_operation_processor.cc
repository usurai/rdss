#include "async_operation_processor.h"

#include <glog/logging.h>

#include <cstring>

namespace rdss {

void AsyncOperationProcessor::Execute(AcceptOperation* operation) {
    auto sqe = io_uring_get_sqe(&ring_);
    // assert(sqe != nullptr);
    operation->PrepareSqe(sqe);
    io_uring_sqe_set_data(sqe, operation);
    io_uring_submit(&ring_);
}

std::unique_ptr<AsyncOperationProcessor> AsyncOperationProcessor::Create() {
    io_uring ring;
    const auto ret = io_uring_queue_init(1024, &ring, 0);
    if (ret != 0) {
        LOG(ERROR) << "io_uring_queu_init:" << strerror(-ret);
        return nullptr;
    }
    return std::unique_ptr<AsyncOperationProcessor>(new AsyncOperationProcessor(std::move(ring)));
}
} // namespace rdss
