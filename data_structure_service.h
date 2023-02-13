#pragma once

#include "command.h"
#include "command_dictionary.h"
#include "tracking_hash_table.h"

namespace rdss {

// TODO: Add thread-safe queue command interface, so that multiple read thread can queue to the same
// service simultaneously.
class DataStructureService {
public:
    DataStructureService()
      : data_(new TrackingMap()) {}

    Result Invoke(Command::CommandStrings command_strings);

    void RegisterCommand(CommandName name, Command command) {
        commands_.emplace(std::move(name), std::move(command));
    }

private:
    CommandDictionary commands_;
    std::unique_ptr<TrackingMap> data_;
};

} // namespace rdss
