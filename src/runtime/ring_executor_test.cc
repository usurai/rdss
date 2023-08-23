#include "io/promise.h"
#include "ring_executor.h"

#include <glog/logging.h>

#include <charconv>
#include <condition_variable>
#include <iostream>
#include <mutex>

using namespace rdss;

std::mutex mu;
size_t tasks;
std::atomic<size_t> finished = 0;
bool ready = false;
std::condition_variable cv;

Task<void> TestResume(RingExecutor* re) {
    std::cout << "from " << std::this_thread::get_id() << std::endl;
    co_await ResumeOn(re);
    std::cout << "to " << std::this_thread::get_id() << std::endl;
}

Task<void> PingPong(RingExecutor* main, RingExecutor* re1, RingExecutor* re2, size_t repeat) {
    size_t cnt{0};
    co_await Transfer(main, re2);
    while (cnt < repeat) {
        co_await Transfer(re2, re1);
        co_await Transfer(re1, re2);
        ++cnt;
    }

    if (finished.fetch_add(1, std::memory_order_acq_rel) == tasks - 1) {
        std::lock_guard<std::mutex> l(mu);
        LOG(INFO) << "Finished ping pong as " << finished;
        ready = true;
        cv.notify_one();
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        return 1;
    }

    RingExecutor re;
    RingExecutor re1;
    RingExecutor re2;

    size_t repeat;
    std::from_chars(argv[1], argv[1] + strlen(argv[1]), tasks);
    std::from_chars(argv[2], argv[2] + strlen(argv[2]), repeat);
    LOG(INFO) << "Tasks: " << tasks << ", will repeat " << repeat;

    for (size_t i = 0; i < tasks; ++i) {
        PingPong(&re, &re1, &re2, repeat);
    }

    std::unique_lock l(mu);
    cv.wait(l, []() { return finished == tasks; });
    LOG(INFO) << "Ready, shutting down";

    LOG(INFO) << "SHUTDOWNING";
    re.Shutdown();
    re1.Shutdown();
    re2.Shutdown();
    LOG(INFO) << "SHUTDOWN";

    return 0;
}

int main1() {
    RingExecutor re;
    TestResume(&re);
    re.Shutdown();
    return 0;
}
