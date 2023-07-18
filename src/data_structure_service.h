#pragma once

#include "base/clock.h"
#include "command.h"
#include "command_dictionary.h"
#include "data_structure/tracking_hash_table.h"

#include <chrono>
#include <set>

namespace rdss {

struct Config;

// TODO: Add thread-safe queue command interface, so that multiple read thread can queue to the same
// service simultaneously.
class DataStructureService {
public:
    using TimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;
    using ExpireHashTable = HashTable<TimePoint, Mallocator<TimePoint>>;

public:
    explicit DataStructureService(Config* config, Clock* clock)
      : config_(config)
      , clock_(clock)
      , data_ht_(new MTSHashTable())
      , expire_ht_(new ExpireHashTable()) {}

    Result Invoke(Command::CommandStrings command_strings);

    void RegisterCommand(CommandName name, Command command) {
        commands_.emplace(std::move(name), std::move(command));
    }

    /// Find and return the entry of 'key' if it's valid. Expire the key if it's stale.
    MTSHashTable::EntryPointer FindOrExpire(std::string_view key);

    /// Erase key in both data and expire table.
    void EraseKey(std::string_view key);

    MTSHashTable* DataHashTable() { return data_ht_.get(); }

    ExpireHashTable* GetExpireHashTable() { return expire_ht_.get(); }

    TimePoint GetCommandTimeSnapshot() const { return command_time_snapshot_; }

    /// Triggers a cycle of active expiration. The expiration process will repeatly scan a portion
    /// of volatile keys, and erase the expired keys. The process only stops in one of the following
    /// condition: 1. The expired rate of the last iteration is below the threshold specified by
    /// 'active_expire_acceptable_stale_percent'. 2. The elapsed time of the process has exceeded
    /// the time limit specified by 'active_expire_cycle_time_percent'. 3. The whole table has been
    /// scanned.
    void ActiveExpire();

private:
    size_t IsOOM() const;
    bool Evict(size_t bytes_to_free);
    MTSHashTable::EntryPointer GetSomeOldEntry(size_t samples);

    Config* config_;
    Clock* clock_;
    CommandDictionary commands_;
    std::unique_ptr<MTSHashTable> data_ht_;
    std::unique_ptr<ExpireHashTable> expire_ht_;

    TimePoint command_time_snapshot_;

    // LRU-related
    using DurationCount = int64_t;
    using LRUEntry = std::pair<DurationCount, MTSHashTable::EntryType::KeyPointer>;
    struct CompareLRUEntry {
        constexpr bool operator()(const LRUEntry& lhs, const LRUEntry& rhs) const {
            return lhs.first < rhs.first;
        }
    };
    static constexpr size_t kEvictionPoolLimit = 16;
    std::set<LRUEntry, CompareLRUEntry> eviction_pool_;

    // Index of next bucket of expire table to scan for expired key.
    size_t bucket_index_{0};

public:
    size_t active_expired_keys_{0};

    // TODO: move somewhere
    DurationCount lru_clock_{0};
};

} // namespace rdss
