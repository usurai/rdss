#include "server.h"

#include "client.h"
#include "command.h"
#include "command_registry.h"

#include <sys/resource.h>

#include <chrono>
#include <thread>

namespace rdss {

Server::Server(Config config)
  : config_(std::move(config))
  , clock_(std::make_unique<Clock>(true))
  , dss_executor_(std::make_unique<RingExecutor>("dss_executor", RingConfig{.async_sqe = false}))
  , service_(std::make_unique<DataStructureService>(&config_, clock_.get()))
  , client_manager_(std::make_unique<ClientManager>()) {
    // TODO: Consider move into DSS.
    RegisterCommands(service_.get());

    // TODO: Into init list.
    client_executors_.reserve(ces_);
    for (size_t i = 0; i < ces_; ++i) {
        client_executors_.emplace_back(
          std::make_unique<RingExecutor>("client_executor_" + std::to_string(i)));
    }

    // TODO: Use port in Config.
    listener_ = Listener::Create(6379, client_executors_[0].get());
}

Task<void> Server::AcceptLoop(RingExecutor* executor, std::promise<void> promise) {
    co_await ResumeOn(executor);

    size_t ce_index{0};
    while (active_) {
        auto conn = co_await listener_->Accept(client_executors_[ce_index].get());
        if (client_manager_->ActiveClients() == config_.maxclients) {
            conn->Close();
            delete conn;
            continue;
        }
        ce_index = (ce_index + 1) % client_executors_.size();
        auto* client = client_manager_->AddClient(conn, service_.get());
        client->Process(executor, dss_executor_.get());
    }
    promise.set_value();
}

// Consider move to dss
// TODO: Adaptive hz
Task<void> Server::Cron() {
    co_await ResumeOn(dss_executor_.get());

    const auto interval_in_millisecond = 1000 / config_.hz;
    while (active_) {
        co_await dss_executor_->Timeout(std::chrono::milliseconds(interval_in_millisecond));

        service_->RefreshLRUClock();
        service_->ActiveExpire();
    }
}

void Server::Run() {
    // TODO: Make this a util function
    rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim)) {
        LOG(FATAL) << "getrlimit";
    }
    LOG(INFO) << "NOFILE rlimit: " << rlim.rlim_cur << ' ' << rlim.rlim_max;
    rlim.rlim_cur = 1024 * 20;
    rlim.rlim_max = 1024 * 20;
    if (setrlimit(RLIMIT_NOFILE, &rlim)) {
        LOG(FATAL) << "setrlimit";
    }
    if (getrlimit(RLIMIT_NOFILE, &rlim)) {
        LOG(FATAL) << "getrlimit";
    }
    LOG(INFO) << "NOFILE rlimit after modification: " << rlim.rlim_cur << ' ' << rlim.rlim_max;

    Cron();

    std::promise<void> promise;
    auto future = promise.get_future();
    AcceptLoop(client_executors_[0].get(), std::move(promise));
    future.wait();
}

} // namespace rdss
