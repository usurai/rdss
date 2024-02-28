#include "ring_executor.h"

#include "constants.h" // For kIOGenericBufferSize

#include <cassert>
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

        // auto ret = pthread_setname_np(pthread_self(), name_.c_str());
        auto ret = pthread_setname_np(pthread_self(), name_.c_str());
        if (ret) {
            LOG(FATAL) << "pthread_setname_np:" << strerror(ret);
        }

        LOG(INFO) << "Executor " << name_ << " starting at thread " << gettid();

        io_uring_params params = {};
        params.cq_entries = config_.cq_entries;
        params.flags |= IORING_SETUP_CQSIZE;
        params.flags |= IORING_SETUP_SINGLE_ISSUER;
        if (config_.sqpoll) {
            params.flags |= IORING_SETUP_SQPOLL;
        } else {
            if (name_.starts_with("dss")) {
                // params.flags |= IORING_SETUP_TASKRUN_FLAG;
                // params.flags |= IORING_SETUP_COOP_TASKRUN;
            } else {
                params.flags |= IORING_SETUP_TASKRUN_FLAG;
                // params.flags |= IORING_SETUP_DEFER_TASKRUN;
                params.flags |= IORING_SETUP_COOP_TASKRUN;
            }
            // params.flags |= IORING_SETUP_TASKRUN_FLAG;
            // params.flags |= IORING_SETUP_COOP_TASKRUN;
            // params.flags |= IORING_SETUP_DEFER_TASKRUN;
        }

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

        fd_ = ring_.enter_ring_fd;
        if (config_.register_ring_fd) {
            ret = io_uring_register_ring_fd(&ring_);
            if (ret != 1) {
                LOG(FATAL) << "io_uring_register_ring_fd:" << strerror(-ret);
            }
        }

        promise.set_value();
        this->LoopNew();
    });

    future.wait();
}

RingExecutor::~RingExecutor() {
    if (buf_ring_ != nullptr) {
        io_uring_free_buf_ring(Ring(), buf_ring_, buf_entries_, 0 /* group id */);
        free(buf_);
    }
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
    io_uring_prep_msg_ring(sqe, RingFD(), 0, 0, 0);
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
    const auto submit_batch = std::max(1UL, config_.submit_batch_size);
    LOG(INFO) << name_ << " submit_batch: " << submit_batch << " wait_batch: " << wait_batch;

    io_uring_cqe* cqe;
    while (active_.load(std::memory_order_relaxed)) {
        auto ret = io_uring_submit_and_wait_timeout(Ring(), &cqe, wait_batch, &ts, nullptr);

        if (io_uring_cq_has_overflow(Ring())) {
            LOG(WARNING) << name_ << " CQ has overflow.";
        }
        if (*Ring()->sq.kflags & IORING_SQ_CQ_OVERFLOW) {
            LOG(WARNING) << name_ << " saw overflow.";
        }

        if (ret < 0 && ret != -ETIME) {
            LOG(FATAL) << "io_uring_submit_and_wait_timeout:" << strerror(-ret);
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
            awaitable->flags = cqe->flags;
            awaitable->handle();

            if (processed % submit_batch == 0) {
                io_uring_submit(Ring());
                io_uring_cq_advance(Ring(), submit_batch);
            }
        }
        if (processed % submit_batch) {
            io_uring_cq_advance(Ring(), processed % submit_batch);
        }
        VLOG(1) << "Processed " << processed << " events.";
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

void RingExecutor::InitBufRing() {
    // All exr use 0 as group id.
    const auto buf_group_id = 0;
    const size_t buf_size = ((config_.max_direct_descriptors == 0) ? 1024
                                                                   : config_.max_direct_descriptors)
                            * kIOGenericBufferSize;

    assert(buf_size % buf_entry_size == 0);
    buf_entries_ = buf_size / buf_entry_size;
    assert(buf_entries_ <= (1 << 15));

    // TODO: 1.alignment 2.mem tracking.
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) {
        LOG(FATAL) << "sysconf(_SC_PAGESIZE)";
    }
    if (posix_memalign(reinterpret_cast<void**>(&buf_), page_size, buf_size)) {
        LOG(FATAL) << "posix_memalign";
    }

    int ret;
    buf_ring_ = io_uring_setup_buf_ring(Ring(), buf_entries_, buf_group_id, 0, &ret);
    if (buf_ring_ == nullptr) {
        LOG(FATAL) << "io_uring_setup_buf_ring:" << strerror(-ret);
    }

    char* ptr = buf_;
    for (size_t i = 0; i < buf_entries_; ++i) {
        io_uring_buf_ring_add(buf_ring_, ptr, buf_entry_size, i, buf_entries_ - 1, i);
        ptr += buf_entry_size;
    }
    io_uring_buf_ring_advance(buf_ring_, buf_entries_);
    LOG(INFO) << name_ << " setups buffer ring of size " << buf_size << " with " << buf_entries_
              << " entries, each has size " << buf_entry_size;
}

RingExecutor::BufferView RingExecutor::GetBufferView(uint32_t entry_id, size_t length) {
    assert(entry_id < buf_entries_);
    const auto num_entries = length / buf_entry_size + ((length % buf_entry_size == 0) ? 0 : 1);
    if (num_entries + entry_id <= buf_entries_) {
        return RingExecutor::BufferView{
          .view = std::string_view{buf_ + entry_id * buf_entry_size, length},
          .separated_entry_id = std::nullopt};
    }

    // TODO: We assume only one separated result at the same time.
    assert(concat_str_.empty());

    const auto first_part_length = (buf_entries_ - entry_id) * buf_entry_size;
    assert(first_part_length < length);
    concat_str_.resize(length);
    std::memcpy(concat_str_.data(), buf_ + entry_id * buf_entry_size, first_part_length);
    std::memcpy(concat_str_.data() + first_part_length, buf_, length - first_part_length);
    return RingExecutor::BufferView{.view = concat_str_, .separated_entry_id = entry_id};
}

void RingExecutor::PutBufferView(RingExecutor::BufferView&& buffer_view) {
    uint32_t start_entry_id;
    if (buffer_view.separated_entry_id.has_value()) {
        start_entry_id = buffer_view.separated_entry_id.value();
    } else {
        assert((buffer_view.view.data() - buf_) % buf_entry_size == 0);
        start_entry_id = (buffer_view.view.data() - buf_) / buf_entry_size;
    }

    auto remaining_length = buffer_view.view.length();
    auto entry_id = start_entry_id;
    size_t offset{0};
    auto mask = io_uring_buf_ring_mask(buf_entries_);
    while (remaining_length) {
        io_uring_buf_ring_add(
          buf_ring_,
          buf_ + entry_id * buf_entry_size,
          buf_entry_size,
          entry_id,
          buf_entries_ - 1,
          offset);
        remaining_length -= (remaining_length < buf_entry_size) ? remaining_length : buf_entry_size;
        entry_id = (entry_id + 1) & (buf_entries_ - 1);
        ++offset;
    }
    io_uring_buf_ring_advance(buf_ring_, offset);

    if (buffer_view.separated_entry_id.has_value()) {
        concat_str_.clear();
    }
}

} // namespace rdss
