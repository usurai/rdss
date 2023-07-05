#include "string_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <charconv>
#include <limits>
#include <optional>

namespace rdss {

using TimePoint = DataStructureService::TimePoint;
using Args = Command::CommandStrings;

static constexpr TimePoint::rep kRepMax = std::numeric_limits<TimePoint::rep>::max();

enum class SetMode {
    kRegular, /*** Update if key presents, insert otherwise ***/
    kNX,      /*** Only insert if key doesn't present ***/
    kXX       /*** Only update if key presents ***/
};

std::optional<TimePoint::rep> ParseInt(Command::CommandString str) {
    TimePoint::rep ll;
    auto [ptr, err] = std::from_chars(str.data(), str.data() + str.size(), ll);
    if (err != std::errc{} || ptr != str.data() + str.size()) {
        return std::nullopt;
    }
    return ll;
}

enum class ExtractExpireResult { kDone, kNotFound, kError };

ExtractExpireResult ExtractExpireOptions(
  Args args, size_t& i, TimePoint cmd_time, Result& result, std::optional<TimePoint>& expire_time) {
    std::function<std::optional<TimePoint>(TimePoint::rep)> rep_to_tp;
    if (!args[i].compare("PX")) {
        rep_to_tp = [current = cmd_time](TimePoint::rep rep) -> std::optional<TimePoint> {
            const auto duration = std::chrono::milliseconds{rep};
            if (TimePoint::max() - current >= duration) {
                return current + duration;
            }
            return std::nullopt;
        };
    } else if (!args[i].compare("EX")) {
        rep_to_tp = [current = cmd_time](TimePoint::rep rep) -> std::optional<TimePoint> {
            if (kRepMax / 1000 < rep) {
                return std::nullopt;
            }
            const auto duration = std::chrono::seconds{rep};
            if (TimePoint::max() - current >= duration) {
                return current + duration;
            }
            return std::nullopt;
        };
    } else if (!args[i].compare("PXAT")) {
        rep_to_tp = [](TimePoint::rep rep) { return TimePoint{std::chrono::milliseconds{rep}}; };
    } else if (!args[i].compare("EXAT")) {
        rep_to_tp = [](TimePoint::rep rep) -> std::optional<TimePoint> {
            if (kRepMax / 1000 < rep) {
                return std::nullopt;
            }
            return TimePoint{std::chrono::seconds{rep}};
        };
    } else {
        return ExtractExpireResult::kNotFound;
    }

    if (i == args.size() - 1) {
        result.Add("syntax error");
        return ExtractExpireResult::kError;
    }
    if (expire_time.has_value()) {
        result.Add("syntax error");
        return ExtractExpireResult::kError;
    }

    auto ll = ParseInt(args[i + 1]);
    if (!ll.has_value() || ll <= 0) {
        result.Add("value is not an integer or out of range");
        return ExtractExpireResult::kError;
    }
    expire_time = rep_to_tp(ll.value());
    if (!expire_time.has_value()) {
        result.Add("value is not an integer or out of range");
        return ExtractExpireResult::kError;
    }

    i += 1;
    return ExtractExpireResult::kDone;
}

// Extracts options in arguments of SET command:
// - 'set_mode': [NX | XX]
// - 'get': [GET]
// - 'expire_time' / 'keep_ttl': [EX seconds | PX milliseconds | EXAT unix-time-seconds | PXAT
// unix-time-milliseconds | KEEPTTL]
// If one of [EX|PX|EXAT|PXAT] presents, 'expire_time' is set to value of specified expiration time
// in unix-time-milliseconds, otherwise, it's std::nullopt.
// Returns false if arguments are invalid or conflict, in this case, error is added to 'result'.
// Returns true otherwise.
// TODO: Determine option by size(), discard string_view.compare().
bool ExtractSetOptions(
  Args args,
  TimePoint cmd_time,
  Result& result,
  SetMode& set_mode,
  std::optional<TimePoint>& expire_time,
  bool& keep_ttl,
  bool& get) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (!args[i].compare("GET")) {
            get = true;
            continue;
        } else if (!args[i].compare("NX")) {
            if (set_mode != SetMode::kRegular) {
                result.Add("syntax error");
                return false;
            }
            set_mode = SetMode::kNX;
            continue;
        } else if (!args[i].compare("XX")) {
            if (set_mode != SetMode::kRegular) {
                result.Add("syntax error");
                return false;
            }
            set_mode = SetMode::kXX;
            continue;
        } else if (!args[i].compare("KEEPTTL")) {
            if (expire_time.has_value()) {
                result.Add("syntax error");
                return false;
            }
            keep_ttl = true;
            continue;
        }

