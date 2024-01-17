#include "util.h"

#include <glog/logging.h>

#include <algorithm>
#include <liburing.h>

void SetupRingSqpoll(io_uring* ring) {
    io_uring_params params{};
    params.cq_entries = kCqEntries;
    params.flags |= IORING_SETUP_SQPOLL;
    int ret;
    if ((ret = io_uring_queue_init_params(kSqEntries, ring, &params)) != 0) {
        LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-ret);
    }
}

void GenerateRandomKeys(
  std::vector<std::string>& keys, size_t num, const std::string& prefix, size_t key_digits) {
    keys.reserve(num);
    for (size_t i = 0; i < num; ++i) {
        const auto s = std::to_string(i);
        if (s.size() < key_digits) {
            keys.push_back(prefix + std::string(key_digits - s.size(), 0) + s);
        } else {
            keys.push_back(prefix + s);
        }
    }
    std::random_shuffle(keys.begin(), keys.end());
}
