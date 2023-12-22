#include "io/async_operation_processor.h"

#include "base/config.h"
#include "io/async_operation.h"

#include <glog/logging.h>

#include <cstring>

namespace rdss {

std::unique_ptr<AsyncOperationProcessor> AsyncOperationProcessor::Create(Config* config) {
    io_uring ring;
    auto ret = io_uring_queue_init(1024, &ring, IORING_SETUP_SQPOLL);
    if (ret != 0) {
        LOG(ERROR) << "io_uring_queu_init:" << strerror(-ret);
        return nullptr;
    }

    unsigned int max_workers[2] = {
      0, static_cast<unsigned int>(config->io_uring_wq_max_unbound_workers)};
    ret = io_uring_register_iowq_max_workers(&ring, max_workers);
    if (ret != 0) {
        LOG(ERROR) << "io_uring_register_iowq_max_workers:" << strerror(-ret);
        return nullptr;
    }

    return std::unique_ptr<AsyncOperationProcessor>(new AsyncOperationProcessor(std::move(ring)));
}

} // namespace rdss
