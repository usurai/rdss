#include "server.h"

#include "client.h"
#include "command.h"
#include "string_commands.h"

#include <chrono>
#include <thread>

namespace rdss {

Server::Server(Config config)
  : config_(std::move(config))
  , processor_(AsyncOperationProcessor::Create())
  , listener_(Listener::Create(6379, processor_.get()))
  , proactor_(std::make_unique<Proactor>(processor_->GetRing()))
  , service_(std::make_unique<DataStructureService>())
  , client_manager_(std::make_unique<ClientManager>()) {
    RegisterCommands();
}

void Server::Run() {
    AcceptLoop();
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

void Server::RegisterCommands() {
    service_->RegisterCommand("SET", Command("SET").SetHandler(SetFunction));
    service_->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
}

void Server::Shutdown() {
    active_ = false;
    proactor_->Stop();
}

} // namespace rdss
