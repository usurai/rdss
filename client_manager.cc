#include "client_manager.h"

#include "client.h"
#include "connection.h"

namespace rdss {

namespace detail {

size_t ConnectionToIndex(Connection* conn) { return static_cast<size_t>(conn->GetFD()) - 1; }

} // namespace detail

Client* ClientManager::AddClient(Connection* conn, DataStructureService* service) {
    const size_t index = detail::ConnectionToIndex(conn);
    if (index >= clients_.size()) {
        clients_.resize(index + 1);
    }
    assert(clients_[index] == nullptr);

    clients_[index] = new Client(conn, this, service);
    ++active_clients_;
    return clients_[index];
}

void ClientManager::RemoveClient(Connection* conn) {
    const size_t index = detail::ConnectionToIndex(conn);
    assert(clients_.size() > index && clients_[index] != nullptr);
    clients_[index] = nullptr;
    --active_clients_;
}

} // namespace rdss
