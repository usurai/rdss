#include "string_commands.h"

#include "command.h"
#include "data_structure_service.h"

namespace rdss {

Result SetFunction(DataStructureService& service, Command::CommandStrings command_strings) {
    Result result;
    if (command_strings.size() != 3) {
        result.Add("error");
        return result;
    }

    auto key_ptr = std::make_shared<TrackingString>(command_strings[1]);
    auto value_ptr = std::make_shared<TrackingString>(command_strings[2]);
    auto [entry, _] = service.HashTable()->InsertOrAssign(std::move(key_ptr), std::move(value_ptr));
    entry->lru = service.lru_clock_;
    VLOG(1) << "lru:" << entry->lru;
    result.Add("inserted");
    return result;
}

Result GetFunction(DataStructureService& service, Command::CommandStrings command_strings) {
    Result result;
    if (command_strings.size() != 2) {
        result.Add("error");
        return result;
    }
    auto entry = service.HashTable()->Find(command_strings[1]);
    if (entry == nullptr) {
        result.AddNull();
    } else {
        entry->lru = service.lru_clock_;
        VLOG(1) << "lru:" << entry->lru;
        // TODO: Eliminate the conversion.
        result.Add(std::string(*(entry->value)));
    }
    return result;
}

void RegisterStringCommands(DataStructureService* service) {
    service->RegisterCommand("SET", Command("SET").SetHandler(SetFunction).SetIsWriteCommand());
    service->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
}

} // namespace rdss
