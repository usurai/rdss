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

    auto [entry, _] = service.DataHashTable()->Upsert(
      command_strings[1], CreateMTSPtr(command_strings[2]));
    entry->GetKey()->SetLRU(service.lru_clock_);
    VLOG(1) << "lru:" << entry->GetKey()->GetLRU();
    result.Add("inserted");
    return result;
}

Result GetFunction(DataStructureService& service, Command::CommandStrings command_strings) {
    Result result;
    if (command_strings.size() != 2) {
        result.Add("error");
        return result;
    }
    auto entry = service.DataHashTable()->Find(command_strings[1]);
    if (entry == nullptr) {
        result.AddNull();
    } else {
        entry->GetKey()->SetLRU(service.lru_clock_);
        VLOG(1) << "lru:" << entry->GetKey()->GetLRU();
        // TODO: Eliminate the conversion.
        result.Add(std::string(*entry->value));
    }
    return result;
}

Result ExistsFunction(DataStructureService& service, Command::CommandStrings command_strings) {
    Result result;
    int32_t cnt{0};
    for (size_t i = 1; i < command_strings.size(); ++i) {
        auto entry = service.DataHashTable()->Find(command_strings[i]);
        if (entry != nullptr) {
            entry->GetKey()->SetLRU(service.lru_clock_);
            ++cnt;
        }
    }
    result.Add(cnt);
    return result;
}

void RegisterStringCommands(DataStructureService* service) {
    service->RegisterCommand("SET", Command("SET").SetHandler(SetFunction).SetIsWriteCommand());
    service->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
    service->RegisterCommand("EXISTS", Command("EXISTS").SetHandler(ExistsFunction));
}

} // namespace rdss
