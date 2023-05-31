#pragma once

#include "command.h"
#include "command_dictionary.h"
#include "tracking_hash_table.h"

#include <set>

namespace rdss {

struct Config;

// TODO: Add thread-safe queue command interface, so that multiple read thread can queue to the same
// service simultaneously.
class DataStructureService {
public:
    explicit DataStructureService(Config* config)
      : config_(config)
      , data_(new MTSHashTable()) {}

    Result Invoke(Command::CommandStrings command_strings);

    void RegisterCommand(CommandName name, Command command) {
        commands_.emplace(std::move(name), std::move(command));
    }

    MTSHashTable* HashTable() { return data_.get(); }

private:
    size_t IsOOM() const;
    bool Evict(size_t bytes_to_free);
    MTSHashTable::EntryPointer GetSomeOldEntry(size_t samples);

    Config* config_;
    CommandDictionary commands_;
    std::unique_ptr<MTSHashTable> data_;

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
