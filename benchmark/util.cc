#include "util.h"

#include <glog/logging.h>

#include <liburing.h>

constexpr size_t kSqEntries = 4096;
constexpr size_t kCqEntries = 4096 * 16;

void SetupRing(io_uring* ring) {
    io_uring_params params{};
    params.cq_entries = kCqEntries;
    int ret;
    if ((ret = io_uring_queue_init_params(kSqEntries, ring, &params)) != 0) {
        LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-ret);
    }
}

void SetupRingSqpoll(io_uring* ring) {
    io_uring_params params{};
    params.cq_entries = kCqEntries;
    params.flags |= IORING_SETUP_SQPOLL;
    int ret;
    if ((ret = io_uring_queue_init_params(kSqEntries, ring, &params)) != 0) {
        LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-ret);
    }
}
