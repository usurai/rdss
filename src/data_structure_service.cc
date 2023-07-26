#include "data_structure_service.h"

#include "config.h"

#include <glog/logging.h>

#include <chrono>
#include <memory.h>

namespace rdss {

using SetStatus = DataStructureService::SetStatus;
using SetMode = DataStructureService::SetMode;

Result DataStructureService::Invoke(Command::CommandStrings command_strings) {
    VLOG(3) << "received " << command_strings.size() << " commands:";
    for (const auto& arg : command_strings) {
        VLOG(3) << arg;
    }

    Result result;

    auto command_itor = commands_.find(command_strings[0]);
    if (command_itor == commands_.end()) {
        // TODO: this should be error
        result.Add("command not found");
        return result;
    }
    auto& command = command_itor->second;

    if (command.IsWriteCommand()) {
        size_t bytes_to_free = IsOOM();
        if (bytes_to_free != 0 && !Evict(bytes_to_free)) {
            // TODO: this should be error
            result.Add("OOM");
            return result;
        }
    }

    command_time_snapshot_ = clock_->Now();

    result = command(*this, std::move(command_strings));
    return result;
}

size_t DataStructureService::IsOOM() const {
    if (config_->maxmemory == 0) {
        return 0;
    }

    const auto allocated
      = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kAll>();
    if (allocated < config_->maxmemory) {
        return 0;
    }
    return allocated - config_->maxmemory;
}

bool DataStructureService::Evict(size_t bytes_to_free) {
    assert(bytes_to_free != 0);
    VLOG(1) << "Start eviction, policy:" << MaxmemoryPolicyEnumToStr(config_->maxmemory_policy)
            << ", bytes_to_free:" << bytes_to_free;

    if (config_->maxmemory_policy == MaxmemoryPolicy::kNoEviction) {
        return false;
    }

    if (config_->maxmemory_policy == MaxmemoryPolicy::kAllKeysRandom) {
        size_t freed = 0;
        while (freed < bytes_to_free) {
            if (data_ht_->Count() == 0) {
                return false;
            }

            MTSHashTable::EntryPointer entry = data_ht_->GetRandomEntry();
            if (entry == nullptr) {
                return false;
            }
            auto delta
              = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            // TODO: dont convert to string_view
            VLOG(1) << "Evicting key " << entry->GetKey()->StringView();
            expire_ht_->Erase(entry->GetKey()->StringView());
            data_ht_->Erase(entry->GetKey()->StringView());
            ++evicted_keys_;
            delta
              -= MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            VLOG(1) << "Freed " << delta << " bytes.";
            freed += delta;
        }
        return true;
    }

    assert(config_->maxmemory_policy == MaxmemoryPolicy::kAllKeysLru);
    size_t freed = 0;
    while (freed < bytes_to_free) {
        if (data_ht_->Count() == 0) {
            return false;
        }
        MTSHashTable::EntryPointer entry = GetSomeOldEntry(config_->maxmemory_samples);
        if (entry == nullptr) {
            return false;
        }
        auto delta
          = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
        // TODO: dont convert to string_view
        VLOG(1) << "Evicting key " << entry->GetKey()->StringView();
        expire_ht_->Erase(entry->GetKey()->StringView());
        data_ht_->Erase(entry->GetKey()->StringView());
        ++evicted_keys_;
        delta -= MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
        VLOG(1) << "Freed " << delta << " bytes.";
        freed += delta;
    }
    return true;
}

// TODO: Current implementation doesn't care execution time. Consider stop eviction after some
// time or attempts.
MTSHashTable::EntryPointer DataStructureService::GetSomeOldEntry(size_t samples) {
    assert(eviction_pool_.size() < kEvictionPoolLimit);
    assert(data_ht_->Count() > 0);

    MTSHashTable::EntryPointer result{nullptr};
    while (result == nullptr) {
        for (size_t i = 0; i < std::min(samples, data_ht_->Count()); ++i) {
            auto entry = data_ht_->GetRandomEntry();
            assert(entry != nullptr);
            eviction_pool_.emplace(entry->GetKey()->GetLRU(), entry->CopyKey());
        }

        while (eviction_pool_.size() > kEvictionPoolLimit) {
            auto it = eviction_pool_.end();
            --it;
            eviction_pool_.erase(it);
        }

        while (!eviction_pool_.empty()) {
            auto& [lru, key] = *eviction_pool_.begin();
            auto entry = data_ht_->Find(key->StringView());
            if (entry == nullptr || entry->GetKey()->GetLRU() != lru) {
                eviction_pool_.erase(eviction_pool_.begin());
                continue;
            }
            result = entry;
            eviction_pool_.erase(eviction_pool_.begin());
            break;
        }
    }
    return result;
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
                  this->DataHashTable()->Erase(key_sv);
                  this->GetExpireHashTable()->Erase(key_sv);
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

} // namespace rdss
