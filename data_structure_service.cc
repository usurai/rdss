#include "data_structure_service.h"

#include "config.h"

#include <glog/logging.h>

#include <memory.h>

namespace rdss {

Result DataStructureService::Invoke(Command::CommandStrings command_strings) {
    VLOG(1) << "received " << command_strings.size() << " commands:";
    for (const auto& arg : command_strings) {
        VLOG(1) << arg;
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
            if (data_->Count() == 0) {
                return false;
            }

            TrackingMap::EntryPointer entry = data_->GetRandomEntry();
            if (entry == nullptr) {
                return false;
            }
            auto delta
              = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            // TODO: dont convert to string_view
            VLOG(1) << "Evicting key " << *entry->key;
            data_->Erase(std::string_view(entry->key->data(), entry->key->size()));
            // ++evicted_keys;
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
        if (data_->Count() == 0) {
            return false;
        }
        TrackingMap::EntryPointer entry = GetSomeOldEntry(config_->maxmemory_samples);
        if (entry == nullptr) {
            return false;
        }
        auto delta
          = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
        // TODO: dont convert to string_view
        VLOG(1) << "Evicting key " << *entry->key;
        data_->Erase(std::string_view(entry->key->data(), entry->key->size()));
        // ++evicted_keys;
        delta -= MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
        VLOG(1) << "Freed " << delta << " bytes.";
        freed += delta;
    }
    return true;
}

TrackingMap::EntryPointer DataStructureService::GetSomeOldEntry(size_t samples) {
    assert(eviction_pool_.size() < kEvictionPoolLimit);
    assert(data_->Count() > 0);

    TrackingMap::EntryPointer result{nullptr};
    while (result == nullptr) {
        for (size_t i = 0; i < std::min(samples, data_->Count()); ++i) {
            auto entry = data_->GetRandomEntry();
            assert(entry != nullptr);
            eviction_pool_.emplace(entry->lru, entry->key);
        }

        while (eviction_pool_.size() > kEvictionPoolLimit) {
            auto it = eviction_pool_.end();
            --it;
            eviction_pool_.erase(it);
        }

        while (!eviction_pool_.empty()) {
            auto& [lru, key] = *eviction_pool_.begin();
            auto entry = data_->Find({key->data(), key->size()});
            if (entry == nullptr || entry->lru != lru) {
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

} // namespace rdss
