#include "listener.h"
#include "promise.h"

#include <glog/logging.h>

#include <iostream>

using namespace rdss;

Task<void> AcceptLoop(Listener* listener, RingExecutor* client_executor) {
    while (true) {
        auto [err, conn] = co_await listener->Accept(client_executor);
        if (err) {
            LOG(ERROR) << "accept:" << err.message();
            continue;
        }
        LOG(INFO) << "new conn";
        conn->Close();
        delete conn;
    }
}

int main() {
    RingExecutor re;
    auto listener = Listener::Create(6379, &re);
    AcceptLoop(listener.get(), &re);

    int a;
    std::cin >> a;

    return 0;
}
