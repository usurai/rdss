#include "util.h"

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include <chrono>
#include <liburing.h>
#include <thread>

using namespace std::chrono;

constexpr size_t kRepeat = 25'000'000;

io_uring src_ring;
io_uring dest_ring;

namespace detail {

template<bool SingIssuer = false>
static void SetupRingPair(const benchmark::State& s) {
    if (s.range(0) != 0) {
        SetupRing<SingIssuer, false>(&src_ring);
        SetupRing<SingIssuer, false>(&dest_ring);
    } else {
        SetupRing<SingIssuer, true>(&src_ring);
        SetupRing<SingIssuer, true>(&dest_ring);
    }
}

static void SetupRingPairSqpoll(const benchmark::State&) {
    SetupRingSqpoll(&src_ring);
    SetupRingSqpoll(&dest_ring);
}

static void TeardownRingPair(const benchmark::State&) {
    io_uring_queue_exit(&src_ring);
    io_uring_queue_exit(&dest_ring);
}

void RingConsumeWait(io_uring* ring) {
    io_uring_cqe* cqe;
    size_t cnt{0};
    while (true) {
        int ret = io_uring_wait_cqe(ring, &cqe);
        if (ret != 0) {
            LOG(FATAL) << "io_uring_wait_cqe:" << strerror(-ret);
        }
        io_uring_cqe_seen(&dest_ring, cqe);
        if (++cnt == kRepeat) {
            return;
        }
    }
}

void RingConsumePeek(io_uring* ring) {
    io_uring_cqe* cqe;
    size_t cnt{0};
    while (true) {
        int ret = io_uring_wait_cqe(ring, &cqe);
        do {
            if (ret != 0) {
                LOG(FATAL) << "io_uring_wait_cqe:" << strerror(-ret);
            }
            io_uring_cqe_seen(&dest_ring, cqe);
            if (++cnt == kRepeat) {
                return;
            }
        } while ((ret = io_uring_peek_cqe(ring, &cqe)) == 0);
    }
}

void RingConsumeTimeoutWait(io_uring* ring) {
    __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = std::chrono::nanoseconds{1'000'000}.count()};

