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
CommandDictionary cmd_dict;
DataType data;

void queue_multishot_accept() {
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
        queue_multishot_accept();
    }

    auto connection = new Connection(&ring, cqe->res);
    // TODO: handle the case of too manu queued
    assert(connection->QueueRead());
    io_uring_cqe_seen(&ring, cqe);
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
        connection->SetError();
        connection->Reply("Parse error.\n");
        return;
    }

    auto cmd_itor = cmd_dict.find(connection->Command());
    if (cmd_itor == cmd_dict.end()) {
        connection->SetError();
        connection->Reply("Command not found.\n");
        return;
    }

    auto result = cmd_itor->second(connection->vec);
    // TODO: support error
    connection->Reply(rdss::Replier::BuildReply(std::move(result)));
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
    if (listen(sock, 10) < 0) {
        std::cerr << "listen: " << strerror(errno) << '\n';
        return 1;
    }

    // setup ring
    int ret;
    if (rdss::SQ_POLL) {
        io_uring_params p = {};
        p.sq_entries = rdss::QD;
        p.cq_entries = rdss::QD * 8;
        p.flags |= IORING_SETUP_CQSIZE | IORING_SETUP_CLAMP;
        p.flags |= IORING_SETUP_SQPOLL;
        ret = io_uring_queue_init_params(rdss::QD, &ring, &p);
    } else {
        ret = io_uring_queue_init(rdss::QD, &ring, 0);
    }
    if (ret) {
        std::cerr << "io_uring_queue_init: " << strerror(-ret) << '\n';
        return 1;
    }

    RegisterCommands();

    //  queue accept
    queue_multishot_accept();

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
                ++submitted;
            } else {
                auto* connection = reinterpret_cast<Connection*>(cqe->user_data);
                const auto res = cqe->res;
                io_uring_cqe_seen(&ring, cqe);

                bool close_client{false};
                if (res == 0) {
                    close_client = true;
                } else {
                    if (connection->Reading()) {
                        HandleRead(connection, res);
                        ++submitted;
                    } else if (connection->Writting()) {
                        // TODO: handle short write
                        connection->buffer.Clear();
                        assert(connection->QueueRead());
                        connection->state = Connection::State::Reading;
                        ++submitted;
                    } else {
                        // TODO: handle short write
                        close_client = true;
                    }
                }
                if (close_client) {
                    close(connection->fd);
                    delete connection;
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
