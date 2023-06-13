#pragma once

#include "async_operation_processor.h"
#include "client_manager.h"
#include "clock.h"
#include "config.h"
#include "data_structure_service.h"
#include "listener.h"
#include "proactor.h"
#include "promise.h"

#include <cassert>
#include <memory>

namespace rdss {

class Server {
public:
    explicit Server(Config config);

    void Run();

    void Shutdown();

private:
    Task<void> AcceptLoop();

    Task<void> Cron();

    Config config_;

    bool active_ = true;
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<AsyncOperationProcessor> processor_;
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<Proactor> proactor_;
    std::unique_ptr<DataStructureService> service_;
    std::unique_ptr<ClientManager> client_manager_;
};

} // namespace rdss
