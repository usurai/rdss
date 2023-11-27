#pragma once

#include <atomic>
#include <vector>
#include <mutex>

namespace rdss {

class Client;
class Connection;
class DataStructureService;

class ClientManager {
public:
    Client* AddClient(Connection* conn, DataStructureService* service);

    void RemoveClient(Connection* conn);

    std::vector<Client*>& GetClients() { return clients_; }

    size_t ActiveClients() const { return active_clients_.load(); }

private:
    // TODO: mutex
    std::mutex mu_;
    std::vector<Client*> clients_;
    std::atomic<size_t> active_clients_{0};
};

} // namespace rdss
