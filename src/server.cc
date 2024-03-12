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
  , listener_(Listener::Create(config_.port, client_executors_[0].get()))
  , service_(&config_, this, nullptr)
  , shutdown_future_(service_.GetShutdownFuture()) {
}

void Server::Setup() {
    RegisterCommands(&service_);
    SetNofileLimit(std::numeric_limits<uint16_t>::max());
    stats_.start_time = Clock(true).Now();

    auto ret = io_uring_queue_init(16, &ring_, 0);
    if (ret) {
        LOG(FATAL) << "io_uring_queue_init:" << strerror(-ret);
    }
    if (config_.use_ring_buffer) {
        SetupInitBufRing(&ring_, client_executors_);
    }
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
    // TODO: Change to something like
    // 1. target_exr.Schedule(&ring_, func)
    // 2. target_exr.ScheduleRepeat(&ring_, interval, func);
    ScheduleOn(&ring_, dss_executor_.get(), [dss = &service_, exr = dss_executor_.get()]() {
        dss->Cron(exr);
    });
    ScheduleOn(&ring_, client_executors_[0].get(), [this, exr = client_executors_[0].get()]() {
        this->AcceptLoop(exr);
    });

    shutdown_future_.wait();
    Shutdown();
}

void Server::Shutdown() {
    LOG(INFO) << "Shutting down the server";
    LOG(INFO) << "Stopping executors.";

    for (auto& e : client_executors_) {
        e->Deactivate(&ring_);
    }
    dss_executor_->Deactivate(&ring_);

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

    io_uring_queue_exit(&ring_);
}

} // namespace rdss
