#include "string_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <charconv>
#include <limits>
#include <optional>

namespace rdss {

using TimePoint = DataStructureService::TimePoint;
using Args = Command::CommandStrings;
using SetMode = DataStructureService::SetMode;
using SetStatus = DataStructureService::SetStatus;

namespace detail {

// Treat the second arg in 'args' as key and search if key is valid. If valid, update its LRU and
// add it's corresponding value to result, then return {entry of the key, result}. Otherwise, If
// it's stale, expire it, add null to result and return {nullptr, result}.
MTSHashTable::EntryPointer
GetFunctionBase(DataStructureService& service, Command::CommandString key, Result& result) {
    auto entry = service.FindOrExpire(key);
    if (entry == nullptr) {
        result.AddNull();
    } else {
        result.Add(std::string(*entry->value));
        entry->GetKey()->SetLRU(service.GetLRUClock());
    }
    return entry;
}

// Call GetFunctionBase, if the key is found, call callback(found_entry).
template<typename CallbackOnFound>
Result GetFunctionBaseWithCallback(
  DataStructureService& service, Command::CommandString key, CallbackOnFound callback) {
    Result result;
    auto entry = GetFunctionBase(service, key, result);
    if (entry != nullptr) {
        callback(entry);
    }
    return result;
}

} // namespace detail

static constexpr TimePoint::rep kRepMax = std::numeric_limits<TimePoint::rep>::max();

template<typename Duration>
std::optional<TimePoint> IntToTimePoint(TimePoint now, TimePoint::rep i) {
    static constexpr auto target_den = TimePoint::duration::period::den;
    static constexpr auto den = TimePoint::duration::period::den;
    if (i <= 0) {
        return std::nullopt;
    }
    if constexpr (den < target_den) {
        if (TimePoint::duration::max().count() / (target_den / den) < i) {
            return std::nullopt;
        }
    }
    if (TimePoint::max() - now < Duration{i}) {
        return std::nullopt;
    }
    return now + Duration{i};
}

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
// TODO: Determine option by size() in favour of string_view.compare().
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

Result SetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() < 3) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    const auto cmd_time = service.GetCommandTimeSnapshot();

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
    auto [set_status, entry, old_value] = service.SetData(key, args[2], set_mode, get);
    if (set_status == SetStatus::kNoOp) {
        result.AddNull();
        return result;
    }

    if (expire_time.has_value()) {
        expire_ht->Upsert(entry->CopyKey(), expire_time.value());
    } else if (set_status == SetStatus::kUpdated && !keep_ttl) {
        // TODO: maybe we can know there is no expire_entry before this.
        expire_ht->Erase(key);
    }

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

Result MSetFunction(DataStructureService& service, Args args) {
    Result result;
    if (args.size() < 3 || args.size() % 2 == 0) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    for (size_t i = 1; i < args.size(); i += 2) {
        service.SetData(args[i], args[i + 1], SetMode::kRegular, false);
        service.GetExpireHashTable()->Erase(args[i]);
    }
    result.Add("OK");
    return result;
}

Result MSetNXFunction(DataStructureService& service, Args args) {
    Result result;
    if (args.size() < 3 || args.size() % 2 == 0) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    bool succeeded{false};
    for (size_t i = 1; i < args.size(); i += 2) {
        auto [set_status, _, __] = service.SetData(args[i], args[i + 1], SetMode::kNX, false);
        succeeded |= (set_status == SetStatus::kInserted);
    }
    result.Add(((succeeded) ? 1 : 0));
    return result;
}

template<typename RepToTime>
Result SetEXFunctionBase(
  DataStructureService& service, Command::CommandStrings args, RepToTime rep_to_time) {
    Result result;
    const auto now = service.GetCommandTimeSnapshot();
    if (args.size() != 4) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto ll = ParseInt(args[2]);
    if (!ll.has_value()) {
        result.Add("value is not an integer or out of range");
        return result;
    }

    auto expire_time = rep_to_time(now, ll.value());
    if (!expire_time.has_value()) {
        result.Add("value is not an integer or out of range");
        return result;
    }

    auto [entry, _] = service.DataHashTable()->Upsert(args[1], CreateMTSPtr(args[3]));
    service.GetExpireHashTable()->Upsert(entry->CopyKey(), expire_time.value());
    entry->GetKey()->SetLRU(service.GetLRUClock());
    result.Add("OK");
    return result;
}

Result SetEXFunction(DataStructureService& service, Command::CommandStrings args) {
    return SetEXFunctionBase(service, args, IntToTimePoint<std::chrono::seconds>);
}

Result PSetEXFunction(DataStructureService& service, Command::CommandStrings args) {
    return SetEXFunctionBase(service, args, IntToTimePoint<std::chrono::milliseconds>);
}

Result SetNXFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 3) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto [set_status, _, __] = service.SetData(args[1], args[2], SetMode::kNX, false);
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
    detail::GetFunctionBase(service, args[1], result);
    return result;
}

Result MGetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() < 2) {
        result.Add("wrong number of arguments for command");
        return result;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        detail::GetFunctionBase(service, args[i], result);
    }
    return result;
}

Result GetDelFunction(DataStructureService& service, Command::CommandStrings args) {
    if (args.size() != 2) {
        Result result;
        result.Add("wrong number of arguments for command");
        return result;
    }
    return detail::GetFunctionBaseWithCallback(
      service, args[1], [&service](MTSHashTable::EntryPointer entry) {
          service.EraseKey(entry->GetKey()->StringView());
      });
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

    return detail::GetFunctionBaseWithCallback(
      service, args[1], [&service, persist, expire_time](MTSHashTable::EntryPointer entry) {
          auto key = entry->GetKey()->StringView();
          if (persist) {
              service.GetExpireHashTable()->Erase(key);
          } else if (expire_time.has_value()) {
              service.GetExpireHashTable()->Upsert(key, expire_time.value());
          }
      });
}

Result GetSetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;
    if (args.size() != 3) {
        result.Add("wrong number of arguments for command");
        return result;
    }

    auto [set_status, _, old_value] = service.SetData(args[1], args[2], SetMode::kRegular, true);
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
      "PSETEX", Command("PSETEX").SetHandler(PSetEXFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "SETNX", Command("SETNX").SetHandler(SetNXFunction).SetIsWriteCommand());
    service->RegisterCommand("MSET", Command("MSET").SetHandler(MSetFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "MSETNX", Command("MSETNX").SetHandler(MSetNXFunction).SetIsWriteCommand());
    service->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
    service->RegisterCommand("MGET", Command("MGET").SetHandler(MGetFunction));
    service->RegisterCommand("GETDEL", Command("GETDEL").SetHandler(GetDelFunction));
    service->RegisterCommand("GETEX", Command("GETEX").SetHandler(GetEXFunction));
    service->RegisterCommand("GETSET", Command("GETSET").SetHandler(GetSetFunction));
    service->RegisterCommand("EXISTS", Command("EXISTS").SetHandler(ExistsFunction));
}

} // namespace rdss
