#pragma once

#include "cancellation.h"
#include "connection.h"
#include "data_structure_service.h"
#include "promise.h"

#include <glog/logging.h>

namespace rdss {

class ClientManager;

class Client {
public:
    explicit Client(Connection* conn, ClientManager* manager, DataStructureService* service)
      : conn_(std::unique_ptr<Connection>(conn))
      , manager_(manager)
      , service_(service) {
        VLOG(1) << "Client::ctor()";
    }

    ~Client() { VLOG(1) << "Client::dtor()"; }

    Task<void> Echo();
    Task<void> Process();

    void Disconnect();

private:
    void OnConnectionClose() { delete this; }

    std::unique_ptr<Connection> conn_;
    ClientManager* manager_;
    DataStructureService* service_;
    CancellationToken cancel_token_;
};

} // namespace rdss
