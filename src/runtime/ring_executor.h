#pragma once

#include "base/config.h"
#include "io/promise.h"
#include "runtime/continuation.h"

#include <glog/logging.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <liburing.h>
#include <optional>
#include <thread>

namespace rdss::detail {

struct RingTransfer
  : public Continuation
  , public std::suspend_always {
    bool await_ready() const { return no_transfer; }

    void await_suspend(std::coroutine_handle<> h) {
        handle = std::move(h);
        // TODO: GetSqe()
        io_uring_sqe* sqe;
        while ((sqe = io_uring_get_sqe(ring)) == nullptr) {
        }

        io_uring_prep_msg_ring(sqe, target_fd, 0, reinterpret_cast<uint64_t>(this), 0);
        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
        if (submit) {
            auto ret = io_uring_submit(ring);
            if (ret < 0) {
                LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
            }
        }
    }

    io_uring* ring;
    int target_fd;
    bool submit;
    bool no_transfer{false};
};

} // namespace rdss::detail

namespace rdss {

// Forward declaration for 'tls_exr'.
class RingExecutor;

/// Thread-local ring for sending ring message to other executors.
/// - For RingExecutor's worker thread, this is set to RingExecutor's ring.
/// - For main thread, this is set to Server's 'ring_'.
inline thread_local io_uring* tls_ring = nullptr;

/// Thread-local pointer to RingExecutor. Only RingExecutor's worker thread has this set to the
/// corresponding RingExecutor.
inline thread_local RingExecutor* tls_exr = nullptr;

// TODO
struct RingConfig {
    size_t sq_entries = 4096;
    size_t cq_entries = 4096 * 16;
    bool sqpoll = false;
    size_t submit_batch_size = 32;
    size_t wait_batch_size = 1;
    size_t max_direct_descriptors = 4096;
    bool register_ring_fd = true;
};

/// The io_uring-based executor offers versatile functionality for executing CPU-bound tasks or
/// handling I/O operations efficiently using its underlying ring structure.
///
/// To initiate a task on
/// the executor, one typically employs the `Schedule()` function. This function requires the
/// pointer to an io_uring, referred to as `src_ring`, along with a callable representing the task.
/// `Schedule()` utilizes io_uring's ring message mechanism to communicate between rings, and
/// 'src_ring' is used to send ring message. Often, communication occurs between executors, with the
/// caller of `Schedule()` being another executor. In such cases, a variant of `Schedule()` is
/// utilized, accepting only the callable as a parameter. This variant utilizes a thread-local
/// `tls_ring`, set as the caller executor's ring during its construction, for ring message
/// transmission. Additionally, `Schedule()` can be invoked within the executor's thread itself,
/// resulting in the inline execution of the callable.
///
/// For initiating I/O tasks within the executor, the `Initiate()` API is employed.
/// `Initiate()` is a function template that takes an `Operation`, meeting two criteria:
/// 1. It is derived from 'Continuation' and suspends its coroutine execution, awaiting the async
/// I/O result via Continuation's `coroutine_handle`.
/// 2. It can describe the desired I/O operation by providing a `Prepare()` function, which
/// configures an `io_uring_sqe` for submission to the io_uring.
/// Here's how `Initiate()` operates:
/// 1. Upon invocation, the executor obtains an SQE from its ring and passes it to the `Prepare()`
/// function of the `Operation`, detailing the intended action.
/// 2. The executor sets the address of the `Operation` as the SQE's private data and submits it to
/// the ring.
/// 3. Upon completion of the task, a CQE (Completion Queue Event) is generated and subsequently
/// processed by the executor.
/// 4. The executor retrieves the associated `Operation` using private data, sets the CQE's result
/// and flags to the `Continuation`, and invokes the `coroutine_handle` to resume the suspended
/// execution.
///
/// It's important to note that there is no external synchronization during SQE submission, and only
/// the executor's worker thread can submit to the ring. Therefore, `Initiate()` should only be
/// called within the executor's worker thread. A typical approach involves using `Schedule()` to
/// execute the function responsible for I/O submission on the executor, such as servicing a TCP
/// connection.
class RingExecutor {
public:
    /// 'id' is used to set CPU affinity of the worker thread.
    RingExecutor(
      std::string name = "",
      RingConfig config = RingConfig{},
      std::optional<size_t> id = std::nullopt);

