#include "server.h"

#include "command.h"
#include "connection.h"
#include "dragonfly/redis_parser.h"
#include "dragonfly/resp_expr.h"
#include "replier.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <liburing.h>

using rdss::Command;
using rdss::Connection;
using CommandDictionary = std::unordered_map<std::string, Command>;
using DataType = std::map<std::string, std::string>;
using Result = rdss::Result;
using ArgList = facade::RespExpr::Vec;

int sock;
io_uring ring;
constexpr size_t num_write_rings{4};
size_t next_write_ring{0};
std::vector<io_uring> write_rings;
CommandDictionary cmd_dict;
DataType data;

void QueueMultishotAccept() {
    auto* sqe = io_uring_get_sqe(&ring);
    assert(sqe != nullptr);
    // TODO: use direct variant
    io_uring_prep_multishot_accept(sqe, sock, nullptr, nullptr, 0);
    io_uring_sqe_set_data(sqe, &ring);
    io_uring_submit(&ring);
}

Result Hello(ArgList& args) {
    Result res;
    if (args[1].GetString().compare("3")) {
        res.Add("Error");
    } else {
        res.Add("OK");
    }
    return res;
}

Result Blockhole() { return Result{}; }

Result Ping() {
    Result res;
    res.Add("PONG");
    return res;
}

Result Set(ArgList& args) {
    assert(args.size() == 3);
    data[args[1].GetString()] = args[2].GetString();
    Result res;
    res.Add("OK");
    return res;
}

Result Get(ArgList& args) {
    assert(args.size() == 2);

    Result res;
    auto it = data.find(args[1].GetString());
    if (it == data.end()) {
        res.AddNull();
    } else {
        res.Add(it->second);
    }
    return res;
}

void RegisterCommands() {
    cmd_dict.insert(
      {"HELLO", Command("HELLO").SetHandler([](ArgList& args) { return Hello(args); })});
    cmd_dict.insert({"PING", Command("PING").SetHandler([](ArgList&) { return Ping(); })});
    cmd_dict.insert({"SET", Command("SET").SetHandler([](ArgList& args) { return Set(args); })});
    cmd_dict.insert({"GET", Command("GET").SetHandler([](ArgList& args) { return Get(args); })});
    cmd_dict.insert(
      {"COMMAND", Command("COMMAND").SetHandler([](ArgList&) { return Blockhole(); })});
}

void HandleAccept(io_uring_cqe* cqe) {
    if (!(cqe->flags & IORING_CQE_F_MORE)) {
        std::cout << "requeueing accept\n";
        QueueMultishotAccept();
    }

    auto connection = new Connection(&ring, &write_rings[next_write_ring], cqe->res);
    if (++next_write_ring == num_write_rings) {
        next_write_ring = 0;
    }
    // TODO: handle the case of too manu queued
    assert(connection->QueueRead());
}

