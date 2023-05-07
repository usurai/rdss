#include "command_registry.h"

#include "client_commands.h"
#include "string_commands.h"

namespace rdss {

void RegisterCommands(DataStructureService* service) {
    RegisterClientCommands(service);
    RegisterStringCommands(service);
}

} // namespace rdss
