#pragma once

#include "command.h"
#include "command_dictionary.h"
#include "tracking_hash_table.h"

namespace rdss {

struct Config;

// TODO: Add thread-safe queue command interface, so that multiple read thread can queue to the same
// service simultaneously.
class DataStructureService {
public:
    DataStructureService(Config* config)
      : config_(config)
      , data_(new TrackingMap()) {}

    Result Invoke(Command::CommandStrings command_strings);

    void RegisterCommand(CommandName name, Command command) {
        commands_.emplace(std::move(name), std::move(command));
    }

private:
    size_t IsOOM() const;
    bool Evict(size_t bytes_to_free);

    Config* config_;
    CommandDictionary commands_;
    std::unique_ptr<TrackingMap> data_;
};

} // namespace rdss
