#pragma once

#include "connection.h"
#include "glog/logging.h"
#include "promise.h"

namespace rdss {

class Client {
public:
    explicit Client(Connection* conn)
      : conn_(std::unique_ptr<Connection>(conn)) {}

    Task<void> Echo();

private:
    void OnConnectionClose() { delete this; }

    std::unique_ptr<Connection> conn_;
};

} // namespace rdss
