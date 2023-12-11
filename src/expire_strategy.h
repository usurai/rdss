#pragma once

#include <atomic>
#include <cstddef>

namespace rdss {

class DataStructureService;
class Config;

struct ExpireStats {
    std::atomic<size_t> active_expired_keys{0};
};

class ExpireStrategy {
public:
    explicit ExpireStrategy(DataStructureService*);

    /// Triggers a cycle of active expiration. The expiration process will repeatly scan a portion
    /// of volatile keys, and erase the expired keys. The process only stops in one of the following
    /// condition: 1. The expired rate of the last iteration is below the threshold specified by
    /// 'active_expire_acceptable_stale_percent'. 2. The elapsed time of the process has exceeded
    /// the time limit specified by 'active_expire_cycle_time_percent'. 3. The whole table has been
    /// scanned.
    void ActiveExpire();

    const ExpireStats& GetStats() const { return stats_; }

private:
    DataStructureService* service_;
    const Config* config_;
    // Index of next bucket of expire table to scan for expired key.
    size_t bucket_index_{0};

    size_t threshold_percentage_;
    size_t keys_per_loop_;

    ExpireStats stats_;
};

} // namespace rdss
