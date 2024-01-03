#include "ring_executor.h"

namespace rdss {

RingExecutor::RingExecutor(std::string name, RingConfig config)
  : name_(std::move(name))
  , config_(std::move(config)) {
    io_uring_params params = {};
    params.cq_entries = config_.cq_entries;
    if (config_.sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
    }
    // TODO: This caused EEXIST when submitting.
    // params.flags |= IORING_SETUP_SINGLE_ISSUER;

    int ret;
    if ((ret = io_uring_queue_init_params(config_.sq_entries, &ring_, &params)) != 0) {
        LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-ret);
    }
    if (!(params.features & IORING_FEAT_NODROP)) {
        LOG(WARNING) << "io_uring: No IORING_FEAT_NODROP";
    }

    if (config_.async_sqe) {
        unsigned int max_workers[2] = {0, config_.max_unbound_workers};
        ret = io_uring_register_iowq_max_workers(&ring_, max_workers);
        if (ret != 0) {
            LOG(ERROR) << "io_uring_register_iowq_max_workers:" << strerror(-ret);
        }
    }

    thread_ = std::thread([this]() {
        LOG(INFO) << "Executor " << name_ << " starting at thread " << gettid();
        if (this->config_.sqpoll) {
            this->Loop();
        } else {
            this->LoopTimeoutWait();
        }
    });
}

RingExecutor::~RingExecutor() {
    io_uring_queue_exit(Ring());
    LOG(INFO) << "Executor " << name_ << " exiting.";
}

void RingExecutor::Shutdown() {
    if (!thread_.joinable()) {
        LOG(ERROR) << "Thread not joinable";
        return;
    }
    thread_.join();
}

void RingExecutor::Deactivate(io_uring* ring) {
    active_.store(false, std::memory_order_relaxed);
    const auto local_ring = (ring == nullptr);
    if (local_ring) {
        ring = new io_uring{};
        auto ret = io_uring_queue_init(4, ring, 0);
        if (ret) {
            LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
        }
    }
    auto sqe = io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        LOG(FATAL) << "io_uring_get_sqe";
    }
    io_uring_prep_msg_ring(sqe, Ring()->enter_ring_fd, 0, 0, 0);
    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
    auto ret = io_uring_submit(ring);
    if (ret < 0) {
        LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
    }
    if (local_ring) {
        io_uring_queue_exit(ring);
        delete ring;
    }
}

// For SQPOLL enabled ring.
void RingExecutor::Loop() {
    io_uring_cqe* cqe;
    int ret;
    while (active_.load(std::memory_order_relaxed)) {
        ret = io_uring_wait_cqe(&ring_, &cqe);
        // TODO: Try io_uring_cq_advance.
        do {
            if (ret != 0) {
                LOG(FATAL) << "io_uring_wait_cqe:" << strerror(-ret);
            }

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

void RingExecutor::LoopTimeoutWait() {
    // TODO: move out.
    __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = std::chrono::nanoseconds{500'000}.count()};

    io_uring_cqe* cqe;
    int ret;
    while (active_.load(std::memory_order_relaxed)) {
        ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
        // TODO: Try io_uring_cq_advance.
        do {
            if (ret == -ETIME) {
                if (io_uring_sq_ready(&ring_)) {
                    // TOOD: submit_and_wait
                    ret = io_uring_submit(&ring_);
                    if (ret < 0) {
                        LOG(FATAL) << name_ << ": io_uring_submit " << strerror(-ret);
                    }
                    VLOG(1) << name_ << " submitted " << ret << " SQEs at wait side.";
                }
                break;
            }
            if (ret) {
                LOG(FATAL) << "io_uring_wait_cqe_timeout:" << strerror(ret);
            }

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
    if (!config_.sqpoll && io_uring_sq_ready(Ring()) < config_.submit_batch_size) {
        return;
    }
    auto ret = io_uring_submit(Ring());
    if (ret < 0) {
        LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
    }
}

} // namespace rdss
