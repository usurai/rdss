#include "server.h"

#include "async_operation.h"
#include "client.h"
#include "command.h"
#include "string_commands.h"

#include <chrono>
#include <thread>

namespace detail {

int64_t GetLruClock() {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    return epoch.count();
}

} // namespace detail

namespace rdss {

Server::Server(Config config)
  : config_(std::move(config))
  , processor_(AsyncOperationProcessor::Create())
  , listener_(Listener::Create(6379, processor_.get()))
  , proactor_(std::make_unique<Proactor>(processor_->GetRing()))
  , service_(std::make_unique<DataStructureService>(&config_))
  , client_manager_(std::make_unique<ClientManager>()) {
    RegisterCommands();
}

void Server::Run() {
    AcceptLoop();
    Cron();
    proactor_->Run();
}

Task<void> Server::AcceptLoop() {
    while (active_) {
        auto conn = co_await listener_->Accept(/*cancel_token*/);
        if (client_manager_->ActiveClients() == config_.maxclients) {
            conn->Close();
            delete conn;
            continue;
        }
        auto* client = client_manager_->AddClient(conn, service_.get());
        client->Process();
    }
}

// TODO: Adaptive hz
Task<void> Server::Cron() {
    const auto interval_in_millisecond = 1000 / config_.hz;
    while (active_) {
        co_await AwaitableTimeout(
          processor_.get(), std::chrono::milliseconds(interval_in_millisecond));

        service_->lru_clock_ = detail::GetLruClock();
    }
}

void Server::RegisterCommands() {
    service_->RegisterCommand("SET", Command("SET").SetHandler(SetFunction).SetIsWriteCommand());
    service_->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
}

void Server::Shutdown() {
    active_ = false;
    proactor_->Stop();
}

} // namespace rdss
