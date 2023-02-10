#pragma once

#include "connection.h"
#include "data_structure_service.h"
#include "promise.h"

namespace rdss {

class Client {
public:
    explicit Client(Connection* conn, DataStructureService* service)
      : conn_(std::unique_ptr<Connection>(conn))
      , service_(service) {}

    Task<void> Echo();
    Task<void> Process();

private:
    void OnConnectionClose() { delete this; }

    std::unique_ptr<Connection> conn_;
    DataStructureService* service_;
};

} // namespace rdss
