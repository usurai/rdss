#include "string_commands.h"

#include "command.h"
#include "data_structure_service.h"

#include <charconv>
#include <limits>
#include <optional>

namespace rdss {

using TimePoint = DataStructureService::TimePoint;

static constexpr TimePoint::rep kRepMax = std::numeric_limits<TimePoint::rep>::max();

enum class SetMode { kRegular, kNX, kXX };

Result SetFunction(DataStructureService& service, Command::CommandStrings args) {
    Result result;

    const auto cmd_time = service.GetCommandTimeSnapshot();

    SetMode set_mode{SetMode::kRegular};
    std::optional<TimePoint> expire_time{std::nullopt};
    bool keep_ttl{false};
    bool get{false};
    if (args.size() > 3) {
        std::function<std::optional<TimePoint>(TimePoint::rep)> rep_to_tp;

        for (size_t i = 3; i < args.size(); ++i) {
            if (!args[i].compare("GET")) {
                get = true;
                continue;
            } else if (!args[i].compare("NX")) {
                if (set_mode != SetMode::kRegular) {
                    result.Add("syntax error");
                    return result;
                }
                set_mode = SetMode::kNX;
                continue;
            } else if (!args[i].compare("XX")) {
                if (set_mode != SetMode::kRegular) {
                    result.Add("syntax error");
                    return result;
                }
                set_mode = SetMode::kXX;
                continue;
            } else if (!args[i].compare("KEEPTTL")) {
                if (expire_time.has_value()) {
                    result.Add("syntax error");
                    return result;
                }
                keep_ttl = true;
                continue;
            } else if (!args[i].compare("PX")) {
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
                rep_to_tp = [](TimePoint::rep rep) {
                    return TimePoint{std::chrono::milliseconds{rep}};
                };
            } else if (!args[i].compare("EXAT")) {
                rep_to_tp = [](TimePoint::rep rep) -> std::optional<TimePoint> {
                    if (kRepMax / 1000 < rep) {
                        return std::nullopt;
                    }
                    return TimePoint{std::chrono::seconds{rep}};
                };
            } else {
                result.Add("syntax error");
                return result;
            }

            if (i == args.size() - 1) {
                result.Add("syntax error");
                return result;
            }
            if (expire_time.has_value() || keep_ttl) {
                result.Add("syntax error");
                return result;
            }

            TimePoint::rep ll;
            auto expire_sv = args[i + 1];
            auto [ptr, err] = std::from_chars(
              expire_sv.data(), expire_sv.data() + expire_sv.size(), ll);
            if (err != std::errc{} || ptr != expire_sv.data() + expire_sv.size() || ll <= 0) {
                result.Add("value is not an integer or out of range");
                return result;
            }
            expire_time = rep_to_tp(ll);
            if (!expire_time.has_value()) {
                result.Add("value is not an integer or out of range");
                return result;
            }
            VLOG(1) << "expire time:\t\t" << expire_time.value().time_since_epoch().count();
            i += 1;
        }
    }

    VLOG(1) << "SetMode:" << static_cast<int>(set_mode);

    auto key = args[1];
    auto value = args[2];

    auto* data_ht = service.DataHashTable();
    auto* expire_ht = service.GetExpireHashTable();
    bool inserted{false};
    bool overwritten{false};
    MTSPtr old_value{nullptr};
    {
        switch (set_mode) {
        case SetMode::kRegular: {
            bool exists{false};
            if (!get) {
                auto upsert_result = data_ht->Upsert(key, CreateMTSPtr(value));
                exists = upsert_result.second;
            } else {
                auto fc_result = data_ht->FindOrCreate(key, true);
                if (fc_result.second) {
                    old_value = std::move(fc_result.first->value);
                    exists = true;
                }
                fc_result.first->value = CreateMTSPtr(value);
            }
            if (!exists) {
                inserted = true;
            } else {
                overwritten = true;
            }
            break;
        }
        case SetMode::kNX: {
            auto data_entry = data_ht->Find(key);
            if (data_entry != nullptr) {
                auto expire_entry = expire_ht->Find(key);
                if (expire_entry != nullptr && expire_entry->value <= cmd_time) {
                    data_entry->value = CreateMTSPtr(value);
                    inserted = true;
                }
            } else {
                data_ht->Insert(key, CreateMTSPtr(value));
                inserted = true;
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
            overwritten = true;
            break;
        }
        }
    }

    if (!inserted && !overwritten) {
        result.AddNull();
        return result;
    }

    if (expire_time.has_value()) {
        expire_ht->Upsert(key, expire_time.value());
    } else if (overwritten && !keep_ttl) {
        // TODO: maybe we can know there is no expire_entry before this.
        expire_ht->Erase(key);
    }

    VLOG(1) << "inserted:" << inserted << " overwritten:" << overwritten;

    // TODO: lru

    if (get) {
        if (old_value == nullptr) {
            result.AddNull();
        } else {
            result.Add(std::string(*old_value));
        }
    } else {
        result.Add("OK");
    }
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
