#include "server.h"

#include "client.h"
#include "runtime/util.h"
#include "service/command_registry.h"
#include "sys/util.h"

#include <chrono>
#include <thread>

namespace rdss {

Server::Server(Config config)
  : config_(std::move(config))
  , dss_executor_(RingExecutor::Create(0, "dss_exr", config_))
  , client_executors_(RingExecutor::Create(config.client_executors, 1, "cli_exr_", config_))
  , service_(&config_, this, nullptr)
  , shutdown_future_(service_.GetShutdownFuture()) {
    listener_ = Listener::Create(config_.port, client_executors_[0].get());

    RegisterCommands(&service_);
}

Task<void> Server::AcceptLoop(RingExecutor* this_exr) {
    size_t ce_index{0};
    while (active_) {
        auto conn = co_await listener_->Accept(client_executors_[ce_index].get());
        stats_.connections_received.fetch_add(1, std::memory_order_relaxed);
        if (client_manager_.ActiveClients() == config_.maxclients) {
            stats_.rejected_connections.fetch_add(1, std::memory_order_relaxed);
            // TODO: dtor will close
            conn->Close();
            delete conn;
            continue;
        }
        ce_index = (ce_index + 1) % client_executors_.size();
        auto* client = client_manager_.AddClient(conn, &service_);
        client->Process(this_exr, dss_executor_.get(), config_.use_ring_buffer);
    }
}

void Server::Run() {
    SetNofileLimit(std::numeric_limits<uint16_t>::max());
    stats_.start_time = Clock(true).Now();

    io_uring src_ring;
    auto ret = io_uring_queue_init(16, &src_ring, 0);
    if (ret) {
        LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
    }

    if (config_.use_ring_buffer) {
        SetupInitBufRing(&src_ring, client_executors_);
    }

    // TODO: Change to something like
    // 1. target_exr.Schedule(&src_ring, func)
    // 2. target_exr.ScheduleRepeat(&src_ring, interval, func);
    ScheduleOn(&src_ring, dss_executor_.get(), [dss = &service_, exr = dss_executor_.get()]() {
        dss->Cron(exr);
    });
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
    auto clients = client_manager_.GetClients();
    for (auto client : clients) {
        if (client != nullptr) {
            client->Close();
        }
    }
    assert(client_manager_.ActiveClients() == 0);
}

} // namespace rdss
