#include "util.h"

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include <chrono>
#include <liburing.h>
#include <thread>

using namespace std::chrono;

constexpr size_t kRepeat = 1'000'000;

io_uring src_ring;
io_uring dest_ring;

static void SetupRingPair(const benchmark::State&) {
    SetupRing(&src_ring);
    SetupRing(&dest_ring);
}

static void SetupRingPairSqpoll(const benchmark::State&) {
    SetupRingSqpoll(&src_ring);
    SetupRingSqpoll(&dest_ring);
}

static void TeardownRingPair(const benchmark::State&) {
    io_uring_queue_exit(&src_ring);
    io_uring_queue_exit(&dest_ring);
}

void RingConsumeWait() {
    io_uring_cqe* cqe;
    size_t cnt{0};
    while (true) {
        int ret = io_uring_wait_cqe(&dest_ring, &cqe);
        if (ret != 0) {
            LOG(FATAL) << "io_uring_wait_cqe:" << strerror(-ret);
        }
        io_uring_cqe_seen(&dest_ring, cqe);
        if (++cnt == kRepeat) {
            return;
        }
    }
}

void RingConsumePeek() {
    io_uring_cqe* cqe;
    size_t cnt{0};
    while (true) {
        int ret = io_uring_wait_cqe(&dest_ring, &cqe);
        do {
            if (ret != 0) {
                LOG(FATAL) << "io_uring_wait_cqe:" << strerror(-ret);
            }
            io_uring_cqe_seen(&dest_ring, cqe);
            if (++cnt == kRepeat) {
                return;
            }
        } while ((ret = io_uring_peek_cqe(&dest_ring, &cqe)) == 0);
    }
}

void RingConsumeTimeoutWait() {
    __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = std::chrono::nanoseconds{1'000'000}.count()};

    io_uring_cqe* cqe;
    int ret;
    size_t cnt{0};
    while (true) {
        ret = io_uring_wait_cqe_timeout(&dest_ring, &cqe, &ts);
        // TODO: Try io_uring_cq_advance.
        do {
            if (ret == -ETIME) {
                if (io_uring_sq_ready(&dest_ring)) {
                    // TOOD: submit_and_wait
                    ret = io_uring_submit(&dest_ring);
                    if (ret < 0) {
                        LOG(FATAL) << ": io_uring_submit " << strerror(-ret);
                    }
                    VLOG(1) << " submitted " << ret << " SQEs at wait side.";
                }
                break;
            }
            if (ret) {
                LOG(FATAL) << "io_uring_wait_cqe_timeout:" << strerror(ret);
            }
            io_uring_cqe_seen(&dest_ring, cqe);

            if (++cnt == kRepeat) {
                return;
            }
        } while (!io_uring_peek_cqe(&dest_ring, &cqe));
    }
}

enum class ConsumeMethod {
    kWait,
    kPeek,
    kTimeoutWait,
};

auto ConsumeMethodFunction(ConsumeMethod m) {
    switch (m) {
    case ConsumeMethod::kWait:
        return RingConsumeWait;
    case ConsumeMethod::kPeek:
        return RingConsumePeek;
    case ConsumeMethod::kTimeoutWait:
        return RingConsumeTimeoutWait;
    }
}

static void BenchRingMsg(benchmark::State& s, ConsumeMethod consume_method) {
    for (auto _ : s) {
        int ret;

        std::thread t(ConsumeMethodFunction(consume_method));

        const size_t submit_batch = s.range(0);

        const auto start = steady_clock::now();

        io_uring_sqe* sqe;
        for (size_t i = 0; i < kRepeat; ++i) {
            while ((sqe = io_uring_get_sqe(&src_ring)) == nullptr) {
            }
            io_uring_prep_msg_ring(sqe, dest_ring.enter_ring_fd, 0, 0, 0);
            io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
            if ((i % submit_batch == submit_batch - 1) || i == kRepeat - 1) {
                ret = io_uring_submit(&src_ring);
                if (ret < 0) {
                    LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
                }
            }
        }
        const auto submission_finish = steady_clock::now();
        t.join();
        const auto processing_finish = steady_clock::now();
        const auto iteration_time = duration_cast<duration<double>>(processing_finish - start);
        s.SetIterationTime(iteration_time.count());
        s.counters["op/s"] = static_cast<double>(kRepeat) / iteration_time.count();
    }
}

static void BenchRingMsgWait(benchmark::State& s) { BenchRingMsg(s, ConsumeMethod::kWait); }

static void BenchRingMsgPeek(benchmark::State& s) { BenchRingMsg(s, ConsumeMethod::kPeek); }

static void BenchRingMsgTimeoutWait(benchmark::State& s) {
    BenchRingMsg(s, ConsumeMethod::kTimeoutWait);
}

BENCHMARK(BenchRingMsgWait)
  ->UseManualTime()
  ->Setup(SetupRingPair)
  ->Teardown(TeardownRingPair)
  ->RangeMultiplier(2)
  ->Range(1, 1 << 12);

BENCHMARK(BenchRingMsgPeek)
  ->UseManualTime()
  ->Setup(SetupRingPair)
  ->Teardown(TeardownRingPair)
  ->RangeMultiplier(2)
  ->Range(1, 1 << 12);

BENCHMARK(BenchRingMsgTimeoutWait)
  ->UseManualTime()
  ->Setup(SetupRingPair)
  ->Teardown(TeardownRingPair)
  ->RangeMultiplier(2)
  ->Range(1, 1 << 12);

BENCHMARK(BenchRingMsgWait)
  ->Name("BenchRingMsgWaitSqpoll")
  ->UseManualTime()
  ->Setup(SetupRingPairSqpoll)
  ->Teardown(TeardownRingPair)
  ->Arg(1);

BENCHMARK(BenchRingMsgPeek)
  ->Name("BenchRingMsgPeekSqpoll")
  ->UseManualTime()
  ->Setup(SetupRingPairSqpoll)
  ->Teardown(TeardownRingPair)
  ->Arg(1);

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
