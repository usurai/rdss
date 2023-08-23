#include "ring_executor.h"

namespace rdss {

RingExecutor::RingExecutor(std::string name)
  : name_(std::move(name)) {
    // TODO: constant/config {SQPOLL, sq/cq_entries, max_workers}.
    io_uring_params params = {};
    params.cq_entries = 50000;
    params.flags |= IORING_SETUP_SQPOLL;
    int res;
    if ((res = io_uring_queue_init_params(4096, &ring_, &params)) != 0) {
        LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-res);
    }

    unsigned int max_workers[2] = {0, 5};
    auto ret = io_uring_register_iowq_max_workers(&ring_, max_workers);
    if (ret != 0) {
        LOG(FATAL) << "io_uring_register_iowq_max_workers:" << strerror(-ret);
    }

    if (!(params.features & IORING_FEAT_NODROP)) {
        LOG(FATAL) << "No IORING_FEAT_NODROP";
    }

    thread_ = std::thread([this]() {
        LOG(INFO) << "Executor " << name_ << " at thread " << gettid();
        this->Loop();
    });
}

void RingExecutor::Shutdown() {
    if (!thread_.joinable()) {
        LOG(FATAL) << "Thread not joinable";
    }
    active_.store(false);

    auto sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, 0);
    io_uring_submit(&ring_);

    thread_.join();
}

void RingExecutor::Loop() {
    // TODO: move out.
    __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = std::chrono::nanoseconds{200'000}.count()};

    io_uring_cqe* cqe;
    while (active_.load()) {
        // auto result = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
        auto result = io_uring_wait_cqe(&ring_, &cqe);
        // TODO: Try io_uring_cq_advance.
        do {
            // if (result != 0) {
            //     if (io_uring_sq_ready(&ring_)) {
            //         // TOOD: submit_and_wait
            //         io_uring_submit(&ring_);
            //     }
            //     break;
            // }

            const auto data = cqe->user_data;
            const auto res = cqe->res;
            io_uring_cqe_seen(&ring_, cqe);

            if (data == 0) {
                continue;
            }
            auto awaitable = reinterpret_cast<Continuation*>(data);
            awaitable->result = res;
            awaitable->handle();
        } while (!io_uring_peek_cqe(&ring_, &cqe));
    }
    LOG(INFO) << "Terminating thread " << gettid();
}

void RingExecutor::MaybeSubmit() {
    // if (io_uring_sq_ready(Ring()) < kRingSubmitBatchSize) {
    //     return;
    // }
    auto res = io_uring_submit(Ring());
    if (res < 0) {
        LOG(FATAL) << "io_uring_submit:" << strerror(-res);
    }
}

} // namespace rdss
