#include "misc_commands.h"

#include "command.h"
#include "data_structure_service.h"

namespace rdss {

Result DbSizeFunction(DataStructureService& service, Command::CommandStrings) {
    Result result;
    result.Add(static_cast<int>(service.DataHashTable()->Count()));
    return result;
}

Result InfoFunction(DataStructureService&, Command::CommandStrings) {
    Result result;
    // TODO: Add stat to include 'evicted_keys'
    result.Add("# Memory\r\nevicted_keys:" + std::to_string(0));
    return result;
}

// TODO: Implementation.
Result CommandFunction(DataStructureService&, Command::CommandStrings) {
    Result result;
    result.Add(" ");
    return result;
}

void RegisterMiscCommands(DataStructureService* service) {
    service->RegisterCommand("DBSIZE", Command("DBSIZE").SetHandler(DbSizeFunction));
    service->RegisterCommand("INFO", Command("INFO").SetHandler(InfoFunction));
    service->RegisterCommand("COMMAND", Command("COMMAND").SetHandler(CommandFunction));
}

} // namespace rdss
