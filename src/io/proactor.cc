#include "io/proactor.h"

#include "io/async_operation.h"

#include <glog/logging.h>

#include <cstring>

namespace rdss {

void Proactor::Run() {
    io_uring_cqe* cqe;
    while (active_) {
        auto ret = io_uring_wait_cqe(ring_, &cqe);
        if (ret) {
            LOG(ERROR) << "io_uring_wait_cqe: " << strerror(-ret);
            continue;
        }
        do {
            auto handler = reinterpret_cast<CompletionHandler*>(cqe->user_data);
            const auto res = cqe->res;
            io_uring_cqe_seen(ring_, cqe);
            handler->Complete(res);
            // TODO: use io_uring_peek_batch_cqe
        } while (!io_uring_peek_cqe(ring_, &cqe));
    }
}

} // namespace rdss
