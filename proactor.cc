#include "proactor.h"

#include "async_operation.h"

#include <glog/logging.h>

#include <cstring>

namespace rdss {

void Proactor::Run() {
    io_uring_cqe* cqe;
    while (true) {
        auto ret = io_uring_wait_cqe(ring_, &cqe);
        if (ret) {
            LOG(ERROR) << "io_uring_wait_cqe: " << strerror(-ret);
            continue;
        }
        auto promise = reinterpret_cast<Promise*>(cqe->user_data);
        const auto res = cqe->res;
        io_uring_cqe_seen(ring_, cqe);
        promise->Set(res);
    }
}

} // namespace rdss