    io_uring_cqe* cqe;
    int ret;
    size_t cnt{0};
    while (true) {
        ret = io_uring_wait_cqe_timeout(ring, &cqe, &ts);
        // TODO: Try io_uring_cq_advance.
        do {
            if (ret == -ETIME) {
                if (io_uring_sq_ready(ring)) {
                    // TOOD: submit_and_wait
                    ret = io_uring_submit(ring);
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
            io_uring_cqe_seen(ring, cqe);

            if (++cnt == kRepeat) {
                return;
            }
        } while (!io_uring_peek_cqe(ring, &cqe));
    }
}

void PrepareRingMsg(io_uring_sqe* sqe) {
    io_uring_prep_msg_ring(sqe, dest_ring.enter_ring_fd, 0, 0, 0);
    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
}

} // namespace detail

enum class ConsumeMethod {
    kWait,
    kPeek,
    kTimeoutWait,
};

auto ConsumeMethodFunction(ConsumeMethod m) {
    switch (m) {
    case ConsumeMethod::kWait:
        return detail::RingConsumeWait;
    case ConsumeMethod::kPeek:
        return detail::RingConsumePeek;
    case ConsumeMethod::kTimeoutWait:
        return detail::RingConsumeTimeoutWait;
    }
}

// Starts a thread consuming 'consume_ring' with function specified by 'consume_method'. Then submit
// 'kRepeat' SQEs prepared by 'prepare_sqe' function to 'submit_ring'. Then waits for consuming
// thread to return. IterationTime is set with elapsed time from the start of submission to the
// return of consuming thread.
template<typename FuncType>
static void BenchRing(
  benchmark::State& s,
  ConsumeMethod consume_method,
  io_uring* submit_ring,
  io_uring* consume_ring,
  FuncType prepare_sqe) {
    for (auto _ : s) {
        std::thread t(ConsumeMethodFunction(consume_method), consume_ring);
        const size_t submit_batch = s.range(0);
        // TODO: get future when thread finishing setup
        std::this_thread::sleep_for(std::chrono::seconds(3));
        const auto start = steady_clock::now();
        io_uring_sqe* sqe;
        uint64_t get_sqe_fail{0};
        for (size_t i = 0; i < kRepeat; ++i) {
            while ((sqe = io_uring_get_sqe(submit_ring)) == nullptr) {
                ++get_sqe_fail;
                LOG(WARNING) << "Unable to get sqe, sleeping...";
                std::this_thread::sleep_for(std::chrono::microseconds{750});
                LOG(WARNING) << "Unable to get sqe after sleeping " << i;
            }
            prepare_sqe(sqe);
            if (submit_batch == 0 || i % submit_batch == submit_batch - 1 || i == kRepeat - 1) {
                auto ret = io_uring_submit(submit_ring);
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
        s.counters["sqe_fail"] = get_sqe_fail;
    }
}

static void Nop(benchmark::State& s) {
    BenchRing(s, ConsumeMethod::kWait, &src_ring, &src_ring, [](io_uring_sqe* sqe) {
        io_uring_prep_nop(sqe);
    });
}

static void BenchRingMsgWait(benchmark::State& s) {
    BenchRing(s, ConsumeMethod::kWait, &src_ring, &dest_ring, detail::PrepareRingMsg);
}

static void BenchRingMsgPeek(benchmark::State& s) {
    BenchRing(s, ConsumeMethod::kPeek, &src_ring, &dest_ring, detail::PrepareRingMsg);
}

static void BenchRingMsgTimeoutWait(benchmark::State& s) {
    BenchRing(s, ConsumeMethod::kTimeoutWait, &src_ring, &dest_ring, detail::PrepareRingMsg);
}

BENCHMARK(Nop)
  ->Name("NopNonSingleIssuer")
  ->UseManualTime()
  ->Setup(detail::SetupRingPair<false>)
  ->Teardown(detail::TeardownRingPair)
  ->RangeMultiplier(4)
  ->Range(0, 1 << 12);

BENCHMARK(Nop)
  ->Name("NopSingleIssuer")
  ->UseManualTime()
  ->Setup(detail::SetupRingPair<true>)
  ->Teardown(detail::TeardownRingPair)
  ->RangeMultiplier(4)
  ->Range(0, 1 << 12);

BENCHMARK(BenchRingMsgWait)
  ->Name("BenchRingMsgWaitSqpoll")
  ->UseManualTime()
  ->Setup(detail::SetupRingPairSqpoll)
  ->Teardown(detail::TeardownRingPair)
  ->Arg(1);

BENCHMARK(BenchRingMsgWait)
  ->UseManualTime()
  ->Setup(detail::SetupRingPair<false>)
  ->Teardown(detail::TeardownRingPair)
  ->RangeMultiplier(4)
  ->Range(1, 1 << 12);

BENCHMARK(BenchRingMsgPeek)
  ->Name("BenchRingMsgPeekSqpoll")
  ->UseManualTime()
  ->Setup(detail::SetupRingPairSqpoll)
  ->Teardown(detail::TeardownRingPair)
  ->Arg(1);

BENCHMARK(BenchRingMsgPeek)
  ->UseManualTime()
  ->Setup(detail::SetupRingPair<false>)
  ->Teardown(detail::TeardownRingPair)
  ->RangeMultiplier(4)
  ->Range(1, 1 << 12);

BENCHMARK(BenchRingMsgTimeoutWait)
  ->UseManualTime()
  ->Setup(detail::SetupRingPair<false>)
  ->Teardown(detail::TeardownRingPair)
  ->RangeMultiplier(4)
  ->Range(1, 1 << 12);

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
