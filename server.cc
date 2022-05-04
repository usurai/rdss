#include "server.h"

#include "client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <liburing.h>

using rdss::Client;

int sock;
io_uring ring;

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
    auto ret = io_uring_queue_init(rdss::QD, &ring, 0);
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
        // TODO: don't quit here
        if (cqe->res < 0) {
            std::cerr << "async op: " << strerror(-cqe->res) << '\n';
            return 1;
        }

        // if accept, create client and queue read
        if (cqe->user_data == 0) {
            if (!(cqe->flags & IORING_CQE_F_MORE)) {
                std::cout << "requeueing accept\n";
                queue_multishot_accept();
            }

            auto client = new Client(&ring, cqe->res);
            // TODO: handle the case of too manu queued
            assert(client->QueueRead());
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        auto* client = reinterpret_cast<Client*>(cqe->user_data);
        io_uring_cqe_seen(&ring, cqe);

        bool close_client{false};
        if (client->Reading()) {
            // std::cout << "Request(" << client->read_length
            //           << "):" << std::string_view(client->query_buffer.data(),
            //           client->read_length);
            client->IncreaseReadLength(cqe->res);
            // parse buffer: query_buffer->args
            const auto parse_result = client->ParseBuffer();
            switch (parse_result) {
            //  1. error: reply error, close
            case Client::ParseResult::Error:
                client->SetError();
                client->Reply("Parse error.\n");
                continue;
            //  2. not enough read: extent buffer, queue read again
            case Client::ParseResult::NeedsMore:
                if (!client->ExpandQueryBuffer()) {
                    client->SetError();
                    client->Reply("Exceed read limit.");
                } else {
                    client->QueueRead();
                }
                continue;
            //  3. done
            case Client::ParseResult::Success:
                break;
            }

            // TODO
            // lookup command
            //  1. not found: reply error, close
            //  2. done
            // execute command

            if (client->Command().compare("PING") || client->Command().compare("ping")) {
                client->Reply("PONG\n");
            } else if (client->Command().compare("SET") || client->Command().compare("set")) {
                // TODO: implement
            } else if (client->Command().compare("GET") || client->Command().compare("get")) {
                // TODO: implement
            }
        } else if (client->Writting()) {
            // TODO: handle short write
            close_client = true;
        } else {
            // TODO: handle short write
            close_client = true;
        }

        if (close_client) {
            close(client->fd);
            delete client;
        }
    }

    return 0;
}
