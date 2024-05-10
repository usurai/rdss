// Copyright (c) usurai.
// Licensed under the MIT license.
#include "ring_executor.h"

#include "constants.h" // For kIOGenericBufferSize
#include "sys/util.h"

#include <cassert>
#include <future>
#include <thread>

namespace rdss {

RingExecutor::RingExecutor(std::string name, RingConfig config, std::optional<size_t> id)
  : name_(std::move(name))
  , config_(std::move(config)) {
    std::promise<void> promise;
    auto future = promise.get_future();

    thread_ = std::thread([this, &promise, cpu = id]() {
        if (cpu.has_value()) {
            SetThreadAffinity(
              (config_.sqpoll ? std::vector<size_t>{cpu.value(), cpu.value() + 1}
                              : std::vector<size_t>{cpu.value()}));
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
            params.flags |= IORING_SETUP_TASKRUN_FLAG;
            params.flags |= IORING_SETUP_COOP_TASKRUN;
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

        if (config_.max_direct_descriptors) {
            ret = io_uring_register_files_sparse(&ring_, config_.max_direct_descriptors);
            if (ret) {
                LOG(FATAL) << "io_uring_register_files_sparse:" << strerror(-ret);
            }
            fd_slot_indices_.reserve(config_.max_direct_descriptors);
            for (uint32_t i = 0; i < config_.max_direct_descriptors; ++i) {
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

        assert(tls_ring == nullptr);
        tls_ring = Ring();
        tls_exr = this;

        promise.set_value();
        this->EventLoop();
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

// static
std::unique_ptr<RingExecutor>
RingExecutor::Create(size_t id, std::string name, const Config& config) {
    RingConfig rc{
      .sqpoll = config.sqpoll,
      .submit_batch_size = config.submit_batch_size,
      .wait_batch_size = config.wait_batch_size,
    };
    return std::make_unique<RingExecutor>(std::move(name), std::move(rc), id);
}

// static
std::vector<std::unique_ptr<RingExecutor>>
RingExecutor::Create(size_t n, size_t start_id, std::string name_prefix, const Config& config) {
    std::vector<std::unique_ptr<RingExecutor>> result;
    result.reserve(n);
    for (size_t i = start_id; i < start_id + n; ++i) {
        result.emplace_back(RingExecutor::Create(i, name_prefix + std::to_string(i), config));
    }
    return result;
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

void RingExecutor::EventLoop() {
    __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = std::chrono::nanoseconds{25'000'000}.count()};
    const auto wait_batch = std::max(1U, config_.wait_batch_size);
    const auto submit_batch = std::max(1U, config_.submit_batch_size);

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
    return static_cast<int>(index);
}

void RingExecutor::UnregisterFd(int fd_slot_index) {
    assert(fd_slot_index >= 0);
    fd_slot_indices_.push_back(static_cast<uint32_t>(fd_slot_index));
}

void RingExecutor::InitBufRing() {
    // All exr use 0 as group id.
    const uint16_t buf_group_id = 0;
    const uint32_t buf_size = ((config_.max_direct_descriptors == 0)
                                 ? 1024
                                 : config_.max_direct_descriptors)
                              * kIOGenericBufferSize;

    assert(buf_size % buf_entry_size == 0);
    buf_entries_ = static_cast<uint32_t>(buf_size / buf_entry_size);
    assert(buf_entries_ <= (1 << 15));

    // TODO: 1.alignment 2.mem tracking.
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) {
        LOG(FATAL) << "sysconf(_SC_PAGESIZE)";
    }
    if (posix_memalign(reinterpret_cast<void**>(&buf_), static_cast<size_t>(page_size), buf_size)) {
        LOG(FATAL) << "posix_memalign";
    }

    int ret;
    buf_ring_ = io_uring_setup_buf_ring(Ring(), buf_entries_, buf_group_id, 0, &ret);
    if (buf_ring_ == nullptr) {
        LOG(FATAL) << "io_uring_setup_buf_ring:" << strerror(-ret);
    }

    char* ptr = buf_;
    for (uint32_t i = 0; i < buf_entries_; ++i) {
        io_uring_buf_ring_add(
          buf_ring_,
          ptr,
          buf_entry_size,
          static_cast<uint16_t>(i),
          static_cast<int32_t>(buf_entries_) - 1,
          static_cast<int>(i));
        ptr += buf_entry_size;
    }
    io_uring_buf_ring_advance(buf_ring_, static_cast<int32_t>(buf_entries_));
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
        assert(buffer_view.view.data() >= buf_);
        auto offset = buffer_view.view.data() - buf_;
        assert(offset % buf_entry_size == 0);
        start_entry_id = static_cast<uint32_t>(offset) / buf_entry_size;
    }

    auto remaining_length = buffer_view.view.length();
    uint16_t entry_id = static_cast<uint16_t>(start_entry_id);
    int32_t offset{0};
    const int32_t mask = static_cast<int32_t>(buf_entries_) - 1;
    while (remaining_length) {
        io_uring_buf_ring_add(
          buf_ring_, buf_ + entry_id * buf_entry_size, buf_entry_size, entry_id, mask, offset);
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
