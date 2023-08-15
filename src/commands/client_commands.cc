#include "client_commands.h"

#include "command.h"
#include "data_structure_service.h"

namespace rdss {

// TODO: This needs more work.
void HelloFunction(DataStructureService&, Args command_strings, Result& result) {
    if (command_strings.size() == 2 && !command_strings[1].compare("3")) {
        result.SetOk();
    } else {
        result.SetError(Error::kProtocol);
    }
}

void RegisterClientCommands(DataStructureService* service) {
    service->RegisterCommand("HELLO", Command("HELLO").SetHandler(HelloFunction));
}

} // namespace rdss
