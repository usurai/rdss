#include "server.h"

// TODO: Unify the naming.
#include "command.h"
#include "config.h"
#include "connection.h"
#include "dragonfly/redis_parser.h"
#include "dragonfly/resp_expr.h"
#include "hash_table.h"
#include "memory.h"
#include "redis_parser.h"
#include "replier.h"
#include "tracking_hash_table.h"

#include <arpa/inet.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <liburing.h>
#include <set>

using rdss::Command;
using rdss::Connection;
using rdss::Mallocator;
using rdss::Result;
using rdss::TrackingMap;
using rdss::TrackingString;

using CommandDictionary = std::unordered_map<std::string, Command>;
using ArgList = facade::RespExpr::Vec;
using DurationCount = int64_t;

io_uring ring;
int listen_sock;
constexpr size_t num_write_rings{8};
size_t next_write_ring{0};
std::vector<io_uring> write_rings;
CommandDictionary cmd_dict;
TrackingMap data;

rdss::Config config;
__kernel_timespec ts = {.tv_sec = 0, .tv_nsec = 1};
DurationCount lru_clock = 0;

using LRUEntry = std::pair<DurationCount, rdss::TrackingStringPtr>;
struct CompareLRUEntry {
    constexpr bool operator()(const LRUEntry& lhs, const LRUEntry& rhs) const {
        return lhs.first < rhs.first;
    }
};
std::set<LRUEntry, CompareLRUEntry> eviction_pool;
constexpr size_t kEvictionPoolLimit = 16;

// TODO: Make a stat struct.
int64_t evicted_keys = 0;

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
    auto key_ptr = std::make_shared<TrackingString>(args[1].GetString());
    auto value_ptr = std::make_shared<TrackingString>(args[2].GetString());
    auto [entry, inserted] = data.InsertOrAssign(std::move(key_ptr), std::move(value_ptr));
    entry->lru = lru_clock;
    Result res;
    res.Add("OK");
    return res;
}

Result Get(ArgList& args) {
    assert(args.size() == 2);

    Result res;
    // TODO: Add support to Find by std::string_view
    auto entry = data.Find(args[1].GetString());
    if (entry == nullptr) {
        res.AddNull();
    } else {
        entry->lru = lru_clock;
        res.Add(std::string(*(entry->value)));
    }
    return res;
}

Result Exists(ArgList& args) {
    Result res;
    int32_t cnt{0};
    for (size_t i = 1; i < args.size(); ++i) {
        auto entry = data.Find(args[i].GetString());
        if (entry != nullptr) {
            entry->lru = lru_clock;
            ++cnt;
        }
    }
    res.Add(cnt);
    return res;
}

Result Dbsize() {
    Result res;
    res.Add(static_cast<int>(data.Count()));
    return res;
}

Result Info() {
    Result res;
    res.Add("# Memory\r\nevicted_keys:" + std::to_string(evicted_keys));
    return res;
}

// TODO: Handle upper/lower case in a reasonable way.
void RegisterCommands() {
    cmd_dict.insert(
      {"HELLO", Command("HELLO").SetHandler([](ArgList& args) { return Hello(args); })});
    cmd_dict.insert({"PING", Command("PING").SetHandler([](ArgList&) { return Ping(); })});
    cmd_dict.insert(
      {"SET",
       Command("SET").SetHandler([](ArgList& args) { return Set(args); }).SetIsWriteCommand()});
    cmd_dict.insert(
      {"set",
       Command("SET").SetHandler([](ArgList& args) { return Set(args); }).SetIsWriteCommand()});
    cmd_dict.insert({"GET", Command("GET").SetHandler([](ArgList& args) { return Get(args); })});
    cmd_dict.insert({"get", Command("GET").SetHandler([](ArgList& args) { return Get(args); })});
    cmd_dict.insert(
      {"COMMAND", Command("COMMAND").SetHandler([](ArgList&) { return Blockhole(); })});
    cmd_dict.insert(
      {"EXISTS", Command("EXISTS").SetHandler([](ArgList& args) { return Exists(args); })});
    cmd_dict.insert(
      {"exists", Command("EXISTS").SetHandler([](ArgList& args) { return Exists(args); })});
    cmd_dict.insert({"dbsize", Command("DBSIZE").SetHandler([](ArgList&) { return Dbsize(); })});
    cmd_dict.insert({"info", Command("INFO").SetHandler([](ArgList&) { return Info(); })});
}

