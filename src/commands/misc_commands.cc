#include "misc_commands.h"

#include "command.h"
#include "data_structure_service.h"
#include "server.h"

#include <sstream>
#include <unistd.h>

namespace rdss::detail {

// TODO: lru_clock: Clock incrementing every minute, for LRU management
std::string CollectServerInfo(DataStructureService& service) {
    std::stringstream stream;

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

    const auto uptime = service.GetClock()->Now() - service.GetServer()->GetStartupTime();
    stream << "uptime_in_seconds:"
           << std::chrono::duration_cast<std::chrono::seconds>(uptime).count() << '\n';
    stream << "uptime_in_days:" << std::chrono::duration_cast<std::chrono::days>(uptime).count()
           << '\n';

    stream << "hz:" << service.GetConfig()->hz << '\n';
    stream << "configured_hz:" << service.GetConfig()->hz << "\n\n";

    return stream.str();
}

std::string CollectClientsInfo(DataStructureService& service) {
    std::stringstream stream;
    const auto* client_manager = service.GetServer()->GetClientManager();

    stream << "# Clients\n";
    stream << "connected_clients:" << client_manager->ActiveClients() << '\n';
    stream << "maxclients:" << service.GetConfig()->maxclients << '\n';
    stream << "client_recent_max_input_buffer:" << client_manager->GetMaxInputBuffer() << '\n';
    stream << "client_recent_max_output_buffer:" << client_manager->GetMaxOutputBuffer() << "\n\n";

    return stream.str();
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

    std::string s;
    if (args.size() == 1) {
        s += detail::CollectServerInfo(service);
        s += detail::CollectClientsInfo(service);
    } else {
        for (size_t i = 1; i < args.size(); ++i) {
            if (!args[i].compare("SERVER") || !args[i].compare("server")) {
                s += detail::CollectServerInfo(service);
                continue;
            }
            if (!args[i].compare("CLIENTS") || !args[i].compare("clients")) {
                s += detail::CollectClientsInfo(service);
                continue;
            }
        }
    }

    result.SetString(CreateMTSPtr(std::move(s)));
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
