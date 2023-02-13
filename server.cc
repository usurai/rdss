#include "server.h"

#include "client.h"
#include "command.h"
#include "string_commands.h"

namespace rdss {

Server::Server()
  : processor_(AsyncOperationProcessor::Create())
  , listener_(Listener::Create(6379, processor_.get()))
  , proactor_(std::make_unique<Proactor>(processor_->GetRing()))
  , service_(std::make_unique<DataStructureService>()) {
    RegisterCommands();
}

void Server::Run() {
    AcceptLoop();
    proactor_->Run();
}

Task<void> Server::AcceptLoop() {
    while (true) {
        auto conn = co_await listener_->Accept(/*cancel_token*/);
        auto client = new Client(conn, service_.get());
        client->Process();
    }
}

void Server::RegisterCommands() {
    service_->RegisterCommand("SET", Command("SET").SetHandler(SetFunction));
    service_->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
}

} // namespace rdss
