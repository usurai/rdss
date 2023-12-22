#include "eviction_strategy.h"

#include "data_structure_service.h"

namespace rdss::detail {

using LastAccessTimePoint = MTSHashTable::EntryType::KeyType::LastAccessTimePoint;
using LastAccessTimeDuration = MTSHashTable::EntryType::KeyType::LastAccessTimeDuration;

auto Now() {
    return std::chrono::time_point_cast<LastAccessTimeDuration>(LastAccessTimePoint::clock::now());
}

} // namespace rdss::detail

namespace rdss {

EvictionStrategy::EvictionStrategy(DataStructureService* service)
  : service_(service)
  , maxmemory_policy_(service_->GetConfig()->maxmemory_policy)
  , maxmemory_(service_->GetConfig()->maxmemory)
  , maxmemory_samples_(service_->GetConfig()->maxmemory_samples)
  , lru_clock_(detail::Now()) {}

void EvictionStrategy::RefreshLRUClock() { lru_clock_ = detail::Now(); }

size_t EvictionStrategy::MaxmemoryExceeded() const {
    if (maxmemory_ == 0) {
        return 0;
    }

    const auto allocated
      = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kAll>();
    if (allocated <= maxmemory_) {
        return 0;
    }
    return allocated - maxmemory_;
}

bool EvictionStrategy::Evict(size_t bytes_to_free) {
    assert(bytes_to_free != 0);
    auto* data_ht = service_->DataTable();
    auto* expire_ht = service_->ExpireTable();

    VLOG(1) << "Start eviction, policy:" << MaxmemoryPolicyEnumToStr(maxmemory_policy_)
            << ", bytes_to_free:" << bytes_to_free;

    switch (maxmemory_policy_) {
    case (MaxmemoryPolicy::kNoEviction):
        return false;

    case (MaxmemoryPolicy::kAllKeysRandom): {
        size_t freed = 0;
        while (freed < bytes_to_free) {
            if (data_ht->Count() == 0) {
                return false;
            }

            MTSHashTable::EntryPointer entry = data_ht->GetRandomEntry();
            if (entry == nullptr) {
                return false;
            }
            auto delta
              = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            // TODO: dont convert to string_view
            VLOG(1) << "Evicting key " << entry->GetKey()->StringView();
            expire_ht->Erase(entry->GetKey()->StringView());
            data_ht->Erase(entry->GetKey()->StringView());
            evicted_keys_.fetch_add(1, std::memory_order_relaxed);
            delta
              -= MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            VLOG(1) << "Freed " << delta << " bytes.";
            freed += delta;
        }
        return true;
    }
    case (MaxmemoryPolicy::kAllKeysLru): {
        size_t freed = 0;
        while (freed < bytes_to_free) {
            if (data_ht->Count() == 0) {
                return false;
            }
            MTSHashTable::EntryPointer entry = GetSomeOldEntry(maxmemory_samples_);
            if (entry == nullptr) {
                return false;
            }
            auto delta
              = MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            // TODO: dont convert to string_view
            VLOG(1) << "Evicting key " << entry->GetKey()->StringView();
            expire_ht->Erase(entry->GetKey()->StringView());
            data_ht->Erase(entry->GetKey()->StringView());
            evicted_keys_.fetch_add(1, std::memory_order_relaxed);
            delta
              -= MemoryTracker::GetInstance().GetAllocated<MemoryTracker::Category::kMallocator>();
            VLOG(1) << "Freed " << delta << " bytes.";
            freed += delta;
        }
        return true;
    }
    }
}

// TODO: Current implementation doesn't care execution time. Consider stop eviction after some
// time or attempts.
MTSHashTable::EntryPointer EvictionStrategy::GetSomeOldEntry(size_t samples) {
    auto* data_ht = service_->DataTable();

    assert(eviction_pool_.size() < kEvictionPoolLimit);
    assert(data_ht->Count() > 0);

    MTSHashTable::EntryPointer result{nullptr};
    while (result == nullptr) {
        for (size_t i = 0; i < std::min(samples, data_ht->Count()); ++i) {
            auto entry = data_ht->GetRandomEntry();
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
            auto entry = data_ht->Find(key->StringView());
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

} // namespace rdss
