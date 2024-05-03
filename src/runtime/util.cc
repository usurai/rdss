#include "util.h"

#include <future>

namespace rdss {

detail::RingTransfer ResumeOn(RingExecutor* exr) {
    if (exr == tls_exr) {
        return detail::RingTransfer{.no_transfer = true};
    }

    auto src_ring = tls_ring != nullptr ? tls_ring
                                        : (tls_exr != nullptr ? tls_exr->Ring() : nullptr);
    assert(src_ring != nullptr);
    return detail::RingTransfer{.ring = src_ring, .target_fd = exr->RingFD(), .submit = false};
}

void SetupInitBufRing(std::vector<std::unique_ptr<RingExecutor>>& exrs) {
    std::atomic<size_t> num_not_finished{exrs.size()};
    std::promise<void> init_promise;
    auto init_future = init_promise.get_future();
    for (auto& exr : exrs) {
        exr->Schedule([&exr, &num_not_finished, &init_promise]() {
            exr->InitBufRing();
            if (num_not_finished.fetch_sub(1, std::memory_order_release) == 1) {
                init_promise.set_value();
            }
        });
    }
    init_future.wait();
}

} // namespace rdss
