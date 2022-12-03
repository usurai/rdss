#include "server.h"

// TODO: Unify the naming.
#include "command.h"
#include "config.h"
#include "connection.h"
#include "dragonfly/redis_parser.h"
#include "dragonfly/resp_expr.h"
#include "hash_table.h"
#include "memory.h"
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
using rdss::Mallocator;

using CommandDictionary = std::unordered_map<std::string, Command>;
using TrackingString = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;
using TrackingMap = rdss::HashTable<TrackingString, TrackingString>;
using Result = rdss::Result;
using ArgList = facade::RespExpr::Vec;

io_uring ring;
int listen_sock;
constexpr size_t num_write_rings{8};
size_t next_write_ring{0};
std::vector<io_uring> write_rings;
CommandDictionary cmd_dict;
TrackingMap data;

rdss::Config config;

io_uring NewRing(bool polling = false);
void AddRings(io_uring* ring);
int SetupListening();
void QueueMultishotAccept(io_uring* ring, int socket);

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
    data.InsertOrAssign(TrackingString(args[1].GetString()), TrackingString(args[2].GetString()));
    Result res;
    res.Add("OK");
    return res;
}

Result Get(ArgList& args) {
    assert(args.size() == 2);

    Result res;
    auto entry = data.Find(TrackingString(args[1].GetString()));
    if (entry == nullptr) {
        res.AddNull();
    } else {
        res.Add(std::string(entry->value));
    }
    return res;
}

void RegisterCommands() {
    cmd_dict.insert(
      {"HELLO", Command("HELLO").SetHandler([](ArgList& args) { return Hello(args); })});
    cmd_dict.insert({"PING", Command("PING").SetHandler([](ArgList&) { return Ping(); })});
    cmd_dict.insert(
      {"SET",
       Command("SET").SetHandler([](ArgList& args) { return Set(args); }).SetIsWriteCommand()});
    cmd_dict.insert({"GET", Command("GET").SetHandler([](ArgList& args) { return Get(args); })});
    cmd_dict.insert(
      {"COMMAND", Command("COMMAND").SetHandler([](ArgList&) { return Blockhole(); })});
}

void HandleAccept(io_uring_cqe* cqe) {
    if (!(cqe->flags & IORING_CQE_F_MORE)) {
        std::cout << "requeueing accept\n";
        QueueMultishotAccept(&ring, listen_sock);
    }

    auto connection = new Connection(&ring, &write_rings[next_write_ring], cqe->res);
    if (++next_write_ring == num_write_rings) {
        next_write_ring = 0;
    }
    // TODO: handle the case of too manu queued
    assert(connection->QueueRead());
}

bool IsOOM() {
    // TODO: turn this into log.
    std::cout << std::to_string(rdss::MemoryTracker::GetInstance().GetAllocated()) << " vs "
              << std::to_string(config.maxmemory) + ").\n";
    return (
      config.maxmemory != 0
      && rdss::MemoryTracker::GetInstance().GetAllocated() >= config.maxmemory);
}

bool evict() {
    if (data.Count() == 0) {
        return false;
    }

    // Random evict
    auto entry = data.GetRandomEntry();
    if (entry == nullptr) {
        return false;
    }
    data.Erase(entry->key);
    return true;
}

void ProcessCommand(Connection* conn, Command& cmd) {
    bool evictCannotSolveOOM = false;
    while (IsOOM()) {
        if (!evict()) {
            evictCannotSolveOOM = true;
            break;
        }
    }

    if (evictCannotSolveOOM && cmd.IsWriteCommand()) {
        conn->Reply(
          "error: OOM command not allowd when used memory > 'maxmemory', ("
          + std::to_string(rdss::MemoryTracker::GetInstance().GetAllocated()) + " vs "
          + std::to_string(config.maxmemory) + ").\n");
    } else {
        // TODO: support error
        auto result = cmd(conn->vec);
        conn->Reply(rdss::Replier::BuildReply(std::move(result)));
    }
    conn->buffer.Clear();
    conn->QueueRead();
}

void HandleRead(Connection* conn, int32_t bytes) {
    conn->buffer.CommitWrite(static_cast<size_t>(bytes));
    uint32_t consumed{0};
    auto res = conn->parser.Parse(conn->buffer.InputBuffer(), &consumed, &conn->vec);
    if (consumed != 0) {
        conn->buffer.ConsumeInput(consumed);
    }
    switch (res) {
    case facade::RedisParser::Result::OK:
        // std::cout << "Parse done, argc:" << conn->vec.size() << '\n';
        break;
    case facade::RedisParser::Result::INPUT_PENDING:
        // std::cout << "Needs more\n";
        conn->buffer.EnsureCapacity(conn->buffer.Capacity());
        conn->QueueRead();
        return;
    default:
        conn->ReplyAndClose("Parse error.\n");
        return;
    }

    auto cmd_itor = cmd_dict.find(conn->Command());
    if (cmd_itor == cmd_dict.end()) {
        conn->Reply("Command not found.\n");
        conn->buffer.Clear();
        conn->QueueRead();
        return;
    }

    ProcessCommand(conn, cmd_itor->second);
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Too many arguments\n";
        return 1;
    }
    if (argc == 2) {
        config.ReadFromFile(argv[1]);
        std::cout << "Config: " << config.ToString() << '\n';
    }

    // setup ring
    ring = NewRing(rdss::SQ_POLL);

    listen_sock = SetupListening();
    if (listen_sock == 0) {
        return 1;
    }

    AddRings(&ring);
    QueueMultishotAccept(&ring, listen_sock);
    io_uring_submit(&ring);

    RegisterCommands();

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
            if (cqe->user_data == 1024) {
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
    addr.sin_port = htons(8080);
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

void QueueMultishotAccept(io_uring* ring, int socket) {
    auto* sqe = io_uring_get_sqe(ring);
    assert(sqe != nullptr);
    // TODO: use direct variant
    io_uring_prep_multishot_accept(sqe, socket, nullptr, nullptr, 0);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(1024));
}

io_uring NewRing(bool polling) {
    io_uring ring;
    if (polling) {
        io_uring_params p = {};
        p.flags |= IORING_SETUP_SQPOLL;
        if (auto ret = io_uring_queue_init_params(rdss::QD, &ring, &p)) {
            std::cerr << "io_uring_queue_init_params: " << strerror(-ret) << '\n';
            std::terminate();
        }
    } else {
        if (auto ret = io_uring_queue_init(rdss::QD, &ring, 0)) {
            std::cerr << "io_uring_queue_init: " << strerror(-ret) << '\n';
            std::terminate();
        }
    }
    return ring;
}

void AddRings(io_uring* agg) {
    for (size_t i = 0; i < num_write_rings; ++i) {
        write_rings.push_back(NewRing(true));
        auto* sqe = io_uring_get_sqe(agg);
        assert(sqe != nullptr);
        io_uring_prep_poll_multishot(sqe, write_rings[i].ring_fd, POLL_IN);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(i));
    }
}
