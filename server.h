#pragma once

#include "async_operation_processor.h"
#include "client_manager.h"
#include "data_structure_service.h"
#include "listener.h"
#include "proactor.h"
#include "promise.h"

#include <cassert>
#include <memory>

namespace rdss {

class Server {
public:
    Server();

    void Run();

    void Shutdown();

private:
    Task<void> AcceptLoop();

    void RegisterCommands();

    bool active_ = true;
    std::unique_ptr<AsyncOperationProcessor> processor_;
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<Proactor> proactor_;
    std::unique_ptr<DataStructureService> service_;
    std::unique_ptr<ClientManager> client_manager_;
};

} // namespace rdss
