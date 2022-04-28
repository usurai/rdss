#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <liburing.h>
#include <string_view>
#include <vector>

static constexpr size_t QD = 64;
static constexpr size_t READ_SIZE = 4096;

int sock;
io_uring ring;

struct Connection {
    enum class State { Init, Reading, Writting };

    State state{State::Init};
    int fd;
    std::vector<char> read_buffer;
    size_t read_length{0};

    Connection(int fd_)
      : fd(fd_)
      , read_buffer(READ_SIZE) {}
};

void queue_multishot_accept() {
    auto* sqe = io_uring_get_sqe(&ring);
    assert(sqe != nullptr);
    // TODO: use direct variant
    io_uring_prep_multishot_accept(sqe, sock, nullptr, nullptr, 0);
    io_uring_sqe_set_data(sqe, nullptr);
    io_uring_submit(&ring);
}

int main() {
    // socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "socket: " << strerror(errno) << '\n';
        return 1;
    }

    int enable{1};
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        std::cerr << "setsockopt" << strerror(errno) << '\n';
        return 1;
    }

    sockaddr_in addr{0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind
    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind: " << strerror(errno) << '\n';
        return 1;
    }

    // listen
    if (listen(sock, 10) < 0) {
        std::cerr << "listen: " << strerror(errno) << '\n';
        return 1;
    }

    // setup ring
    auto ret = io_uring_queue_init(QD, &ring, 0);
    if (ret) {
        std::cerr << "io_uring_queue_init: " << strerror(-ret) << '\n';
        return 1;
    }

    //  queue accept
    queue_multishot_accept();

    // loop, wait cqe
    while (true) {
        io_uring_cqe* cqe;
        auto ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret) {
            std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << '\n';
            return 1;
        }
        if (cqe->res < 0) {
            std::cerr << "async op: " << strerror(-cqe->res) << '\n';
            return 1;
        }

        // if accept
        //   queue read
        if (cqe->user_data == 0) {
            if (!(cqe->flags & IORING_CQE_F_MORE)) {
                std::cout << "requeueing accept\n";
                queue_multishot_accept();
            }

            auto conn = new Connection(cqe->res);
            auto* sqe = io_uring_get_sqe(&ring);
            assert(sqe != nullptr);
            io_uring_prep_read(sqe, cqe->res, conn->read_buffer.data(), READ_SIZE, 0);
            io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(conn));
            io_uring_submit(&ring);
        } else {
            bool close_conn{false};

            const auto read_len{cqe->res};
            auto* conn = reinterpret_cast<Connection*>(cqe->user_data);
            conn->state = Connection::State::Reading;
            conn->read_length = read_len;
            std::cout << "Read " << conn->read_length << ":"
                      << std::string_view(conn->read_buffer.data(), conn->read_length);
            close_conn = true;

            if (close_conn) {
                close(conn->fd);
                delete conn;
            }
        }
        io_uring_cqe_seen(&ring, cqe);
    }
    //      if read
    //          if not read finish, queue read
    //          else process
    //              queue write
    //      if write
    //          if not finish, queue write
    //          else close connection

    return 0;
}
