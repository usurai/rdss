#include "command_registry.h"

#include "commands/client_commands.h"
#include "commands/key_commands.h"
#include "commands/misc_commands.h"
#include "commands/string_commands.h"

namespace rdss {

void RegisterCommands(DataStructureService* service) {
    RegisterClientCommands(service);
    RegisterKeyCommands(service);
    RegisterMiscCommands(service);
    RegisterStringCommands(service);
}

} // namespace rdss