void HandleAccept(io_uring_cqe* cqe) {
    if (!(cqe->flags & IORING_CQE_F_MORE)) {
        LOG(INFO) << "requeueing accept";
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
    LOG(INFO) << "OOM: " << std::to_string(rdss::MemoryTracker::GetInstance().GetAllocated())
              << " vs " << std::to_string(config.maxmemory);
    return (
      config.maxmemory != 0
      && rdss::MemoryTracker::GetInstance().GetAllocated() >= config.maxmemory);
}

size_t MemoryToFree() {
    if (config.maxmemory == 0) {
        return 0;
    }

    const auto allocated = rdss::MemoryTracker::GetInstance().GetAllocated();
    if (allocated > config.maxmemory) {
        return allocated - config.maxmemory;
    }
    return 0;
}

// TODO: Current implementation doesn't care execution time. Consider stop eviction after some
// time or attempts.
TrackingMap::EntryPointer GetSomeOldEntry(size_t samples) {
    assert(eviction_pool.size() < kEvictionPoolLimit);
    assert(data.Count() > 0);

    TrackingMap::EntryPointer result{nullptr};
    while (result == nullptr) {
        for (size_t i = 0; i < std::min(samples, data.Count()); ++i) {
            auto entry = data.GetRandomEntry();
            assert(entry != nullptr);
            eviction_pool.emplace(entry->lru, entry->key);
        }

        while (eviction_pool.size() > kEvictionPoolLimit) {
            auto it = eviction_pool.end();
            --it;
            eviction_pool.erase(it);
        }

        while (!eviction_pool.empty()) {
            auto& [lru, key] = *eviction_pool.begin();
            auto entry = data.Find({key->data(), key->size()});
            if (entry == nullptr || entry->lru != lru) {
                eviction_pool.erase(eviction_pool.begin());
                continue;
            }
            result = entry;
            eviction_pool.erase(eviction_pool.begin());
            break;
        }
    }
    return result;
}

// Returns if it's still OOM after eviction.
bool Evict() {
    const auto to_free = MemoryToFree();
    if (to_free == 0) {
        return true;
    }

    size_t freed = 0;
    while (freed < to_free) {
        if (data.Count() == 0) {
            return false;
        }

        if (config.maxmemory_policy == rdss::MaxmemoryPolicy::kNoEviction) {
            return false;
        }

        TrackingMap::EntryPointer entry{nullptr};
        if (config.maxmemory_policy == rdss::MaxmemoryPolicy::kAllKeysRandom) {
            entry = data.GetRandomEntry();
        } else {
            // allkeys-lru
            // TODO: make samples configurable: maxmemory-samples
            entry = GetSomeOldEntry(config.maxmemory_samples);
        }

        if (entry == nullptr) {
            return false;
        }
        auto delta = rdss::MemoryTracker::GetInstance().GetAllocated();
        data.Erase(std::string_view(entry->key->data(), entry->key->size()));
        ++evicted_keys;
        delta -= rdss::MemoryTracker::GetInstance().GetAllocated();
        freed += delta;
    }
    return true;
}

void ProcessCommand(Connection* conn, Command& cmd) {
    const bool evictCannotSolveOOM = !Evict();
    if (evictCannotSolveOOM && cmd.IsWriteCommand()) {
        conn->Reply(
          "error: OOM command not allowd when used memory > 'maxmemory', ("
          + std::to_string(rdss::MemoryTracker::GetInstance().GetAllocated()) + " vs "
          + std::to_string(config.maxmemory) + ").\n");
    } else {
        // TODO: support error
        // auto result = cmd(conn->vec);
        // conn->Reply(rdss::Replier::BuildReply(std::move(result)));
    }
    conn->QueueRead();
}

void HandleRead(Connection* conn, size_t bytes) {
    conn->read_buffer->Produce(bytes);

    // LOG(INFO) << std::string_view(
    //   reinterpret_cast<char*>(conn->buffer.InputBuffer().data()),
    //   conn->buffer.InputBuffer().size());
    const bool is_mbulk_start = (conn->read_buffer->Source().at(0) == '*');
    if (conn->parser == nullptr && is_mbulk_start) {
        conn->InitParser();
    }
    const auto is_mbulk
      = (is_mbulk_start || (conn->parser != nullptr && conn->parser->InProgress()));

    VLOG(1) << "Using mbulk parser:" << is_mbulk;
    auto [res, args] = is_mbulk ? conn->parser->Parse()
                                : rdss::InlineParser::ParseInline(conn->read_buffer.get());
    switch (res) {
    case rdss::RedisParser::State::kInit:
        return;
    case rdss::RedisParser::State::kError:
        conn->Reply("Parse error.\n");
        conn->read_buffer->Reset();
        return;
    case rdss::RedisParser::State::kParsing:
        return;
    case rdss::RedisParser::State::kDone:
        break;
    }

    VLOG(1) << "Received command:";
    for (const auto& arg : args) {
        VLOG(1) << arg;
    }
}

void Cron();
void QueueCron(io_uring*);
void UpdateTimeSpec();

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    if (argc > 2) {
        LOG(ERROR) << "Too many arguments";
        return 1;
    }
    if (argc == 2) {
        config.ReadFromFile(argv[1]);
        LOG(INFO) << config.ToString();
    }

    UpdateTimeSpec();

    // setup ring
    ring = NewRing(rdss::SQ_POLL);

    listen_sock = SetupListening();
    if (listen_sock == 0) {
        return 1;
    }

    AddRings(&ring);
    QueueMultishotAccept(&ring, listen_sock);
    QueueCron(&ring);
    io_uring_submit(&ring);

    RegisterCommands();

    // loop, wait cqe
    while (true) {
        assert(!io_uring_cq_has_overflow(&ring));

        io_uring_cqe* cqe;
        auto ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret) {
            LOG(ERROR) << "io_uring_wait_cqe: " << strerror(-ret);
            return 1;
        }

        int32_t submitted{0};
        while (true) {
            // TODO: don't quit here
            if (cqe->res < 0 && cqe->user_data != 1025) {
                LOG(ERROR) << "async op: " << strerror(-cqe->res);
                return 1;
            }
            // if accept, create client and queue read
            if (cqe->user_data == 1024) {
                HandleAccept(cqe);
                io_uring_cqe_seen(&ring, cqe);
                ++submitted;
            } else if (cqe->user_data == 1025) {
                Cron();
                io_uring_cqe_seen(&ring, cqe);
                QueueCron(&ring);
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
                        LOG(ERROR) << "async op: " << strerror(-write_cqe->res);
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
                        HandleRead(connection, static_cast<size_t>(res));
                        connection->QueueRead();
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
                LOG(ERROR) << "io_uring_peek_cqe: " << strerror(-ret);
                return 1;
            }
        }
        if (submitted) {
            io_uring_submit(&ring);
        }
    }

    return 0;
}

