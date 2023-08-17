#include "string_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <charconv>
#include <limits>
#include <optional>

namespace rdss {

using TimePoint = DataStructureService::TimePoint;
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
        result.SetNil();
    } else {
        result.SetString(entry->value);
        entry->GetKey()->SetLRU(service.GetLRUClock());
    }
    return entry;
}

// Call GetFunctionBase, if the key is found, call callback(found_entry).
template<typename CallbackOnFound>
void GetFunctionBaseWithCallback(
  DataStructureService& service,
  Command::CommandString key,
  Result& result,
  CallbackOnFound callback) {
    auto entry = GetFunctionBase(service, key, result);
    if (entry != nullptr) {
        callback(entry);
    }
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

template<typename Rep>
std::optional<Rep> ParseInt(Command::CommandString str) {
    Rep ll;
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
        result.SetError(Error::kSyntaxError);
        return ExtractExpireResult::kError;
    }
    if (expire_time.has_value()) {
        result.SetError(Error::kSyntaxError);
        return ExtractExpireResult::kError;
    }

    auto ll = ParseInt<TimePoint::rep>(args[i + 1]);
    if (!ll.has_value() || ll <= 0) {
        result.SetError(Error::kNotAnInt);
        return ExtractExpireResult::kError;
    }
    expire_time = rep_to_tp(ll.value());
    if (!expire_time.has_value()) {
        result.SetError(Error::kNotAnInt);
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
                result.SetError(Error::kSyntaxError);
                return false;
            }
            set_mode = SetMode::kNX;
            continue;
        } else if (!args[i].compare("XX")) {
            if (set_mode != SetMode::kRegular) {
                result.SetError(Error::kSyntaxError);
                return false;
            }
            set_mode = SetMode::kXX;
            continue;
        } else if (!args[i].compare("KEEPTTL")) {
            if (expire_time.has_value()) {
                result.SetError(Error::kSyntaxError);
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
            result.SetError(Error::kSyntaxError);
            return false;
        }
        case ExtractExpireResult::kDone: {
            if (!keep_ttl) {
                break;
            }
            result.SetError(Error::kSyntaxError);
            return false;
        }
        }
    }
    return true;
}

void SetFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() < 3) {
        result.SetError(Error::kWrongArgNum);
        return;
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
        return;
    }

    auto key = args[1];
    auto* expire_ht = service.GetExpireHashTable();
    auto [set_status, entry, old_value] = service.SetData(key, args[2], set_mode, get);
    if (set_status == SetStatus::kNoOp) {
        result.SetNil();
        return;
    }

    if (expire_time.has_value()) {
        expire_ht->Upsert(entry->CopyKey(), expire_time.value());
    } else if (set_status == SetStatus::kUpdated && !keep_ttl) {
        // TODO: maybe we can know there is no expire_entry before this.
        expire_ht->Erase(key);
    }

    if (get) {
        if (old_value == nullptr) {
            result.SetNil();
        } else {
            // TODO: result should take the ownership of shared_ptr.
            result.SetString(std::move(old_value));
        }
    } else {
        result.SetOk();
    }
}

void MSetFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() < 3 || args.size() % 2 == 0) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    for (size_t i = 1; i < args.size(); i += 2) {
        service.SetData(args[i], args[i + 1], SetMode::kRegular, false);
        service.GetExpireHashTable()->Erase(args[i]);
    }
}

void MSetNXFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() < 3 || args.size() % 2 == 0) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    bool succeeded{false};
    for (size_t i = 1; i < args.size(); i += 2) {
        auto [set_status, _, __] = service.SetData(args[i], args[i + 1], SetMode::kNX, false);
        succeeded |= (set_status == SetStatus::kInserted);
    }
    result.SetInt(((succeeded) ? 1 : 0));
}

template<typename RepToTime>
void SetEXFunctionBase(
  DataStructureService& service, Args args, Result& result, RepToTime rep_to_time) {
    const auto now = service.GetCommandTimeSnapshot();
    if (args.size() != 4) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto ll = ParseInt<TimePoint::rep>(args[2]);
    if (!ll.has_value()) {
        result.SetError(Error::kNotAnInt);
        return;
    }

    auto expire_time = rep_to_time(now, ll.value());
    if (!expire_time.has_value()) {
        result.SetError(Error::kNotAnInt);
        return;
    }

    auto [entry, _] = service.DataHashTable()->Upsert(args[1], CreateMTSPtr(args[3]));
    service.GetExpireHashTable()->Upsert(entry->CopyKey(), expire_time.value());
    entry->GetKey()->SetLRU(service.GetLRUClock());
}

