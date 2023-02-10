#include "server.h"

namespace rdss {

Server::Server()
  : processor_(AsyncOperationProcessor::Create())
  , listener_(Listener::Create(6379, processor_.get()))
  , proactor_(std::make_unique<Proactor>(processor_->GetRing()))
  , service_(std::make_unique<DataStructureService>()) {}

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

} // namespace rdss
