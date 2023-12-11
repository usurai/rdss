#include "misc_commands.h"

#include "base/memory.h"
#include "client_manager.h"
#include "command.h"
#include "data_structure_service.h"
#include "server.h"

#include <sys/resource.h>
#include <sys/sysinfo.h>

#include <sstream>
#include <unistd.h>

namespace rdss::detail {

// TODO: lru_clock: Clock incrementing every minute, for LRU management
void CollectServerInfo(DataStructureService& service, std::stringstream& stream) {
    stream << "# Server\n";
    stream << "multiplexing_api:io_uring\n";
    stream << "process_id:" << getpid() << '\n';
    stream << "tcp_port:" << service.GetConfig()->port << '\n';
    stream << "server_time_usec:"
           << std::chrono::time_point_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now())
                .time_since_epoch()
                .count()
           << '\n';

    const auto uptime = service.GetClock()->Now() - service.GetServer()->Stats().start_time;
    stream << "uptime_in_seconds:"
           << std::chrono::duration_cast<std::chrono::seconds>(uptime).count() << '\n';
    stream << "uptime_in_days:" << std::chrono::duration_cast<std::chrono::days>(uptime).count()
           << '\n';

    stream << "hz:" << service.GetConfig()->hz << '\n';
    stream << "configured_hz:" << service.GetConfig()->hz << "\n\n";
}

void CollectClientsInfo(DataStructureService& service, std::stringstream& stream) {
    auto* client_manager = service.GetServer()->GetClientManager();

    stream << "# Clients\n";
    stream << "connected_clients:" << client_manager->ActiveClients() << '\n';
    stream << "maxclients:" << service.GetConfig()->maxclients << '\n';
    stream << "client_recent_max_input_buffer:"
           << client_manager->Stats().max_input_buffer.load(std::memory_order_relaxed) << '\n';
    stream << "client_recent_max_output_buffer:"
           << client_manager->Stats().max_output_buffer.load(std::memory_order_relaxed) << "\n\n";
}

void CollectMemoryInfo(DataStructureService& service, std::stringstream& stream) {
    stream << "# Memory\n";
    stream << "used_memory:"
           << MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kAll>() << '\n';

    rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        LOG(ERROR) << "getrusage:" << strerror(errno);
    } else {
        stream << "used_memory_rss:" << usage.ru_maxrss << '\n';
    }

    stream << "used_memory_peak:" << MemoryTracker::GetInstance().GetPeakAllocated() << '\n';

    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        LOG(ERROR) << "sysinfo:" << strerror(errno);
    } else {
        stream << "total_system_memory:" << info.totalram << '\n';
    }

    stream << '\n';
}

// expired_keys
// expired_stale_perc
// expired_time_cap_reached_count
// expire_cycle_cpu_milliseconds
// evicted_clients
void CollectStatsInfo(DataStructureService& service, std::stringstream& stream) {
    auto& server_stats = service.GetServer()->Stats();
    auto& client_stats = service.GetServer()->GetClientManager()->Stats();

    stream << "# Stats\n";
    stream << "total_connections_received:"
           << server_stats.connections_received.load(std::memory_order_relaxed) << '\n';
    stream << "total_commands_processed:"
           << service.Stats().commands_processed.load(std::memory_order_relaxed) << '\n';
    stream << "total_net_input_bytes:"
           << client_stats.net_input_bytes.load(std::memory_order_relaxed) << '\n';
    stream << "total_net_output_bytes:"
           << client_stats.net_output_bytes.load(std::memory_order_relaxed) << '\n';
    stream << "rejected_connections:"
           << server_stats.rejected_connections.load(std::memory_order_relaxed) << '\n';

    stream << "evicted_keys:" << service.GetEvictor().GetEvictedKeys() << '\n';

    stream << '\n';
}

} // namespace rdss::detail

namespace rdss {

void DbSizeFunction(DataStructureService& service, Args, Result& result) {
    result.SetInt(static_cast<int>(service.DataTable()->Count()));
}

void InfoFunction(DataStructureService& service, Args args, Result& result) {
    // auto str_ptr = CreateMTSPtr(
    //   "# Memory\r\nevicted_keys:" + std::to_string(service.GetEvictedKeys())
    //   + "\r\nactive_expired_keys:" + std::to_string(service.active_expired_keys_));

    // result.SetString(std::move(str_ptr));

    std::stringstream stream;
    if (args.size() == 1) {
        detail::CollectServerInfo(service, stream);
        detail::CollectClientsInfo(service, stream);
        detail::CollectMemoryInfo(service, stream);
        detail::CollectStatsInfo(service, stream);
    } else {
        for (size_t i = 1; i < args.size(); ++i) {
            if (!args[i].compare("SERVER") || !args[i].compare("server")) {
                detail::CollectServerInfo(service, stream);
                continue;
            }
            if (!args[i].compare("CLIENTS") || !args[i].compare("clients")) {
                detail::CollectClientsInfo(service, stream);
                continue;
            }
            if (!args[i].compare("MEMORY") || !args[i].compare("memory")) {
                detail::CollectMemoryInfo(service, stream);
                continue;
            }
            if (!args[i].compare("STATS") || !args[i].compare("stats")) {
                detail::CollectStatsInfo(service, stream);
                continue;
            }
        }
    }

    result.SetString(CreateMTSPtr(stream.str()));
}

// TODO: Implementation.
void CommandFunction(DataStructureService&, Args, Result& result) {
    auto str_ptr = CreateMTSPtr(" ");
    result.SetString(std::move(str_ptr));
}

void ShutdownFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() > 1) {
        result.SetError(Error::kWrongArgNum);
        return;
    }
    LOG(INFO) << "User requested shutdown.";
    service.Shutdown();
    result.SetNil();
}

void RegisterMiscCommands(DataStructureService* service) {
    service->RegisterCommand("DBSIZE", Command("DBSIZE").SetHandler(DbSizeFunction));
    service->RegisterCommand("INFO", Command("INFO").SetHandler(InfoFunction));
    service->RegisterCommand("COMMAND", Command("COMMAND").SetHandler(CommandFunction));
    service->RegisterCommand("SHUTDOWN", Command("SHUTDOWN").SetHandler(ShutdownFunction));
}

} // namespace rdss
