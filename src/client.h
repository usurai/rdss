#pragma once

#include "io/cancellation.h"
#include "io/connection.h"
#include "io/promise.h"

namespace rdss {

class ClientManager;
class DataStructureService;
class RingExecutor;

class Client {
public:
    explicit Client(Connection* conn, ClientManager* manager, DataStructureService* service);

    Task<void> Echo(RingExecutor* from);

    Task<void> Process(RingExecutor* from, RingExecutor* dss_executor);

    void Disconnect();

private:
    void OnConnectionClose() { delete this; }

    std::unique_ptr<Connection> conn_;
    ClientManager* manager_;
    DataStructureService* service_;
    CancellationToken cancel_token_;
};

} // namespace rdss
