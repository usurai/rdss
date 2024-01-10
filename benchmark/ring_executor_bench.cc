#include "util.h"

#include <glog/logging.h>

#include <thread>

constexpr size_t kRepeat = 10'000'000;
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
        const size_t submit_batch = s.range(0);
        if (submit_batch == 0) {
            SetupRingSqpoll(&src_ring);
        } else {
            SetupRing(&src_ring);
        }

        const auto start = steady_clock::now();
        for (size_t i = 0; i < kRepeat; ++i) {
            const auto submit = (submit_batch == 0) || (i % submit_batch == submit_batch - 1)
                                || (i == kRepeat - 1);
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

BENCHMARK(BenchTransfer)->Name("BenchTransferSqpoll")->UseManualTime()->Arg(0);
BENCHMARK(BenchTransfer)->UseManualTime()->RangeMultiplier(2)->Range(1, 1 << 12);

struct PingPong {
    static void nop(size_t, size_t) {}

    Task<void> operator()(
      RingExecutor* main_executor,
      RingExecutor* service_executor,
      std::vector<std::unique_ptr<RingExecutor>>& client_executors,
      size_t shard_index,
      size_t repeat,
      std::mutex& mu,
      std::vector<size_t>& remainings,
      std::promise<void>& finish_promise) {
        return ShardTask(
          nop,
          main_executor,
          service_executor,
          client_executors,
          shard_index,
          repeat,
          mu,
          remainings,
          finish_promise);
    }
};

BENCHMARK(BenchSharded<PingPong>)
  ->Name("PingPong")
  ->UseManualTime()
  ->ArgsProduct({
    {1000},                                                   // total tasks
    {10'000},                                                 // repeats within each task
    {0, 1, 1 << 2, 1 << 4, 1 << 6, 1 << 8, 1 << 10, 1 << 12}, // submission batch size (0 mean
                                                              // sqpoll)
    {1, 2, 3, 4, 5, 6}                                        // shard number
  });

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
