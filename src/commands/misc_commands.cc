#include "misc_commands.h"

#include "command.h"
#include "data_structure_service.h"

namespace rdss {

void DbSizeFunction(DataStructureService& service, Args, Result& result) {
    result.SetInt(static_cast<int>(service.DataHashTable()->Count()));
}

void InfoFunction(DataStructureService& service, Args, Result& result) {
    auto str_ptr = CreateMTSPtr(
      "# Memory\r\nevicted_keys:" + std::to_string(service.GetEvictedKeys())
      + "\r\nactive_expired_keys:" + std::to_string(service.active_expired_keys_));

    result.SetString(std::move(str_ptr));
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
