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
    using LastAccessTimePoint = MTSHashTable::EntryType::KeyType::LastAccessTimePoint;
    using LastAccessTimeDuration = MTSHashTable::EntryType::KeyType::LastAccessTimeDuration;

public:
    explicit DataStructureService(Config* config, Clock* clock)
      : config_(config)
      , clock_(clock)
      , data_ht_(new MTSHashTable())
      , expire_ht_(new ExpireHashTable()) {
        RefreshLRUClock();
    }

    void Invoke(Command::CommandStrings command_strings, Result& result);

    void RegisterCommand(CommandName name, Command command) {
        std::transform(
          name.begin(), name.end(), name.begin(), [](char c) { return std::tolower(c); });
        commands_.emplace(name, command);
        std::transform(
          name.begin(), name.end(), name.begin(), [](char c) { return std::toupper(c); });
        commands_.emplace(std::move(name), std::move(command));
    }

    /// Find and return the entry of 'key' if it's valid. Expire the key if it's stale.
    MTSHashTable::EntryPointer FindOrExpire(std::string_view key);

    enum class SetMode {
        kRegular, /*** Update if key presents, insert otherwise ***/
        kNX,      /*** Only insert if key doesn't present ***/
        kXX       /*** Only update if key presents ***/
    };

    enum class SetStatus { kNoOp, kInserted, kUpdated };

    /// Set 'key' 'value' pair in data table with respect to 'set_mode'. Return the result of the
    /// operation, the entry of 'key', and if 'get' is true and 'key' exists, return the old value
    /// of 'key'.
    std::tuple<SetStatus, MTSHashTable::EntryPointer, MTSPtr>
    SetData(std::string_view key, std::string_view value, SetMode set_mode, bool get);

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

    LastAccessTimePoint GetLRUClock() const { return lru_clock_; }

    void RefreshLRUClock() {
        lru_clock_ = std::chrono::time_point_cast<LastAccessTimeDuration>(
          LastAccessTimePoint::clock::now());
    }

    size_t GetEvictedKeys() const { return evicted_keys_; }

    /// Try rehash data / expiry table for 'time_limit' duration if they are rehashing. This is
    /// called at cron.
    void IncrementalRehashing(std::chrono::steady_clock::duration time_limit);

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
    using LRUEntry = std::pair<LastAccessTimePoint, MTSHashTable::EntryType::KeyPointer>;
    struct CompareLRUEntry {
        constexpr bool operator()(const LRUEntry& lhs, const LRUEntry& rhs) const {
            return lhs.first < rhs.first;
        }
    };
    static constexpr size_t kEvictionPoolLimit = 16;
    std::set<LRUEntry, CompareLRUEntry> eviction_pool_;
    LastAccessTimePoint lru_clock_;
    size_t evicted_keys_{0};

    // Index of next bucket of expire table to scan for expired key.
    size_t bucket_index_{0};

public:
    // TODO: move somewhere
    size_t active_expired_keys_{0};
};

} // namespace rdss
