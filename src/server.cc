#include "server.h"

#include "client.h"
#include "client_manager.h"
#include "io/listener.h"
#include "runtime/ring_executor.h"
#include "service/command_registry.h"
#include "service/data_structure_service.h"
#include "sys/util.h"

#include <chrono>
#include <thread>

namespace rdss {

Server::Server(Config config)
  : config_(std::move(config))
  , clock_(std::make_unique<Clock>(true))
  , dss_executor_(std::make_unique<RingExecutor>("dss_executor", RingConfig{.async_sqe = false}))
  , client_manager_(std::make_unique<ClientManager>()) {
    // TODO: Into init list.
    client_executors_.reserve(ces_);
    for (size_t i = 0; i < ces_; ++i) {
        client_executors_.emplace_back(
          std::make_unique<RingExecutor>("client_executor_" + std::to_string(i)));
    }

    // TODO: Use port in Config.
    listener_ = Listener::Create(6379, client_executors_[0].get());

    std::promise<void> shutdown_promise;
    shutdown_future_ = shutdown_promise.get_future();
    service_ = std::make_unique<DataStructureService>(
      &config_, this, clock_.get(), std::move(shutdown_promise));
    RegisterCommands(service_.get());
}

Task<void> Server::AcceptLoop(RingExecutor* executor) {
    co_await ResumeOn(executor);

    size_t ce_index{0};
    while (active_) {
        auto conn = co_await listener_->Accept(client_executors_[ce_index].get());
        stats_.connections_received.fetch_add(1, std::memory_order_relaxed);
        if (client_manager_->ActiveClients() == config_.maxclients) {
            stats_.rejected_connections.fetch_add(1, std::memory_order_relaxed);
            conn->Close();
            delete conn;
            continue;
        }
        ce_index = (ce_index + 1) % client_executors_.size();
        auto* client = client_manager_->AddClient(conn, service_.get());
        client->Process(executor, dss_executor_.get());
    }
}

Server::~Server() = default;

// Consider move to dss
// TODO: Adaptive hz
Task<void> Server::Cron() {
    co_await ResumeOn(dss_executor_.get());

    const auto interval_in_millisecond = 1000 / config_.hz;
    while (active_) {
        co_await dss_executor_->Timeout(std::chrono::milliseconds(interval_in_millisecond));

        service_->GetEvictor().RefreshLRUClock();
        service_->GetExpirer().ActiveExpire();
        service_->IncrementalRehashing(std::chrono::milliseconds{1});
    }
}

void Server::Run() {
    SetNofileLimit(std::numeric_limits<uint16_t>::max());
    stats_.start_time = clock_->Now();

    Cron();

    AcceptLoop(client_executors_[0].get());

    shutdown_future_.wait();
    LOG(INFO) << "Shutting down the server";
    Shutdown();
}

void Server::Shutdown() {
    LOG(INFO) << "Stopping executors.";
    io_uring ring;
    int ret;
    ret = io_uring_queue_init(128, &ring, 0);
    if (ret) {
        LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
    }
    std::vector<int> fds;
    for (auto& e : client_executors_) {
        e->Deactivate();
        fds.push_back(e->Ring()->enter_ring_fd);
    }
    dss_executor_->Deactivate();
    fds.push_back(dss_executor_->Ring()->enter_ring_fd);

    for (auto fd : fds) {
        auto sqe = io_uring_get_sqe(&ring);
        if (sqe == nullptr) {
            LOG(FATAL) << "io_uring_get_sqe";
        }
        io_uring_prep_msg_ring(sqe, fd, 0, 0, 0);
        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
    }
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        LOG(FATAL) << "io_uring_submit:" << strerror(-ret);
    }
    dss_executor_->Shutdown();
    for (auto& e : client_executors_) {
        e->Shutdown();
    }
    io_uring_queue_exit(&ring);

    LOG(INFO) << "Closing active connections.";
    auto clients = client_manager_->GetClients();
    for (auto client : clients) {
        if (client != nullptr) {
            client->Close();
        }
    }
    assert(client_manager_->ActiveClients() == 0);
}

} // namespace rdss
