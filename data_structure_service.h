#pragma once

#include "command.h"
#include "command_dictionary.h"
#include "tracking_hash_table.h"

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
    explicit DataStructureService(Config* config)
      : config_(config)
      , data_ht_(new MTSHashTable())
      , expire_ht_(new ExpireHashTable()) {}

    Result Invoke(Command::CommandStrings command_strings);

    void RegisterCommand(CommandName name, Command command) {
        commands_.emplace(std::move(name), std::move(command));
    }

    MTSHashTable* DataHashTable() { return data_ht_.get(); }

    ExpireHashTable* GetExpireHashTable() { return expire_ht_.get(); }

    TimePoint GetCommandTimeSnapshot() const { return command_time_snapshot_; }

private:
    size_t IsOOM() const;
    bool Evict(size_t bytes_to_free);
    MTSHashTable::EntryPointer GetSomeOldEntry(size_t samples);

    Config* config_;
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

    // TODO: move somewhere
public:
    DurationCount lru_clock_{0};
};

} // namespace rdss
