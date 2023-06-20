#include "io/async_operation_processor.h"

#include "io/async_operation.h"

#include <glog/logging.h>

#include <cstring>

namespace rdss {

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
