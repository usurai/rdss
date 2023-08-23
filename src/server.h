#pragma once

#include "base/clock.h"
#include "client_manager.h"
#include "config.h"
#include "data_structure_service.h"
#include "io/listener.h"
#include "io/promise.h"
#include "runtime/ring_executor.h"

#include <atomic>
#include <future>
#include <memory>

namespace rdss {

class Server {
public:
    explicit Server(Config config);

    void Run();

    void Shutdown() { active_.store(false); }

private:
    Task<void> Cron();

    // Operates an accept loop on the 'executor', which should be chosen from the set of
    // 'client_executors_'. Upon the arrival of a new connection, evaluates whether the current
    // active connections surpass the defined limit set by the 'maxclients' configuration. If the
    // threshold is exceeded, the connection is declined. Otherwise, a new client is instantiated
    // with the connection, and the client's processing is scheduled on one of the
    // 'client_executors_' in a round-robin manner.
    Task<void> AcceptLoop(RingExecutor* executor, std::promise<void> promise);

    Task<void> Setup();

    Config config_;

    std::atomic<bool> active_ = true;
    // TODO: Move into dss
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<RingExecutor> dss_executor_;
    // TODO: Make this config or something
    const size_t ces_ = 2;
    std::vector<std::unique_ptr<RingExecutor>> client_executors_;
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<DataStructureService> service_;
    std::unique_ptr<ClientManager> client_manager_;
};

} // namespace rdss
