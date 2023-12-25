#pragma once

#include "base/clock.h"
#include "command.h"
#include "command_dictionary.h"
#include "data_structure/tracking_hash_table.h"
#include "eviction_strategy.h"
#include "expire_strategy.h"

#include <chrono>
#include <future>
#include <set>

namespace rdss {

struct Config;
class Server;

struct DSSStats {
    std::atomic<uint64_t> commands_processed;
};

class DataStructureService {
public:
    using TimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;
    using ExpireHashTable = HashTable<TimePoint, Mallocator<TimePoint>>;
    static constexpr auto kIncrementalRehashingTimeLimit = std::chrono::milliseconds{1};

public:
    explicit DataStructureService(
      Config* config, Server* server, Clock* clock, std::promise<void> shutdown_promise);

    void Cron();

    Server* GetServer() { return server_; }

    const Config* GetConfig() const { return config_; }

    void RegisterCommand(CommandName name, Command command);

    void Invoke(Command::CommandStrings command_strings, Result& result);

    TimePoint GetCommandTimeSnapshot() const { return command_time_snapshot_; }

    MTSHashTable* DataTable() { return data_ht_.get(); }

    ExpireHashTable* ExpireTable() { return expire_ht_.get(); }

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

    auto GetLRUClock() const { return evictor_.GetLRUClock(); }

    /// Try rehash data / expiry table for 'time_limit' duration if they are rehashing. This is
    /// called at cron.
    void IncrementalRehashing(std::chrono::steady_clock::duration time_limit);

    void Shutdown() { shutdown_promise_.set_value(); }

    Clock* GetClock() { return clock_; }

    DSSStats& Stats() { return stats_; }

    EvictionStrategy& GetEvictor() { return evictor_; }

    ExpireStrategy& GetExpirer() { return expirer_; }

private:
    size_t IsOOM() const;

    Config* config_;
    Server* server_;
    Clock* clock_;
    std::promise<void> shutdown_promise_;
    CommandDictionary commands_;
    std::unique_ptr<MTSHashTable> data_ht_;
    std::unique_ptr<ExpireHashTable> expire_ht_;
    EvictionStrategy evictor_;
    ExpireStrategy expirer_;
    TimePoint command_time_snapshot_;
    DSSStats stats_;
};

} // namespace rdss
