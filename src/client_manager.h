#pragma once

#include <atomic>
#include <mutex>
#include <vector>

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

    void UpdateInputBufferSize(uint64_t s);

    void UpdateOutputBufferSize(uint64_t s);

    uint64_t GetMaxInputBuffer() const { return max_input_buffer_.load(std::memory_order_relaxed); }

    uint64_t GetMaxOutputBuffer() const {
        return max_output_buffer_.load(std::memory_order_relaxed);
    }

private:
    std::mutex mu_;
    std::vector<Client*> clients_;
    std::atomic<size_t> active_clients_{0};

    std::atomic<uint64_t> max_input_buffer_{};
    std::atomic<uint64_t> max_output_buffer_{};
};

} // namespace rdss