        const auto expire_result = ExtractExpireOptions(args, i, cmd_time, result, expire_time);
        switch (expire_result) {
        case ExtractExpireResult::kError:
            return false;
        case ExtractExpireResult::kNotFound: {
            result.Add("syntax error");
            return false;
        }
        case ExtractExpireResult::kDone: {
            if (!keep_ttl) {
                break;
            }
            result.Add("syntax error");
            return false;
        }
        }
    }
    return true;
}

enum class SetStatus { kNoOp, kInserted, kUpdated };

// TODO: Templatize 'get' argument, or make two functions.
std::pair<SetStatus, MTSPtr> SetData(
  MTSHashTable* data_ht,
  DataStructureService::ExpireHashTable* expire_ht,
  Command::CommandString key,
  Command::CommandString value,
  SetMode set_mode,
  TimePoint cmd_time,
  bool get) {
    bool inserted{false};
    bool overwritten{false};
    SetStatus set_status{SetStatus::kNoOp};
    MTSPtr old_value{nullptr};

    switch (set_mode) {
    case SetMode::kRegular: {
        bool exists{false};
        if (!get) {
            auto upsert_result = data_ht->Upsert(key, CreateMTSPtr(value));
            exists = upsert_result.second;
        } else {
            auto fc_result = data_ht->FindOrCreate(key, true);
            if (fc_result.second) {
                auto expire_entry = expire_ht->Find(key);
                if (expire_entry == nullptr || expire_entry->value > cmd_time) {
                    old_value = std::move(fc_result.first->value);
                    exists = true;
                }
            }
            fc_result.first->value = CreateMTSPtr(value);
        }
        set_status = (exists) ? SetStatus::kUpdated : SetStatus::kInserted;
        break;
    }
    case SetMode::kNX: {
        auto data_entry = data_ht->Find(key);
        if (data_entry != nullptr) {
            auto expire_entry = expire_ht->Find(key);
            if (expire_entry != nullptr && expire_entry->value <= cmd_time) {
                data_entry->value = CreateMTSPtr(value);
                expire_ht->Erase(key);
                set_status = SetStatus::kInserted;
            }
        } else {
            data_ht->Insert(key, CreateMTSPtr(value));
            set_status = SetStatus::kInserted;
        }
        break;
    }
    case SetMode::kXX: {
        auto data_entry = data_ht->Find(key);
        if (data_entry == nullptr) {
            break;
        }
        auto expire_entry = expire_ht->Find(key);
        if (expire_entry != nullptr && expire_entry->value <= cmd_time) {
            data_ht->Erase(key);
            expire_ht->Erase(key);
            break;
        }
        if (get) {
            old_value = std::move(data_entry->value);
        }
        data_entry->value = CreateMTSPtr(value);
        set_status = SetStatus::kUpdated;
        break;
    }
    }
    return {set_status, old_value};
}

Result SetFunction(DataStructureService& service, Command::CommandStrings args) {
    const auto cmd_time = service.GetCommandTimeSnapshot();

    Result result;
    SetMode set_mode{SetMode::kRegular};
    std::optional<TimePoint> expire_time{std::nullopt};
    bool keep_ttl{false};
    bool get{false};
    if (
      args.size() > 3
      && !ExtractSetOptions(
        args.subspan(3), cmd_time, result, set_mode, expire_time, keep_ttl, get)) {
        return result;
    }

    auto key = args[1];
    auto* expire_ht = service.GetExpireHashTable();
    auto [set_status, old_value] = SetData(
      service.DataHashTable(), expire_ht, key, args[2], set_mode, cmd_time, get);
    if (set_status == SetStatus::kNoOp) {
        result.AddNull();
        return result;
    }

    if (expire_time.has_value()) {
        expire_ht->Upsert(key, expire_time.value());
    } else if (set_status == SetStatus::kUpdated && !keep_ttl) {
        // TODO: maybe we can know there is no expire_entry before this.
        expire_ht->Erase(key);
    }

    // TODO: lru

    if (get) {
        if (old_value == nullptr) {
            result.AddNull();
        } else {
            // TODO: result should take the ownership of shared_ptr.
            result.Add(std::string(*old_value));
        }
    } else {
        result.Add("OK");
    }
    return result;
}

Result SetEXFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 4) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto ll = ParseInt(args[2]);
    const auto current = service.GetCommandTimeSnapshot();
    // TODO: Reuse this with ExtractExpireOptions.
    if (
      !ll.has_value() || ll <= 0 || kRepMax / 1000 < ll.value()
      || TimePoint::max() - current < std::chrono::seconds{ll.value()}) {
        result.Add("value is not an integer or out of range");
        return result;
    }

    service.DataHashTable()->Upsert(args[1], CreateMTSPtr(args[3]));
    service.GetExpireHashTable()->Upsert(
      args[1], service.GetCommandTimeSnapshot() + std::chrono::seconds{ll.value()});
    result.Add("OK");
    return result;
}

