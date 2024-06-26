// Copyright (c) usurai.
// Licensed under the MIT license.
#include "data_structure_service.h"

#include "base/config.h"
#include "runtime/ring_executor.h"
#include "runtime/util.h"

#include <glog/logging.h>

namespace rdss {

using SetStatus = DataStructureService::SetStatus;
using SetMode = DataStructureService::SetMode;

DataStructureService::DataStructureService(Config* config, Server* server, Clock* clock)
  : config_(config)
  , server_(server)
  , using_external_clock_(clock != nullptr)
  , clock_(using_external_clock_ ? clock : new Clock(true))
  , evictor_(this)
  , expirer_(this) {}

DataStructureService::~DataStructureService() {
    if (!using_external_clock_) {
        delete clock_;
    }
}

std::future<void> DataStructureService::GetShutdownFuture() {
    if (get_future_called_) {
        LOG(FATAL) << "Repetitive calls to GetShutdownFuture.";
    }
    get_future_called_ = true;
    return shutdown_promise_.get_future();
}

Task<void> DataStructureService::Cron() {
    assert(tls_exr != nullptr);

    // TODO: Adaptive hz
    const auto interval_in_millisecond = 1000 / config_->hz;
    size_t cnt{0};
    while (active_.load(std::memory_order_relaxed)) {
        co_await WaitFor(tls_exr, std::chrono::milliseconds(1));
        UpdateCommandTime();
        if (++cnt < interval_in_millisecond) {
            continue;
        }
        cnt = 0;
        GetEvictor().RefreshLRUClock();
        GetExpirer().ActiveExpire();
        IncrementalRehashing(kIncrementalRehashingTimeLimit);
    }
}

void DataStructureService::RegisterCommand(CommandName name, Command command) {
    std::transform(name.begin(), name.end(), name.begin(), [](char c) { return std::tolower(c); });
    commands_.emplace(name, command);
    std::transform(name.begin(), name.end(), name.begin(), [](char c) { return std::toupper(c); });
    commands_.emplace(std::move(name), std::move(command));
}

void DataStructureService::Invoke(Command::CommandStrings command_strings, Result& result) {
    auto command_itor = commands_.find(command_strings[0]);
    if (command_itor == commands_.end()) {
        result.SetError(Error::kUnknownCommand);
        return;
    }
    auto& command = command_itor->second;

    if (command.IsWriteCommand()) {
        size_t bytes_to_free = evictor_.MaxmemoryExceeded();
        if (bytes_to_free != 0 && !evictor_.Evict(bytes_to_free)) {
            result.SetError(Error::kOOM);
            return;
        }
    }
    command(*this, std::move(command_strings), result);
    stats_.commands_processed.fetch_add(1, std::memory_order_relaxed);
}

MTSHashTable::EntryPointer DataStructureService::FindOrExpire(std::string_view key) {
    auto entry = data_ht_.Find(key);
    if (entry == nullptr) {
        return nullptr;
    }

    // TODO: Add a FindEntryAndProceeding to save an additional Erase call on expired.
    auto* expire_entry = expire_ht_.Find(key);
    const auto expire_found = (expire_entry != nullptr);
    if (!expire_found || GetCommandTimeSnapshot() < expire_entry->value) {
        return entry;
    }

    data_ht_.Erase(key);
    if (expire_found) {
        expire_ht_.Erase(key);
    }
    return nullptr;
}

void DataStructureService::EraseKey(std::string_view key) {
    if (!data_ht_.Erase(key)) {
        return;
    }
    expire_ht_.Erase(key);
}

std::tuple<SetStatus, MTSHashTable::EntryPointer, MTSPtr> DataStructureService::SetData(
  std::string_view key, std::string_view value, SetMode set_mode, bool get) {
    SetStatus set_status{SetStatus::kNoOp};
    MTSPtr old_value{nullptr};
    MTSHashTable::EntryPointer set_entry{nullptr};

    switch (set_mode) {
    case SetMode::kRegular: {
        bool exists{false};
        if (!get) {
            auto upsert_result = data_ht_.Upsert(key, CreateMTSPtr(value));
            set_entry = upsert_result.first;
            exists = upsert_result.second;
        } else {
            auto [entry, exists] = data_ht_.FindOrCreate(key, true);
            if (exists) {
                auto expire_entry = expire_ht_.Find(key);
                if (expire_entry == nullptr || expire_entry->value > GetCommandTimeSnapshot()) {
                    old_value = std::move(entry->value);
                    exists = true;
                }
            }
            entry->value = CreateMTSPtr(value);
            set_entry = entry;
        }
        set_status = (exists) ? SetStatus::kUpdated : SetStatus::kInserted;
        break;
    }
    case SetMode::kNX: {
        auto data_entry = data_ht_.Find(key);
        if (data_entry != nullptr) {
            auto expire_entry = expire_ht_.Find(key);
            if (expire_entry != nullptr && expire_entry->value <= GetCommandTimeSnapshot()) {
                data_entry->value = CreateMTSPtr(value);
                expire_ht_.Erase(key);
                set_entry = data_entry;
                set_status = SetStatus::kInserted;
            }
        } else {
            auto [entry, _] = data_ht_.Insert(key, CreateMTSPtr(value));
            set_entry = entry;
            set_status = SetStatus::kInserted;
        }
        break;
    }
    case SetMode::kXX: {
        auto data_entry = data_ht_.Find(key);
        if (data_entry == nullptr) {
            break;
        }
        auto expire_entry = expire_ht_.Find(key);
        if (expire_entry != nullptr && expire_entry->value <= GetCommandTimeSnapshot()) {
            data_ht_.Erase(key);
            expire_ht_.Erase(key);
            break;
        }
        if (get) {
            old_value = std::move(data_entry->value);
        }
        data_entry->value = CreateMTSPtr(value);
        set_entry = data_entry;
        set_status = SetStatus::kUpdated;
        break;
    }
    }
    if (set_entry != nullptr) {
        set_entry->GetKey()->SetLRU(GetLRUClock());
    }
    return {set_status, set_entry, old_value};
}

void DataStructureService::IncrementalRehashing(std::chrono::steady_clock::duration time_limit) {
    auto rehash = [time_limit](auto table) {
        if (!table->IsRehashing()) {
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        do {
            if (table->RehashSome(100)) {
                break;
            }
        } while (std::chrono::steady_clock::now() - start < time_limit);
    };

    rehash(DataTable());
    rehash(ExpireTable());
}

} // namespace rdss