void SetEXFunction(DataStructureService& service, Args args, Result& result) {
    SetEXFunctionBase(service, args, result, IntToTimePoint<std::chrono::seconds>);
}

void PSetEXFunction(DataStructureService& service, Args args, Result& result) {
    SetEXFunctionBase(service, args, result, IntToTimePoint<std::chrono::milliseconds>);
}

void SetNXFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 3) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto [set_status, _, __] = service.SetData(args[1], args[2], SetMode::kNX, false);
    assert(set_status != SetStatus::kUpdated);
    result.SetInt((set_status == SetStatus::kInserted) ? 1 : 0);
}

void SetRangeFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 4) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto start = ParseInt<uint32_t>(args[2]);
    if (!start.has_value()) {
        result.SetError(Error::kNotAnInt);
        return;
    }
    const auto start_index = start.value();

    auto key = args[1];
    auto [entry, exists] = service.DataHashTable()->FindOrCreate(key, true);
    if (exists) {
        auto expire_entry = service.GetExpireHashTable()->Find(key);
        if (expire_entry != nullptr && expire_entry->value <= service.GetCommandTimeSnapshot()) {
            entry->value.reset();
            service.GetExpireHashTable()->Erase(key);
            exists = false;
        }
    }

    if (!exists || entry->value.use_count() != 1) {
        if (start_index == 0) {
            entry->value = CreateMTSPtr(args[3]);
        } else {
            std::string tmp(start_index + 1, 0);
            tmp += args[3];
            entry->value = CreateMTSPtr(tmp);
        }
    } else {
        if (start_index > entry->value->size()) {
            entry->value->append(start_index - entry->value->size(), 0);
            entry->value->append(args[3]);
        } else {
            entry->value->replace(start_index, entry->value->size() - start_index, args[3]);
        }
    }
    entry->GetKey()->SetLRU(service.GetLRUClock());
    result.SetInt(entry->value->size());
}

void StrlenFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto entry = service.FindOrExpire(args[1]);
    if (entry == nullptr) {
        result.SetInt(0);
        return;
    }
    result.SetInt(entry->value->size());
    entry->GetKey()->SetLRU(service.GetLRUClock());
}

void GetFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }
    detail::GetFunctionBase(service, args[1], result);
}

void MGetFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() < 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        auto key = args[i];
        auto entry = service.FindOrExpire(key);
        if (entry == nullptr) {
            result.AddString(nullptr);
        } else {
            result.AddString(entry->value);
            entry->GetKey()->SetLRU(service.GetLRUClock());
        }
    }
}

void GetDelFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }
    detail::GetFunctionBaseWithCallback(
      service, args[1], result, [&service](MTSHashTable::EntryPointer entry) {
          service.EraseKey(entry->GetKey()->StringView());
      });
}

void GetEXFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() < 2) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    bool persist{false};
    std::optional<TimePoint> expire_time{std::nullopt};
    if (args.size() > 2) {
        for (size_t i = 2; i < args.size(); ++i) {
            if (!args[i].compare("PERSIST")) {
                if (expire_time.has_value() || persist) {
                    result.SetError(Error::kSyntaxError);
                    return;
                }
                persist = true;
                continue;
            }
            const auto expire_result = ExtractExpireOptions(
              args, i, service.GetCommandTimeSnapshot(), result, expire_time);
            switch (expire_result) {
            case ExtractExpireResult::kError:
                return;
            case ExtractExpireResult::kNotFound: {
                result.SetError(Error::kSyntaxError);
                return;
            }
            case ExtractExpireResult::kDone: {
                if (persist) {
                    result.SetError(Error::kSyntaxError);
                    return;
                }
                break;
            }
            }
        }
    }

    detail::GetFunctionBaseWithCallback(
      service, args[1], result, [&service, persist, expire_time](MTSHashTable::EntryPointer entry) {
          auto key = entry->GetKey()->StringView();
          if (persist) {
              service.GetExpireHashTable()->Erase(key);
          } else if (expire_time.has_value()) {
              service.GetExpireHashTable()->Upsert(key, expire_time.value());
          }
      });
}

void GetSetFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 3) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto [set_status, _, old_value] = service.SetData(args[1], args[2], SetMode::kRegular, true);
    assert(set_status != SetStatus::kNoOp);

    if (old_value == nullptr) {
        result.SetNil();
    } else {
        result.SetString(std::move(old_value));
        service.GetExpireHashTable()->Erase(args[1]);
    }
}

void GetRangeFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 4) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto start = ParseInt<int32_t>(args[2]);
    if (!start.has_value()) {
        result.SetError(Error::kNotAnInt);
        return;
    }
    auto end = ParseInt<int32_t>(args[3]);
    if (!end.has_value()) {
        result.SetError(Error::kNotAnInt);
        return;
    }

    auto entry = service.FindOrExpire(args[1]);
    if (entry == nullptr) {
        result.SetString(CreateMTSPtr(""));
        return;
    }

    auto transform_index = [size = entry->value->size()](int32_t index) {
        if (index < 0) {
            index = std::max<int32_t>(0, size + index);
        }
        index = std::min<int32_t>(size, index);
        return index;
    };

    const auto start_index = transform_index(start.value());
    const auto end_index = transform_index(end.value());
    if (start_index == entry->value->size() || end_index <= start_index) {
        result.SetString(CreateMTSPtr(""));
    } else {
        result.SetString(CreateMTSPtr(
          std::string_view(entry->value->data() + start_index, end_index - start_index + 1)));
    }
    entry->GetKey()->SetLRU(service.GetLRUClock());
}

void AppendFunction(DataStructureService& service, Args args, Result& result) {
    if (args.size() != 3) {
        result.SetError(Error::kWrongArgNum);
        return;
    }

    auto key = args[1];
    auto value = args[2];

    auto [entry, exists] = service.DataHashTable()->FindOrCreate(key, true);
    if (!exists) {
        entry->value = CreateMTSPtr(value);
    } else {
        auto& original_value = entry->value;
        if (original_value.use_count() != 1) {
            entry->value = CreateMTSPtr(*original_value);
        }
        entry->value->append(value);
    }
    entry->GetKey()->SetLRU(service.GetLRUClock());
    result.SetInt(entry->value->size());
}

void ExistsFunction(DataStructureService& service, Args args, Result& result) {
    int32_t cnt{0};
    for (size_t i = 1; i < args.size(); ++i) {
        auto entry = service.FindOrExpire(args[i]);
        if (entry != nullptr) {
            entry->GetKey()->SetLRU(service.GetLRUClock());
            ++cnt;
        }
    }
    result.SetInt(cnt);
}

void RegisterStringCommands(DataStructureService* service) {
    service->RegisterCommand("SET", Command("SET").SetHandler(SetFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "SETEX", Command("SETEX").SetHandler(SetEXFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "PSETEX", Command("PSETEX").SetHandler(PSetEXFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "SETNX", Command("SETNX").SetHandler(SetNXFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "SETRANGE", Command("SETRANGE").SetHandler(SetRangeFunction).SetIsWriteCommand());
    service->RegisterCommand("MSET", Command("MSET").SetHandler(MSetFunction).SetIsWriteCommand());
    service->RegisterCommand(
      "MSETNX", Command("MSETNX").SetHandler(MSetNXFunction).SetIsWriteCommand());
    service->RegisterCommand("GET", Command("GET").SetHandler(GetFunction));
    service->RegisterCommand("MGET", Command("MGET").SetHandler(MGetFunction));
    service->RegisterCommand("GETDEL", Command("GETDEL").SetHandler(GetDelFunction));
    service->RegisterCommand("GETEX", Command("GETEX").SetHandler(GetEXFunction));
    service->RegisterCommand("GETSET", Command("GETSET").SetHandler(GetSetFunction));
    service->RegisterCommand("GETRANGE", Command("GETRANGE").SetHandler(GetRangeFunction));
    service->RegisterCommand("SUBSTR", Command("SUBSTR").SetHandler(GetRangeFunction));
    service->RegisterCommand("APPEND", Command("APPEND").SetHandler(AppendFunction));
    service->RegisterCommand("EXISTS", Command("EXISTS").SetHandler(ExistsFunction));
    service->RegisterCommand("STRLEN", Command("STRLEN").SetHandler(StrlenFunction));
}

} // namespace rdss