void HandleRead(Connection* connection, int32_t bytes) {
    connection->buffer.CommitWrite(static_cast<size_t>(bytes));

    // std::cout << "Request("
    //           << "):"
    //           << std::string_view(
    //                reinterpret_cast<char*>(connection->buffer.InputBuffer().data()),
    //                connection->buffer.InputLen())
    //           << '\n';
    // client->IncreaseReadLength(static_cast<size_t>(res));
    // parse buffer: query_buffer->args
    // const auto parse_result = client->ParseBuffer();

    uint32_t consumed{0};
    auto res = connection->parser.Parse(
      connection->buffer.InputBuffer(), &consumed, &connection->vec);
    if (consumed != 0) {
        connection->buffer.ConsumeInput(consumed);
    }
    switch (res) {
    case facade::RedisParser::Result::OK:
        // std::cout << "Parse done, argc:" << connection->vec.size() << '\n';
        break;
    case facade::RedisParser::Result::INPUT_PENDING:
        std::cout << "Needs more\n";
        connection->buffer.EnsureCapacity(connection->buffer.Capacity());
        connection->QueueRead();
        return;
    default:
        connection->ReplyAndClose("Parse error.\n");
        return;
    }

    auto cmd_itor = cmd_dict.find(connection->Command());
    if (cmd_itor == cmd_dict.end()) {
        connection->ReplyAndClose("Command not found.\n");
        return;
    }

    auto result = cmd_itor->second(connection->vec);
    // TODO: support error
    connection->Reply(rdss::Replier::BuildReply(std::move(result)));
    connection->buffer.Clear();
    connection->QueueRead();
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

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind
    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind: " << strerror(errno) << '\n';
        return 1;
    }

    // listen
    if (listen(sock, 1000) < 0) {
        std::cerr << "listen: " << strerror(errno) << '\n';
        return 1;
    }

    // setup ring
    // TODO: consolidate
    int ret;
    if (rdss::SQ_POLL) {
        io_uring_params p = {};
        // p.sq_entries = rdss::QD;
        // p.cq_entries = rdss::QD * 8;
        // p.flags |= IORING_SETUP_CQSIZE | IORING_SETUP_CLAMP;
        p.flags |= IORING_SETUP_SQPOLL;
        ret = io_uring_queue_init_params(rdss::QD, &ring, &p);
    } else {
        ret = io_uring_queue_init(rdss::QD, &ring, 0);
    }
    if (ret) {
        std::cerr << "io_uring_queue_init: " << strerror(-ret) << '\n';
        return 1;
    }

    io_uring_params p = {};
    p.flags |= IORING_SETUP_SQPOLL;
    write_rings.resize(num_write_rings);
    for (size_t i = 0; i < num_write_rings; ++i) {
        ret = io_uring_queue_init_params(rdss::QD, &write_rings[i], &p);
    }

    RegisterCommands();

    //  queue accept
    QueueMultishotAccept();

    // Submit poll write_ring
    for (size_t i = 0; i < num_write_rings; ++i) {
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe != nullptr);
        io_uring_prep_poll_multishot(sqe, write_rings[i].ring_fd, POLL_IN);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(i));
        io_uring_submit(&ring);
    }

    // loop, wait cqe
    while (true) {
        assert(!io_uring_cq_has_overflow(&ring));

        io_uring_cqe* cqe;
        auto ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret) {
            std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << '\n';
            return 1;
        }

        int32_t submitted{0};
        while (true) {
            // TODO: don't quit here
            if (cqe->res < 0) {
                std::cerr << "async op: " << strerror(-cqe->res) << '\n';
                return 1;
            }
            // if accept, create client and queue read
            if (cqe->user_data == reinterpret_cast<uint64_t>(&ring)) {
                HandleAccept(cqe);
                io_uring_cqe_seen(&ring, cqe);
                ++submitted;
            } else if (cqe->user_data < num_write_rings) {
                assert(cqe->flags & IORING_CQE_F_MORE);

                auto& write_ring{write_rings[cqe->user_data]};
                io_uring_cqe_seen(&ring, cqe);

                // TODO: use io_uring_peek_batch_cqe
                while (true) {
                    io_uring_cqe* write_cqe;
                    if (io_uring_peek_cqe(&write_ring, &write_cqe)) {
                        break;
                    }
                    if (write_cqe->res < 0) {
                        std::cerr << "async op: " << strerror(-write_cqe->res) << '\n';
                        return 1;
                    }
                    io_uring_cqe_seen(&write_ring, write_cqe);
                }
            } else {
                auto* connection = reinterpret_cast<Connection*>(cqe->user_data);
                const auto res = cqe->res;
                io_uring_cqe_seen(&ring, cqe);

                if (!connection->Alive()) {
                    delete connection;
                } else {
                    if (res == 0) {
                        connection->SetClosing();
                    } else {
                        HandleRead(connection, res);
                    }
                    ++submitted;
                }
            }

            if (!rdss::DRAIN_CQ) {
                break;
            }

            if (io_uring_sq_space_left(&ring) == 0) {
                break;
            }
            auto ret = io_uring_peek_cqe(&ring, &cqe);
            if (!ret) {
                continue;
            } else if (ret == -EAGAIN) {
                break;
            } else {
                std::cerr << "io_uring_peek_cqe: " << strerror(-ret) << '\n';
                return 1;
            }
        }
        if (submitted) {
            io_uring_submit(&ring);
        }
    }

    return 0;
}
