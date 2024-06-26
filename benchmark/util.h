#include "io/promise.h"
#include "runtime/util.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <future>

using namespace rdss;
using namespace std::chrono;

struct io_uring;

constexpr size_t kSqEntries = 4096;
constexpr size_t kCqEntries = 4096 * 16;

template<bool SingIssuer = false, bool SqPoll = false>
void SetupRing(io_uring* ring) {
    io_uring_params params{};
    params.cq_entries = kCqEntries;
    params.flags |= IORING_SETUP_CQSIZE;
    if constexpr (SingIssuer) {
        params.flags |= IORING_SETUP_SINGLE_ISSUER;
    }
    if constexpr (SqPoll) {
        params.flags |= IORING_SETUP_SQPOLL;
    }

    int ret;
    if ((ret = io_uring_queue_init_params(kSqEntries, ring, &params)) != 0) {
        LOG(FATAL) << "io_uring_queue_init_params:" << strerror(-ret);
    }

    LOG(INFO) << "sq size: " << ring->sq.ring_entries;
    LOG(INFO) << "cq size: " << ring->cq.ring_entries;
}

void SetupRingSqpoll(io_uring* ring);

/// Defines a shard of task that should be executed within a coroutine: For each client_executor,
/// remainings[shard_index] tasks are assigned to them. First transfer the execution to
/// client_executors[shard_index] using tls_ring, optionally executes some logic that is
/// sharded. After that, transfer execution to service_executor to execute the logic should be
/// single-threaded. Then transfer back the execution back to client_executors[shard_index]. Repeats
/// this process for 'repeat' times. Then decrements remainings[shard_index] by one, if it's zero
/// after the decrement, it means this client_executor has finished its job. It should decrese
/// remaining_shards and check if it's zero, if so, it sets the finish_promise to notify the end of
/// tasks.
template<typename ServiceFunc>
Task<void> ShardTask(
  ServiceFunc service_func,
  RingExecutor* service_executor,
  std::vector<std::unique_ptr<RingExecutor>>& client_executors,
  size_t shard_index,
  size_t repeat,
  size_t& shard_remaining_tasks,
  std::atomic<size_t>& remaining_shards,
  std::promise<void>& finish_promise) {
    auto* client_executor = client_executors[shard_index].get();
    co_await ResumeOn(client_executor, true);

    size_t cnt{0};
    const auto num_shards = client_executors.size();
    do {
        co_await ResumeOn(service_executor);
        service_func(num_shards, shard_index, cnt);
        co_await ResumeOn(client_executor);
    } while (++cnt < repeat);

    if (--shard_remaining_tasks != 0) {
        co_return;
    }
    if (remaining_shards.fetch_sub(1, std::memory_order_relaxed) != 1) {
        co_return;
    }
    finish_promise.set_value();
}

/// Executes 'total_tasks' with each task being 'TaskType' with a repeat of 'repeat'. Tasks are
/// sharded into 'num_client_executors', each shard is executed by a ring executor.
template<typename TaskType>
static void BenchSharded(benchmark::State& s) {
    for (auto _ : s) {
        const auto batch_size = static_cast<uint32_t>(s.range(0));
        const auto num_client_executors = static_cast<size_t>(s.range(1));
        const auto tasks_per_client = static_cast<size_t>(s.range(2));
        const size_t repeat = static_cast<size_t>(s.range(3));
        const bool sqpoll = s.range(5);

        RingConfig config{};
        config.sqpoll = sqpoll;
        config.submit_batch_size = batch_size;

        io_uring ring;
        io_uring_queue_init(1024, &ring, 0);
        tls_ring = &ring;

        RingExecutor service_executor("bench-service", config, 0);
        auto client_executors = RingExecutor::Create(
          num_client_executors,
          sqpoll ? 2 : 1,
          "cli_exr_",
          Config{.submit_batch_size = static_cast<uint32_t>(batch_size)});
        std::vector<size_t> remaining_tasks(num_client_executors, 0);
        const auto total_tasks = num_client_executors * tasks_per_client;
        for (size_t i = 0; i < num_client_executors; ++i) {
            remaining_tasks[i] = total_tasks / num_client_executors
                                 + (i < (total_tasks % num_client_executors) ? 1 : 0);
        }

        std::promise<void> promise;
        auto future = promise.get_future();

        const auto start = steady_clock::now();
        size_t ce_index = 0;
        std::atomic<size_t> remaining_shards = num_client_executors;
        for (size_t i = 0; i < total_tasks; ++i) {
            TaskType()(
              &service_executor,
              client_executors,
              ce_index,
              repeat,
              remaining_tasks[ce_index],
              remaining_shards,
              promise);
            if (++ce_index == num_client_executors) {
                ce_index = 0;
            }
        }
        [[maybe_unused]] const auto submission_finish = steady_clock::now();
        future.wait();
        const auto processing_finish = steady_clock::now();

        const auto iteration_time = duration_cast<duration<double>>(processing_finish - start);
        s.SetIterationTime(iteration_time.count());
        s.counters["op/s"] = static_cast<double>(total_tasks * repeat) / iteration_time.count();

        service_executor.Deactivate();
        for (auto& e : client_executors) {
            e->Deactivate();
        }
        service_executor.Shutdown();
        for (auto& e : client_executors) {
            e->Shutdown();
        }

        io_uring_queue_exit(&ring);
    }
}

void GenerateRandomKeys(
  std::vector<std::string>& keys, size_t num, const std::string& prefix, size_t key_digits);
