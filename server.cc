#include "server.h"

#include "client.h"
#include "command.h"
#include "string_commands.h"

#include <chrono>
#include <thread>

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
    // Temporary: With one listening connection, when a new connection comes in, close the old
    // connection by cancelling recv. Also, this assumes that the connection can only be closed from
    // server side, the client closing the connection can cause UB.
    Client* current_client{nullptr};
    size_t served{0};
    while (active_) {
        auto conn = co_await listener_->Accept(/*cancel_token*/);
        if (current_client != nullptr) {
            current_client->Disconnect();
        }

        if (++served == 3) {
            conn->Close();
            delete conn;
            Shutdown();
            std::this_thread::sleep_for(std::chrono::seconds(3));
            co_return;
        }
        current_client = new Client(conn, service_.get());
        current_client->Process();
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
