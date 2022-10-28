#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <cassert>
#include <climits>
#include <cstring>
#include <iostream>
#include <liburing.h>
#include <queue>
#include <vector>

constexpr size_t QD = 1024;
constexpr size_t wrings = 2;
constexpr size_t rrings = 1;
constexpr size_t MAX_CONNECTION = 4096;
constexpr size_t BUFFER_SIZE = 1024;

std::vector<io_uring> write_rings;
std::vector<io_uring> read_rings;
std::vector<std::array<char, BUFFER_SIZE>> buffers(MAX_CONNECTION);
std::vector<size_t> read_sizes(MAX_CONNECTION);
std::vector<int> conn_fds(MAX_CONNECTION);

std::queue<size_t> free_conns;
size_t next_rring{0};
size_t next_wring{0};

int SetupListening();
void QueueMultishotAccept(io_uring* ring, int socket);
io_uring NewPollingRing();
void AddRings(io_uring* agg);
size_t NextReadRing();
size_t NextWriteRing();
void QueueRead(size_t conn);
void QueueWrite(size_t conn);
void QueueClose(io_uring* ring, size_t conn);

int main() {
    auto agg = NewPollingRing();

    const auto listen_sock = SetupListening();
    if (listen_sock == 0) {
        return 1;
    }
    QueueMultishotAccept(&agg, listen_sock);
    AddRings(&agg);
    io_uring_submit(&agg);

    for (size_t i = 0; i < MAX_CONNECTION; ++i) {
        free_conns.push(i);
    }

    while (true) {
        io_uring_cqe* cqe;
        if (auto ret = io_uring_wait_cqe(&agg, &cqe)) {
            std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << '\n';
            return 1;
        }
        if (cqe->res < 0) {
            std::cerr << "async op: " << strerror(-cqe->res) << '\n';
            return 1;
        }

        const auto ud = cqe->user_data;
        const auto res = cqe->res;
        io_uring_cqe_seen(&agg, cqe);

        if (ud == 1024) {
            assert((!free_conns.empty()) && "Max connection exceeded");
            const auto conn = free_conns.front();
            free_conns.pop();

            const auto fd = res;
            conn_fds[conn] = fd;

            QueueRead(conn);
            // std::cout << "accept " << fd << "\n";
        } else if (ud < wrings) {
            // assert(cqe->flags & IORING_CQE_F_MORE);

            auto* write_ring = &write_rings[ud];
            io_uring_cqe* wcqe;
            assert(!io_uring_peek_cqe(write_ring, &wcqe));

            const auto conn = wcqe->user_data;
            assert(static_cast<size_t>(wcqe->res) == read_sizes[conn]);
            // std::cout << "written " << wcqe->res << " bytes from " << conn_fds[conn] << '\n';

            QueueRead(conn);

            io_uring_cqe_seen(write_ring, wcqe);
            auto sqe = io_uring_get_sqe(&agg);
            assert(sqe != nullptr);
            io_uring_prep_poll_add(sqe, write_ring->ring_fd, POLL_IN);
            io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(ud));
            io_uring_submit(&agg);
        } else if (ud < wrings + rrings) {
            // assert(cqe->flags & IORING_CQE_F_MORE);

            auto* read_ring = &read_rings[ud - wrings];
            io_uring_cqe* rcqe;
            assert(!io_uring_peek_cqe(read_ring, &rcqe));
            const auto conn = rcqe->user_data;
            const size_t bytes_read = static_cast<size_t>(rcqe->res);
            // std::cout << "read " << rcqe->res << " bytes from " << conn_fds[conn] << '\n';
            if (bytes_read == 0) {
                QueueClose(&agg, conn);
                free_conns.push(conn);
            } else {
                read_sizes[conn] = bytes_read;
                QueueWrite(conn);
            }

            io_uring_cqe_seen(read_ring, rcqe);
            auto sqe = io_uring_get_sqe(&agg);
            assert(sqe != nullptr);
            io_uring_prep_poll_add(sqe, read_ring->ring_fd, POLL_IN);
            io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(ud));
            io_uring_submit(&agg);
        } else {
            // close done
            assert(ud == INT_MAX);
        }
    }

    return 0;
}

void QueueRead(size_t conn) {
    auto* read_ring = &read_rings[NextReadRing()];
    auto sqe = io_uring_get_sqe(read_ring);
    assert(sqe != nullptr);
    io_uring_prep_recv(sqe, conn_fds[conn], buffers[conn].data(), BUFFER_SIZE, 0);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(conn));
    io_uring_submit(read_ring);
}

void QueueWrite(size_t conn) {
    auto* write_ring = &write_rings[NextWriteRing()];
    auto sqe = io_uring_get_sqe(write_ring);
    assert(sqe != nullptr);
    io_uring_prep_write(sqe, conn_fds[conn], buffers[conn].data(), read_sizes[conn], 0);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(conn));
    io_uring_submit(write_ring);
}

void QueueClose(io_uring* ring, size_t conn) {
    auto sqe = io_uring_get_sqe(ring);
    assert(sqe != nullptr);
    io_uring_prep_close(sqe, conn_fds[conn]);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(INT_MAX));
    io_uring_submit(ring);
    // std::cout << "close " << conn_fds[conn] << "\n";
}

size_t NextReadRing() {
    const auto res = next_rring++;
    if (next_rring == rrings) {
        next_rring = 0;
    }
    return res;
}

size_t NextWriteRing() {
    const auto res = next_wring++;
    if (next_wring == wrings) {
        next_wring = 0;
    }
    return res;
}

void AddRings(io_uring* agg) {
    for (size_t i = 0; i < wrings; ++i) {
        write_rings.push_back(NewPollingRing());
        auto sqe = io_uring_get_sqe(agg);
        assert(sqe != nullptr);
        // io_uring_prep_poll_multishot(sqe, write_rings[i].ring_fd, POLL_IN);
        io_uring_prep_poll_add(sqe, write_rings[i].ring_fd, POLL_IN);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(i));
    }

    for (size_t i = 0; i < rrings; ++i) {
        read_rings.push_back(NewPollingRing());
        auto sqe = io_uring_get_sqe(agg);
        // io_uring_prep_poll_multishot(sqe, read_rings[i].ring_fd, POLL_IN);
        io_uring_prep_poll_add(sqe, read_rings[i].ring_fd, POLL_IN);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(wrings + i));
    }
}

io_uring NewPollingRing() {
    io_uring ring;
    io_uring_params p = {};
    p.flags |= IORING_SETUP_SQPOLL;
    if (auto ret = io_uring_queue_init_params(QD, &ring, &p)) {
        std::cerr << "io_uring_queue_init_params: " << strerror(-ret) << '\n';
        // TODO
        std::terminate();
    }
    return ring;
}

void QueueMultishotAccept(io_uring* ring, int socket) {
    auto* sqe = io_uring_get_sqe(ring);
    assert(sqe != nullptr);
    // TODO: use direct variant
    io_uring_prep_multishot_accept(sqe, socket, nullptr, nullptr, 0);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(1024));
}

int SetupListening() {
    // socket
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "socket: " << strerror(errno) << '\n';
        return 0;
    }

    int enable{1};
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        std::cerr << "setsockopt" << strerror(errno) << '\n';
        return 0;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8081);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind
    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind: " << strerror(errno) << '\n';
        return 0;
    }

    // listen
    if (listen(sock, 1000) < 0) {
        std::cerr << "listen: " << strerror(errno) << '\n';
        return 0;
    }

    return sock;
}