void UpdateTimeSpec() {
    if (config.hz < 1 || config.hz > 500) {
        LOG(FATAL) << "hz is out of range(1~500):" << config.hz;
    }

    if (config.hz == 1) {
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
    } else {
        ts.tv_sec = 0;
        ts.tv_nsec = (1000 / config.hz) * 1000000LL;
    }
}

void QueueCron(io_uring* ring) {
    auto* sqe = io_uring_get_sqe(ring);
    assert(sqe != nullptr);
    io_uring_prep_timeout(sqe, &ts, 1, IORING_TIMEOUT_ETIME_SUCCESS);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(1025));
}

int64_t GetLruClock() {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    return epoch.count();
}

void Cron() {
    // TODO: Support lru resolution.
    auto clock = GetLruClock();
    lru_clock = clock;
}

int SetupListening() {
    // socket
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        LOG(ERROR) << "socket: " << strerror(errno);
        return 0;
    }

    int enable{1};
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        LOG(ERROR) << "setsockopt" << strerror(errno);
        return 0;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind
    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG(ERROR) << "bind: " << strerror(errno);
        return 0;
    }

    // listen
    if (listen(sock, 1000) < 0) {
        LOG(ERROR) << "listen: " << strerror(errno);
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
            LOG(ERROR) << "io_uring_queue_init_params: " << strerror(-ret);
            std::terminate();
        }
    } else {
        if (auto ret = io_uring_queue_init(rdss::QD, &ring, 0)) {
            LOG(ERROR) << "io_uring_queue_init: " << strerror(-ret);
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