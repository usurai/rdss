#pragma once

#include <atomic>
#include <mutex>
#include <vector>

namespace rdss {

class Client;
class Connection;
class DataStructureService;

struct ClientStats {
    std::atomic<uint64_t> max_input_buffer{};
    std::atomic<uint64_t> max_output_buffer{};
    std::atomic<uint64_t> net_input_bytes{};
    std::atomic<uint64_t> net_output_bytes{};

    void UpdateInputBufferSize(uint64_t s);
    void UpdateOutputBufferSize(uint64_t s);
};

class ClientManager {
public:
    Client* AddClient(Connection* conn, DataStructureService* service);

    void RemoveClient(Connection* conn);

    std::vector<Client*>& GetClients() { return clients_; }

    size_t ActiveClients() const { return active_clients_.load(); }

    ClientStats& Stats() { return stats_; }

private:
    std::mutex mu_;
    std::vector<Client*> clients_;
    std::atomic<size_t> active_clients_{0};

    ClientStats stats_;
};

} // namespace rdss
