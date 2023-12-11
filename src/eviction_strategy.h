#pragma once

#include "config.h"
#include "data_structure/tracking_hash_table.h"

#include <set>

namespace rdss {

class DataStructureService;

class EvictionStrategy {
public:
    using LastAccessTimePoint = MTSHashTable::EntryType::KeyType::LastAccessTimePoint;

    explicit EvictionStrategy(DataStructureService* service);

    auto GetLRUClock() const { return lru_clock_; }

    void RefreshLRUClock();

    size_t MaxmemoryExceeded() const;

    bool Evict(size_t bytes_to_free);

    size_t GetEvictedKeys() const { return evicted_keys_; }

private:
    static constexpr size_t kEvictionPoolLimit = 16;
    using LRUEntry = std::pair<LastAccessTimePoint, MTSHashTable::EntryType::KeyPointer>;

    struct CompareLRUEntry {
        constexpr bool operator()(const LRUEntry& lhs, const LRUEntry& rhs) const {
            return lhs.first < rhs.first;
        }
    };

    MTSHashTable::EntryPointer GetSomeOldEntry(size_t samples);

    DataStructureService* service_;
    MaxmemoryPolicy maxmemory_policy_;
    size_t maxmemory_;
    size_t maxmemory_samples_;
    LastAccessTimePoint lru_clock_;
    std::set<LRUEntry, CompareLRUEntry> eviction_pool_;
    std::atomic<size_t> evicted_keys_{0};
};

} // namespace rdss
