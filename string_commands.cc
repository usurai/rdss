#include "string_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <charconv>
#include <optional>

namespace rdss {

Result SetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;

    std::optional<DataStructureService::Clock::time_point> expire_time{std::nullopt};
    if (args.size() > 3) {
        for (size_t i = 3; i < args.size(); ++i) {
            if (!args[i].compare("PX")) {
                if (i == args.size() - 1) {
                    result.Add("syntax error");
                    return result;
                }
            }

            int64_t duration_argument;
            auto expire_sv = args[i + 1];
            auto [ptr, err] = std::from_chars(
              expire_sv.data(), expire_sv.data() + expire_sv.size(), duration_argument);
            if (
              err != std::errc{} || ptr != expire_sv.data() + expire_sv.size()
              || duration_argument <= 0) {
                result.Add("value is not an integer or out of range");
                return result;
            }
            std::chrono::milliseconds d{duration_argument};
            expire_time = service.GetCommandTimeSnapshot() + d;
            VLOG(1) << "expire duration: " << d.count()
                    << "ms, expire time: " << expire_time.value().time_since_epoch().count();
            i += 1;
            break;
        }
    }

    auto [entry, _] = service.DataHashTable()->Upsert(args[1], CreateMTSPtr(args[2]));
    entry->GetKey()->SetLRU(service.lru_clock_);
    VLOG(1) << "Data: insert key [" << entry->GetKey()->StringView()
            << "], lru:" << entry->GetKey()->GetLRU();

    auto* expire_ht = service.GetExpireHashTable();
    if (expire_time.has_value()) {
        auto [exp_entry, _] = expire_ht->FindOrCreate(args[1], true, false);
        exp_entry->key = entry->CopyKey();
        VLOG(1) << "Expire: insert " << exp_entry->key->StringView()
                << ", use_count:" << exp_entry->key.use_count();
        exp_entry->value = expire_time.value();
    } else {
        expire_ht->Erase(args[1]);
    }

    result.Add("inserted");
    return result;
}

Result GetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 2) {
        result.Add("error");
        return result;
    }
    auto entry = service.DataHashTable()->Find(args[1]);
    if (entry == nullptr) {
        result.AddNull();
        return result;
    }

    auto* expire_ht = service.GetExpireHashTable();
    auto* expire_entry = expire_ht->Find(args[1]);
    const auto expire_found = (expire_entry != nullptr);
    if (!expire_found || service.GetCommandTimeSnapshot() < expire_entry->value) {
        entry->GetKey()->SetLRU(service.lru_clock_);
        VLOG(1) << "lru:" << entry->GetKey()->GetLRU();
        // TODO: Eliminate the conversion.
        result.Add(std::string(*entry->value));
        return result;
    }

    service.DataHashTable()->Erase(args[1]);
    if (expire_found) {
        expire_ht->Erase(args[1]);
    }
    result.AddNull();
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
