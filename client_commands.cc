#include "client_commands.h"

#include "command.h"
#include "data_structure_service.h"

namespace rdss {

Result HelloFunction(DataStructureService&, Command::CommandStrings command_strings) {
    Result result;
    if (command_strings.size() == 2 && !command_strings[1].compare("3")) {
        result.Add("OK");
    } else {
        result.Add("Error");
    }
    return result;
}

void RegisterClientCommands(DataStructureService* service) {
    service->RegisterCommand("HELLO", Command("HELLO").SetHandler(HelloFunction));
}

} // namespace rdss
