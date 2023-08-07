#include "key_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <chrono>

namespace rdss {

Result TtlFunction(DataStructureService& service, Args args) {
    Result result;
    if (args.size() > 2) {
        result.Add("syntax error");
        return result;
    }

    auto key = args[1];
    auto data_entry = service.DataHashTable()->Find(key);
    if (data_entry == nullptr) {
        result.Add(-2);
        return result;
    }
    auto expire_entry = service.GetExpireHashTable()->Find(key);
    if (expire_entry == nullptr) {
        result.Add(-1);
        return result;
    }
    const auto cmd_time = service.GetCommandTimeSnapshot();
    if (expire_entry->value <= cmd_time) {
        service.DataHashTable()->Erase(key);
        service.GetExpireHashTable()->Erase(key);
        result.Add(-2);
        return result;
    }

    const auto ttl = std::chrono::duration_cast<std::chrono::seconds>(
      expire_entry->value - cmd_time);
    result.Add(ttl.count());
    return result;
}

void RegisterKeyCommands(DataStructureService* service) {
    service->RegisterCommand("TTL", Command("TTL").SetHandler(TtlFunction));
}

} // namespace rdss
