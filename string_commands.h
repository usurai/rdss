#pragma once

#include "command.h"
#include "data_structure_service.h"

namespace rdss {

Result SetFunction(TrackingMap& data, Command::CommandStrings command_strings);
Result GetFunction(TrackingMap& data, Command::CommandStrings command_strings);

} // namespace rdss
