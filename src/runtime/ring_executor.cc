#include "ring_executor.h"

#include <future>
#include <thread>

namespace rdss {

RingExecutor::RingExecutor(std::string name, RingConfig config, size_t cpu)
  : name_(std::move(name))
  , config_(std::move(config)) {
    std::promise<void> promise;
    auto future = promise.get_future();

    thread_ = std::thread([this, &promise, cpu]() {
        // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
        // only CPU i as set.
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            LOG(FATAL) << "Error calling pthread_setaffinity_np: " << rc;
        }

        LOG(INFO) << "Executor " << name_ << " starting at thread " << gettid();

        io_uring_params params = {};
        params.cq_entries = config_.cq_entries;
        params.flags |= IORING_SETUP_CQSIZE;
        params.flags |= IORING_SETUP_SINGLE_ISSUER;
        // params.flags |= IORING_SETUP_DEFER_TASKRUN;
        if (config_.sqpoll) {
            params.flags |= IORING_SETUP_SQPOLL;
        } else {
            params.flags |= IORING_SETUP_COOP_TASKRUN;
        }

        int ret;
        if ((ret = io_uring_queue_init_params(config_.sq_entries, &ring_, &params)) != 0) {
            LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-ret);
        }
        if (!(params.features & IORING_FEAT_NODROP)) {
            LOG(WARNING) << "io_uring: No IORING_FEAT_NODROP";
        }
        if (!(params.features & IORING_FEAT_FAST_POLL)) {
            LOG(WARNING) << "io_uring: No IORING_FEAT_FAST_POLL";
        }

        if (config_.async_sqe) {
            unsigned int max_workers[2] = {0, config_.max_unbound_workers};
            ret = io_uring_register_iowq_max_workers(&ring_, max_workers);
            if (ret != 0) {
                LOG(ERROR) << "io_uring_register_iowq_max_workers:" << strerror(-ret);
            }
        }

        if (config_.max_direct_descriptors) {
            ret = io_uring_register_files_sparse(&ring_, config_.max_direct_descriptors);
            if (ret) {
                LOG(FATAL) << "io_uring_register_files_sparse:" << strerror(-ret);
            }
            fd_slot_indices_.reserve(config_.max_direct_descriptors);
            for (size_t i = 0; i < config_.max_direct_descriptors; ++i) {
                fd_slot_indices_.push_back(i);
            }
        }

        promise.set_value();
        this->LoopNew();
    });

    future.wait();
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

void RingExecutor::LoopNew() {
    __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = std::chrono::nanoseconds{1'000'000}.count()};
    const auto wait_batch = std::max(1UL, config_.wait_batch_size);

    io_uring_cqe* cqe;
    while (active_.load(std::memory_order_relaxed)) {
        // TODO: batch size
        auto ret = io_uring_submit_and_wait_timeout(Ring(), &cqe, wait_batch, &ts, nullptr);
        if (io_uring_cq_has_overflow(Ring())) {
            LOG(WARNING) << name_ << " CQ has overflow.";
        }
        if (ret < 0 && ret != -ETIME) {
            LOG(FATAL) << "io_uring_wait_cqes:" << strerror(-ret);
        }
        unsigned head;
        size_t processed{0};
        io_uring_for_each_cqe(Ring(), head, cqe) {
            ++processed;
            if (cqe->user_data == 0) {
                break;
            }
            auto awaitable = reinterpret_cast<Continuation*>(cqe->user_data);
            awaitable->result = cqe->res;
            awaitable->handle();
        }
        if (processed) {
            io_uring_cq_advance(Ring(), processed);
            VLOG(1) << "Processed " << processed << " events.";
        }
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

int RingExecutor::RegisterFd(int fd) {
    if (fd_slot_indices_.empty()) {
        return -1;
    }
    const auto index = fd_slot_indices_.back();
    fd_slot_indices_.pop_back();
    auto ret = io_uring_register_files_update(Ring(), index, &fd, 1);
    if (ret != 1) {
        LOG(FATAL) << "io_uring_register_files_update:" << strerror(-ret);
    }
    return index;
}

void RingExecutor::UnregisterFd(int fd_slot_index) { fd_slot_indices_.push_back(fd_slot_index); }

} // namespace rdss
