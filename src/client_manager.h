#pragma once

#include <vector>

namespace rdss {

class Client;
class Connection;
class DataStructureService;

class ClientManager {
public:
    Client* AddClient(Connection* conn, DataStructureService* service);
    void RemoveClient(Connection* conn);
    size_t ActiveClients() const { return active_clients_; }

private:
    std::vector<Client*> clients_;
    size_t active_clients_{0};
};

} // namespace rdss
