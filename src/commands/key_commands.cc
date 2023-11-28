#include "key_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <chrono>

namespace rdss {

void TtlFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() > 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto key = args[1];
    auto data_entry = service.DataTable()->Find(key);
    if (data_entry == nullptr) {
        result.SetInt(-2);
        return;
    }
    auto expire_entry = service.ExpireTable()->Find(key);
    if (expire_entry == nullptr) {
        result.SetInt(-1);
        return;
    }
    const auto cmd_time = service.GetCommandTimeSnapshot();
    if (expire_entry->value <= cmd_time) {
        service.DataTable()->Erase(key);
        service.ExpireTable()->Erase(key);
        result.SetInt(-2);
        return;
    }

    const auto ttl = std::chrono::duration_cast<std::chrono::seconds>(
      expire_entry->value - cmd_time);
    result.SetInt(ttl.count());
}

void DelFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() < 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    int deleted{0};
    for (size_t i = 1; i < args.size(); ++i) {
        auto key = args[i];
        auto entry = service.FindOrExpire(key);
        if (entry != nullptr) {
            service.EraseKey(key);
            ++deleted;
        }
    }
    result.SetInt(deleted);
}

void RegisterKeyCommands(DataStructureService* service) {
    service->RegisterCommand("TTL", Command("TTL").SetHandler(TtlFunction));
    service->RegisterCommand("DEL", Command("DEL").SetHandler(DelFunction).SetIsWriteCommand());
}

} // namespace rdss
