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

    /// Actively shutdowns the server:
    /// 1. Set every executor's 'active' to false.
    /// 2. Wake every executor's worker thread by ring message so that they can realize the
    /// deactivation.
    /// 3. Call every executor's Shutdown() to blocking wait the worker threads to terminate.
    /// 4. Release all the active clients by close the socket and delete them.
    void Shutdown();

private:
    Task<void> Cron();

    // Operates an accept loop on the 'executor', which should be chosen from the set of
    // 'client_executors_'. Upon the arrival of a new connection, evaluates whether the current
    // active connections surpass the defined limit set by the 'maxclients' configuration. If the
    // threshold is exceeded, the connection is declined. Otherwise, a new client is instantiated
    // with the connection, and the client's processing is scheduled on one of the
    // 'client_executors_' in a round-robin manner.
    Task<void> AcceptLoop(RingExecutor* executor);

    Config config_;

    std::atomic<bool> active_ = true;
    // TODO: Move into dss
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<RingExecutor> dss_executor_;
    // TODO: Make this config or something
    const size_t ces_ = 2;
    std::vector<std::unique_ptr<RingExecutor>> client_executors_;
    std::unique_ptr<Listener> listener_;
    std::future<void> shutdown_future_;
    std::unique_ptr<DataStructureService> service_;
    std::unique_ptr<ClientManager> client_manager_;
};

} // namespace rdss
