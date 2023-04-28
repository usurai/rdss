#pragma once

#include "command.h"

namespace rdss {

class DataStructureService;

Result SetFunction(DataStructureService& service, Command::CommandStrings command_strings);
Result GetFunction(DataStructureService& service, Command::CommandStrings command_strings);

} // namespace rdss