    RingExecutor(const RingExecutor&) = delete;
    RingExecutor(RingExecutor&&) = delete;
    RingExecutor& operator=(const RingExecutor&) = delete;
    RingExecutor& operator=(RingExecutor&&) = delete;

    ~RingExecutor();

    /// Factory method to create an executor with Server's config.
    static std::unique_ptr<RingExecutor> Create(size_t id, std::string name, const Config& config);

    /// Factory method to create 'n' RingExecutors, with an increasing 'id' starting at 'start_id'.
    /// The name of executors will be 'name_prefix' + 'id'.
    static std::vector<std::unique_ptr<RingExecutor>>
    Create(size_t n, size_t start_id, std::string name_prefix, const Config& config);

    io_uring* Ring() { return &ring_; }

    int RingFD() const { return fd_; }

    // TODO
    /// To stop the executor, one needs to first call 'Deactivate()', which sets 'active_' flag of
    /// executor to false and then sends a ring msg with user_data set as 0 to wake the worker
    /// thread of the executor. This will terminate the worker thread. After that, one needs to call
    /// 'Shutdown()' to blocking wait for the worker thread to terminate. Finally, call the
    /// destructor implicitly to exit the io_uring.
    /// The caller can provide the io_uring for this function to send the ring message, or make the
    /// function to create the ring itself by passing nullptr.
    void Deactivate(io_uring* ring = nullptr);

    void Shutdown();

    template<typename Operation>
    void Initiate(Operation* operation);

    /// Schedules the execution of 'func' on this executor.
    /// - If 'src_ring' is the ring of 'this', 'func' will be executed inline.
    /// - Otherwise, 'func' will be captured as coroutine_handle and sent to 'this' via ring message
    /// via 'src_ring', and executed after reapped.
    template<typename FuncType>
    Task<void> Schedule(io_uring* src_ring, FuncType func);

    /// Similar to the above but using thread-local ring 'rls_ring' as 'src_ring'.
    template<typename FuncType>
    Task<void> Schedule(FuncType func) {
        return Schedule(tls_ring, std::move(func));
    }

    // Registers 'fd' with the executor's ring. Returns the index into the registered fd if
    // successful, returns -1 otherwise.
    int RegisterFd(int fd);

    void UnregisterFd(int fd_slot_index);

    void InitBufRing();

    // BufferView is the view of the recv result stored in the ring buffer. If the referenced buffer
    // is consecutive, 'view' is simply the string_view over that buffer. If the referenced buffer
    // is non-consecutive, 'RingExecutor' creates a temporary string to and copies the two parts
    // into the string, then 'view' is string_view over the temporary string.
    struct BufferView {
        std::string_view view;

        // If the referenced buffer are non-adjacent, i.e. one at the last and one at the first,
        // then 'separated_entry_id' is the first entry id of the entry at the last part of the
        // buffer. nullopt otherwise.:w
        std::optional<uint32_t> separated_entry_id = std::nullopt;
    };

    BufferView GetBufferView(uint32_t entry_id, size_t length);

    void PutBufferView(BufferView&& buffer_view);

private:
    void EventLoop();

    const std::string name_;
    const RingConfig config_;
    std::atomic<bool> active_ = true;
    io_uring ring_;
    int fd_;
    std::thread thread_;
    std::vector<int> fd_slot_indices_;

    io_uring_buf_ring* buf_ring_{nullptr};
    char* buf_{nullptr};
    // TODO: 1. move somewhere 2. rename to kXXX
    static constexpr size_t buf_entry_size = 2048 * 2;
    size_t buf_entries_{0};
    std::string concat_str_;
};

template<typename Operation>
void RingExecutor::Initiate(Operation* operation) {
    // TODO: Consolidate below into Ring->GetSqe()
    io_uring_sqe* sqe{nullptr};
    while ((sqe = io_uring_get_sqe(Ring())) == nullptr) {
        if (config_.sqpoll) {
            auto ret = io_uring_sqring_wait(Ring());
            if (ret < 0) {
                LOG(FATAL) << "io_uring_sqring_wait:" << strerror(-ret);
            }
        } else {
            io_uring_submit(Ring());
        }
    }

    operation->Prepare(sqe);
    io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(operation));
}

template<typename FuncType>
Task<void> RingExecutor::Schedule(io_uring* src_ring, FuncType func) {
    assert(src_ring != nullptr);
    if (src_ring != Ring()) {
        co_await detail::RingTransfer{.ring = src_ring, .target_fd = RingFD(), .submit = true};
    }
    func();
}

} // namespace rdss
