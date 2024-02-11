#include "server.h"

#include "client.h"
#include "client_manager.h"
#include "io/listener.h"
#include "runtime/ring_executor.h"
#include "runtime/util.h"
#include "service/command_registry.h"
#include "service/data_structure_service.h"
#include "sys/util.h"

#include <chrono>
#include <thread>

namespace rdss {

Server::Server(Config config)
  : config_(std::move(config))
  , clock_(std::make_unique<Clock>(true))
  , dss_executor_(std::make_unique<RingExecutor>(
      "dss_executor", RingConfig{.sqpoll = config_.sqpoll, .async_sqe = false}))
  , client_manager_(std::make_unique<ClientManager>()) {
    // TODO: Into init list.
    client_executors_.reserve(config_.client_executors);
    for (size_t i = 0; i < config_.client_executors; ++i) {
        client_executors_.emplace_back(std::make_unique<RingExecutor>(
          "client_executor_" + std::to_string(i), RingConfig{.sqpoll = config_.sqpoll}));
    }

    listener_ = Listener::Create(config_.port, client_executors_[0].get());

    std::promise<void> shutdown_promise;
    shutdown_future_ = shutdown_promise.get_future();
    service_ = std::make_unique<DataStructureService>(
      &config_, this, clock_.get(), std::move(shutdown_promise));
    RegisterCommands(service_.get());
}

Task<void> Server::AcceptLoop(RingExecutor* this_exr) {
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
        client->Process(this_exr, dss_executor_.get());
    }
}

Server::~Server() = default;

// TODO: Adaptive hz
Task<void> Server::Cron() {
    const auto interval_in_millisecond = 1000 / config_.hz;
    while (active_) {
        co_await dss_executor_->Timeout(std::chrono::milliseconds(interval_in_millisecond));
        service_->Cron();
    }
}

void Server::Run() {
    SetNofileLimit(std::numeric_limits<uint16_t>::max());
    stats_.start_time = clock_->Now();

    io_uring src_ring;
    auto ret = io_uring_queue_init(16, &src_ring, 0);
    if (ret) {
        LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
    }

    ScheduleOn(&src_ring, dss_executor_.get(), [this]() { this->Cron(); });
    ScheduleOn(&src_ring, client_executors_[0].get(), [this, exr = client_executors_[0].get()]() {
        this->AcceptLoop(exr);
    });

    io_uring_queue_exit(&src_ring);

    shutdown_future_.wait();
    LOG(INFO) << "Shutting down the server";
    Shutdown();
}

void Server::Shutdown() {
    LOG(INFO) << "Stopping executors.";

    io_uring ring;
    int ret = io_uring_queue_init(16, &ring, 0);
    if (ret) {
        LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
    }

    for (auto& e : client_executors_) {
        e->Deactivate(&ring);
    }
    dss_executor_->Deactivate(&ring);

    io_uring_queue_exit(&ring);

    dss_executor_->Shutdown();
    for (auto& e : client_executors_) {
        e->Shutdown();
    }

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
