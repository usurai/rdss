#include "client_manager.h"

#include "client.h"
#include "io/connection.h"

namespace rdss {

namespace detail {

size_t ConnectionToIndex(Connection* conn) { return static_cast<size_t>(conn->GetFD()) - 1; }

} // namespace detail

Client* ClientManager::AddClient(Connection* conn, DataStructureService* service) {
    const size_t index = detail::ConnectionToIndex(conn);
    if (index >= clients_.size()) {
        std::lock_guard l(mu_);
        clients_.resize(2 * index);
    }
    assert(clients_[index] == nullptr);

    active_clients_.fetch_add(1);
    clients_[index] = new Client(conn, this, service);
    return clients_[index];
}

void ClientManager::RemoveClient(Connection* conn) {
    std::lock_guard l(mu_);
    const size_t index = detail::ConnectionToIndex(conn);
    assert(clients_.size() > index && clients_[index] != nullptr);
    clients_[index] = nullptr;
    active_clients_.fetch_sub(1, std::memory_order_release);
}

void ClientManager::UpdateInputBufferSize(uint64_t s) {
    auto cur = max_input_buffer_.load(std::memory_order_relaxed);
    while (s > cur && !max_input_buffer_.compare_exchange_weak(cur, s)) {
    }
}

void ClientManager::UpdateOutputBufferSize(uint64_t s) {
    auto cur = max_output_buffer_.load(std::memory_order_relaxed);
    while (s > cur && !max_output_buffer_.compare_exchange_weak(cur, s)) {
    }
}

} // namespace rdss