Result SetNXFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 3) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto [set_status, _] = SetData(
      service.DataHashTable(),
      service.GetExpireHashTable(),
      args[1],
      args[2],
      SetMode::kNX,
      service.GetCommandTimeSnapshot(),
      false);
    assert(set_status != SetStatus::kUpdated);
    result.Add((set_status == SetStatus::kInserted) ? 1 : 0);
    return result;
}

Result GetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 2) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto key = args[1];
    auto entry = service.DataHashTable()->Find(key);
    if (entry == nullptr) {
        result.AddNull();
        return result;
    }

    auto* expire_ht = service.GetExpireHashTable();
    auto* expire_entry = expire_ht->Find(key);
    const auto expire_found = (expire_entry != nullptr);
    if (!expire_found || service.GetCommandTimeSnapshot() < expire_entry->value) {
        entry->GetKey()->SetLRU(service.lru_clock_);
        VLOG(1) << "lru:" << entry->GetKey()->GetLRU();
        // TODO: Eliminate the conversion.
        result.Add(std::string(*entry->value));
        return result;
    }

    service.DataHashTable()->Erase(key);
    if (expire_found) {
        expire_ht->Erase(key);
    }
    result.AddNull();
    return result;
}

Result GetDelFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 2) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto key = args[1];
    auto entry = service.DataHashTable()->Find(key);
    if (entry == nullptr) {
        result.AddNull();
        return result;
    }

    auto* expire_ht = service.GetExpireHashTable();
    auto* expire_entry = expire_ht->Find(key);
    const auto expire_found = (expire_entry != nullptr);
    if (!expire_found || service.GetCommandTimeSnapshot() < expire_entry->value) {
        // TODO: Eliminate the conversion.
        result.Add(std::string(*entry->value));
    } else {
        result.AddNull();
    }

    service.DataHashTable()->Erase(key);
    if (expire_found) {
        expire_ht->Erase(key);
    }
    return result;
}

Result GetEXFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() < 2) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    bool persist{false};
    std::optional<TimePoint> expire_time{std::nullopt};
    if (args.size() > 2) {
        for (size_t i = 2; i < args.size(); ++i) {
            if (!args[i].compare("PERSIST")) {
                if (expire_time.has_value() || persist) {
                    result.Add("syntax error");
                    return result;
                }
                persist = true;
                continue;
            }
            const auto expire_result = ExtractExpireOptions(
              args, i, service.GetCommandTimeSnapshot(), result, expire_time);
            switch (expire_result) {
            case ExtractExpireResult::kError:
                return result;
            case ExtractExpireResult::kNotFound: {
                result.Add("syntax error");
                return result;
            }
            case ExtractExpireResult::kDone: {
                if (persist) {
                    result.Add("syntax error");
                    return result;
                }
                break;
            }
            }
        }
    }

    auto key = args[1];
    auto entry = service.DataHashTable()->Find(key);
    if (entry == nullptr) {
        result.AddNull();
        return result;
    }

    auto* expire_ht = service.GetExpireHashTable();
    auto* expire_entry = expire_ht->Find(key);
    const auto expire_found = (expire_entry != nullptr);
    if (!expire_found || service.GetCommandTimeSnapshot() < expire_entry->value) {
        // TODO: Eliminate the conversion.
        result.Add(std::string(*entry->value));
        if (persist) {
            if (expire_found) {
                expire_ht->Erase(key);
            }
        } else if (expire_time.has_value()) {
            expire_ht->Upsert(key, expire_time.value());
        }
        return result;
    }

    service.DataHashTable()->Erase(key);
    if (expire_found) {
        expire_ht->Erase(key);
    }
    result.AddNull();
    return result;
}

Result GetSetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 3) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto [set_status, old_value] = SetData(
      service.DataHashTable(),
      service.GetExpireHashTable(),
      args[1],
      args[2],
      SetMode::kRegular,
      service.GetCommandTimeSnapshot(),
      true);
    assert(set_status != SetStatus::kNoOp);

    if (old_value == nullptr) {
        result.AddNull();
    } else {
        result.Add(std::string(*old_value));
        service.GetExpireHashTable()->Erase(args[1]);
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
    service->RegisterCommand(
      "SETEX", Command("SETEX").SetHandler(SetEXFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "SETNX", Command("SETNX").SetHandler(SetNXFunction).SetIsWriteCommand());
    service->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
    service->RegisterCommand("GETDEL", Command("GETDEL").SetHandler(GetDelFunction));
    service->RegisterCommand("GETEX", Command("GETEX").SetHandler(GetEXFunction));
    service->RegisterCommand("GETSET", Command("GETSET").SetHandler(GetSetFunction));
    service->RegisterCommand("EXISTS", Command("EXISTS").SetHandler(ExistsFunction));
}

} // namespace rdss
