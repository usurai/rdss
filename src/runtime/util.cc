#include "util.h"

#include <future>

namespace rdss {

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
