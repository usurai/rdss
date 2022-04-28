#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <liburing.h>

static constexpr size_t QD = 64;

int sock;
io_uring ring;

void queue_multishot_accept() {
    auto* sqe = io_uring_get_sqe(&ring);
    assert(sqe != nullptr);
    // TODO: use direct variant
    io_uring_prep_multishot_accept(sqe, sock, nullptr, nullptr, 0);
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
            std::cerr << "async accept: " << strerror(errno) << '\n';
            return 1;
        }
        std::cout << "accepted\n";
        close(cqe->res);
        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            std::cout << "requeueing accept\n";
            queue_multishot_accept();
        }
        io_uring_cqe_seen(&ring, cqe);
    }
    //      if accept
    //          queue accept
    //          queue read
    //      if read
    //          if not read finish, queue read
    //          else process
    //              queue write
    //      if write
    //          if not finish, queue write
    //          else close connection

    return 0;
}
