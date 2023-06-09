#include "command_registry.h"

#include "client_commands.h"
#include "key_commands.h"
#include "misc_commands.h"
#include "string_commands.h"

namespace rdss {

void RegisterCommands(DataStructureService* service) {
    RegisterClientCommands(service);
    RegisterKeyCommands(service);
    RegisterMiscCommands(service);
    RegisterStringCommands(service);
}

} // namespace rdss
