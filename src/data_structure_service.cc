#include "data_structure_service.h"

#include "config.h"

#include <glog/logging.h>

namespace rdss {

using SetStatus = DataStructureService::SetStatus;
using SetMode = DataStructureService::SetMode;

DataStructureService::DataStructureService(
  Config* config, Server* server, Clock* clock, std::promise<void> shutdown_promise)
  : config_(config)
  , server_(server)
  , clock_(clock)
  , shutdown_promise_(std::move(shutdown_promise))
  , data_ht_(new MTSHashTable())
  , expire_ht_(new ExpireHashTable())
  , evictor_(this) {}

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
    command_time_snapshot_ = clock_->Now();
    command(*this, std::move(command_strings), result);
    stats_.commands_processed.fetch_add(1, std::memory_order_relaxed);
}

void DataStructureService::ActiveExpire() {
    // TODO: Move these to state of new "Expirer" to avoid recomputation.
    const auto time_limit = std::chrono::steady_clock::duration{std::chrono::seconds{1}}
                            * config_->active_expire_cycle_time_percent / 100 / config_->hz;
    const size_t threshold_percentage = config_->active_expire_acceptable_stale_percent;
    const size_t keys_per_loop = config_->active_expire_keys_per_loop;
    const size_t max_samples = expire_ht_->Count();

    size_t sampled_keys{0};
    size_t expired_keys{0};
    const auto start_time = std::chrono::steady_clock::now();
    const auto now = clock_->Now();

    while (true) {
        size_t keys_to_sample = keys_per_loop;
        if (keys_to_sample > expire_ht_->Count()) {
            keys_to_sample = expire_ht_->Count();
        }
        if (keys_to_sample == 0) {
            break;
        }

        size_t sampled_this_iter{0};
        size_t expired_this_iter{0};
        while (sampled_this_iter < keys_to_sample) {
            bucket_index_ = expire_ht_->TraverseBucket(
              bucket_index_,
              [&sampled_this_iter, &expired_this_iter, now, this](
                ExpireHashTable::EntryPointer entry) {
                  assert(entry != nullptr);
                  ++sampled_this_iter;
                  if (entry->value > now) {
                      // TODO: Aggregate how long has it expired.
                      return;
                  }
                  auto key_sv = entry->key->StringView();
                  // TODO: Consolidate the erase method.
                  this->DataTable()->Erase(key_sv);
                  this->ExpireTable()->Erase(key_sv);
                  ++expired_this_iter;
              });

            if (bucket_index_ == 0) {
                break;
            }
        }

        if (sampled_this_iter == 0) {
            break;
        }

        sampled_keys += sampled_this_iter;
        expired_keys += expired_this_iter;
        const auto expired_rate = static_cast<double>(expired_this_iter * 100) / sampled_this_iter;
        const auto elapsed = std::chrono::steady_clock::now() - start_time;

        VLOG(2) << "ActiveExpire loop | sampled:" << sampled_this_iter
                << " expired:" << expired_this_iter << " expired rate:" << expired_rate
                << " elapsed_time:" << elapsed.count();

        if (expired_rate <= static_cast<double>(threshold_percentage)) {
            VLOG(2) << "ActiveExpire quits because expired rate is below " << threshold_percentage;
            break;
        }

        if (elapsed >= time_limit) {
            VLOG(2) << "ActiveExpire quits because timeout.";
            break;
        }

        if (sampled_keys == max_samples) {
            VLOG(2) << "ActiveExpire quits because max_samples reached";
            break;
        }
    }
    active_expired_keys_ += expired_keys;
}

MTSHashTable::EntryPointer DataStructureService::FindOrExpire(std::string_view key) {
    auto entry = data_ht_->Find(key);
    if (entry == nullptr) {
        return nullptr;
    }

    // TODO: Add a FindEntryAndProceeding to save an additional Erase call on expired.
    auto* expire_entry = expire_ht_->Find(key);
    const auto expire_found = (expire_entry != nullptr);
    if (!expire_found || GetCommandTimeSnapshot() < expire_entry->value) {
        return entry;
    }

    data_ht_->Erase(key);
    if (expire_found) {
        expire_ht_->Erase(key);
    }
    return nullptr;
}

void DataStructureService::EraseKey(std::string_view key) {
    if (!data_ht_->Erase(key)) {
        return;
    }
    expire_ht_->Erase(key);
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
            auto upsert_result = data_ht_->Upsert(key, CreateMTSPtr(value));
            set_entry = upsert_result.first;
            exists = upsert_result.second;
        } else {
            auto [entry, exists] = data_ht_->FindOrCreate(key, true);
            if (exists) {
                auto expire_entry = expire_ht_->Find(key);
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
        auto data_entry = data_ht_->Find(key);
        if (data_entry != nullptr) {
            auto expire_entry = expire_ht_->Find(key);
            if (expire_entry != nullptr && expire_entry->value <= GetCommandTimeSnapshot()) {
                data_entry->value = CreateMTSPtr(value);
                expire_ht_->Erase(key);
                set_entry = data_entry;
                set_status = SetStatus::kInserted;
            }
        } else {
            auto [entry, _] = data_ht_->Insert(key, CreateMTSPtr(value));
            set_entry = entry;
            set_status = SetStatus::kInserted;
        }
        break;
    }
    case SetMode::kXX: {
        auto data_entry = data_ht_->Find(key);
        if (data_entry == nullptr) {
            break;
        }
        auto expire_entry = expire_ht_->Find(key);
        if (expire_entry != nullptr && expire_entry->value <= GetCommandTimeSnapshot()) {
            data_ht_->Erase(key);
            expire_ht_->Erase(key);
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
