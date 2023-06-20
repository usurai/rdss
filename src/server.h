#pragma once

#include "base/clock.h"
#include "client_manager.h"
#include "config.h"
#include "data_structure_service.h"
#include "io/async_operation_processor.h"
#include "io/proactor.h"
#include "io/promise.h"
#include "listener.h"

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
