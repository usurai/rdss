#pragma once

#include "command.h"

#include <span>
#include <string_view>

namespace rdss {

using CommandStrings = std::span<std::string_view>;

// TODO: Add thread-safe queue command interface, so that multiple read thread can queue to the same
// service simultaneously.
class DataStructureService {
public:
    Result Invoke(CommandStrings command_strings);
};

} // namespace rdss
