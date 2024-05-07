// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include "base/clock.h"
#include "base/config.h"
#include "client_manager.h"
#include "io/listener.h"
#include "io/promise.h"
#include "runtime/ring_executor.h"
#include "service/data_structure_service.h"

#include <atomic>
#include <future>
#include <memory>
#include <vector>

namespace rdss {

struct ServerStats {
    Clock::TimePoint start_time;
    std::atomic<uint64_t> connections_received{};
    std::atomic<uint64_t> rejected_connections{};
};

class Server {
public:
    explicit Server(Config config);

    /// 1. Registers commands to 'service_'.
    /// 2. Tries to set limit of open file to 65536.
    /// 3. Update start time of 'stats_'.
    /// 4. Initialize 'ring_' that is used to send messages to executors.
    /// 5. Setup buffer ring of client executors if enabled.
    void Setup();

    /// Blocking waits for 'service_' to shutdown.
    void Run();

    /// Actively shutdowns the server:
    /// 1. Set every executor's 'active' to false.
    /// 2. Wake every executor's worker thread by ring message so that they can realize the
    /// deactivation.
    /// 3. Call every executor's Shutdown() to blocking wait the worker threads to terminate.
    /// 4. Release all the active clients by close the socket and delete them.
    void Shutdown();

    ClientManager* GetClientManager() { return &client_manager_; }

    ServerStats& Stats() { return stats_; }

private:
    // Operates an accept loop on RingExecutor, which should be chosen from the set of
    // 'client_executors_'. Upon the arrival of a new connection, evaluates whether the current
    // active connections surpass the defined limit set by the 'maxclients' configuration. If the
    // threshold is exceeded, the connection is declined. Otherwise, a new client is instantiated
    // with the connection, and the client's processing is scheduled on one of the
    // 'client_executors_' in a round-robin manner.
    Task<void> AcceptLoop();

    Config config_;

    std::atomic<bool> active_ = true;
    std::unique_ptr<RingExecutor> dss_executor_;
    std::vector<std::unique_ptr<RingExecutor>> client_executors_;
    std::unique_ptr<Listener> listener_;
    DataStructureService service_;
    std::future<void> shutdown_future_;
    ClientManager client_manager_;
    ServerStats stats_;
    io_uring ring_;
};

} // namespace rdss
