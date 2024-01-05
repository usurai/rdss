#include "io/promise.h"
#include "runtime/ring_executor.h"
#include "util.h"

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include <future>
#include <iostream>
#include <thread>

using namespace rdss;
using namespace std::chrono;

constexpr size_t kRepeat = 1'000'000;

io_uring src_ring;

struct Awaitable
  : public Continuation
  , public std::suspend_always {
    void await_suspend(std::coroutine_handle<> h) {
        handle = std::move(h);
        io_uring_sqe* sqe;
        while ((sqe = io_uring_get_sqe(&src_ring)) == nullptr) {
        }
        io_uring_prep_msg_ring(
          sqe, to->Ring()->enter_ring_fd, 0, reinterpret_cast<uint64_t>(this), 0);
        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
        if (submit) {
            auto ret = io_uring_submit(ring);
            if (ret < 0) {
                LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
            }
        }
    }

    io_uring* ring;
    RingExecutor* to;
    bool submit;
};

Task<void>
TransferTo(io_uring* ring, RingExecutor* to, bool submit, size_t& cnt, std::promise<void>& p) {
    co_await Awaitable{.ring = ring, .to = to, .submit = submit};

    if (++cnt == kRepeat) {
        p.set_value();
    }
}

static void BenchTransfer(benchmark::State& s) {
    for (auto _ : s) {
        std::promise<void> p;
        auto f = p.get_future();
        RingExecutor executor;
        size_t cnt{0};
        SetupRing(&src_ring);

        const size_t submit_batch = s.range(0);

        const auto start = steady_clock::now();
        for (size_t i = 0; i < kRepeat; ++i) {
            const auto submit = (i % submit_batch == submit_batch - 1) || (i == kRepeat - 1);
            benchmark::DoNotOptimize(TransferTo(&src_ring, &executor, submit, cnt, p));
        }
        const auto submission_finish = steady_clock::now();
        f.wait();
        const auto processing_finish = steady_clock::now();
        const auto iteration_time = duration_cast<duration<double>>(processing_finish - start);
        s.SetIterationTime(iteration_time.count());
        s.counters["op/s"] = static_cast<double>(kRepeat) / iteration_time.count();

        executor.Deactivate();
        executor.Shutdown();
    }
}

BENCHMARK(BenchTransfer)->UseManualTime()->RangeMultiplier(2)->Range(1, 1 << 12);

BENCHMARK(BenchTransfer)->Name("BenchTransferSqpoll")->UseManualTime()->Arg(1);

constexpr size_t tasks = 1'000;
std::mutex mu;

Task<void> PingPong(
  size_t index,
  RingExecutor* main,
  RingExecutor* re1,
  std::vector<std::unique_ptr<RingExecutor>>& executors,
  size_t repeat,
  std::vector<size_t>& remainings,
  std::promise<void>& promise) {
    size_t cnt{0};
    auto* re2 = executors[index].get();
    co_await Transfer(main, re2);
    while (cnt < repeat) {
        co_await Transfer(re2, re1);
        co_await Transfer(re1, re2);
        ++cnt;
    }

    if (--remainings[index] != 0) {
        co_return;
    }
    std::lock_guard l(mu);
    for (auto r : remainings) {
        if (r != 0) {
            co_return;
        }
    }
    promise.set_value();
}

static void BenchPingPong(benchmark::State& s) {
    for (auto _ : s) {
        const auto batch_size = s.range(0);
        const auto num_client_executors = s.range(1);

        RingConfig config{};
        if (batch_size == 0) {
            config.sqpoll = true;
        } else {
            config.submit_batch_size = batch_size;
        }

        RingExecutor main("", config);
        RingExecutor service_executor("", config);
        std::vector<std::unique_ptr<RingExecutor>> client_executors;
        client_executors.reserve(num_client_executors);
        std::vector<size_t> remaining_tasks(num_client_executors, 0);
        for (size_t i = 0; i < num_client_executors; ++i) {
            client_executors.emplace_back(std::make_unique<RingExecutor>("", config));
            remaining_tasks[i] = tasks / num_client_executors
                                 + (i < (tasks % num_client_executors) ? 1 : 0);
        }
        constexpr size_t repeat = 4'000;

        std::promise<void> promise;
        auto future = promise.get_future();

        const auto start = steady_clock::now();
        size_t ce_index = 0;
        for (size_t i = 0; i < tasks; ++i) {
            PingPong(
              ce_index,
              &main,
              &service_executor,
              client_executors,
              repeat,
              remaining_tasks,
              promise);
            if (++ce_index == num_client_executors) {
                ce_index = 0;
            }
        }
        const auto submission_finish = steady_clock::now();
        future.wait();
        const auto processing_finish = steady_clock::now();

        const auto iteration_time = duration_cast<duration<double>>(processing_finish - start);
        s.SetIterationTime(iteration_time.count());
        s.counters["op/s"] = static_cast<double>(tasks * repeat) / iteration_time.count();

        main.Deactivate();
        service_executor.Deactivate();
        for (auto& e : client_executors) {
            e->Deactivate();
        }
        main.Shutdown();
        service_executor.Shutdown();
        for (auto& e : client_executors) {
            e->Shutdown();
        }
    }
}

BENCHMARK(BenchPingPong)
  ->UseManualTime()
  ->ArgsProduct({{0, 1, 1 << 2, 1 << 4, 1 << 6, 1 << 8, 1 << 10, 1 << 12}, {1, 2, 3, 4, 5, 6}});

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
