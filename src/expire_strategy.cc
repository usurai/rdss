#include "expire_strategy.h"

#include "config.h"
#include "data_structure_service.h"

namespace rdss {

ExpireStrategy::ExpireStrategy(DataStructureService* service)
  : service_(service)
  , config_(service_->GetConfig())
  , threshold_percentage_(config_->active_expire_acceptable_stale_percent)
  , keys_per_loop_(config_->active_expire_keys_per_loop) {}

void ExpireStrategy::ActiveExpire() {
    auto* data_ht = service_->DataTable();
    auto* expire_ht = service_->ExpireTable();
    const auto time_limit = std::chrono::steady_clock::duration{std::chrono::seconds{1}}
                            * config_->active_expire_cycle_time_percent / 100 / config_->hz;
    const size_t max_samples = expire_ht->Count();

    size_t sampled_keys{0};
    size_t expired_keys{0};
    const auto start_time = std::chrono::steady_clock::now();
    const auto now = service_->GetClock()->Now();

    while (true) {
        size_t keys_to_sample = keys_per_loop_;
        if (keys_to_sample > expire_ht->Count()) {
            keys_to_sample = expire_ht->Count();
        }
        if (keys_to_sample == 0) {
            stats_.expired_stale_perc.store(0, std::memory_order_relaxed);
            break;
        }

        size_t sampled_this_iter{0};
        size_t expired_this_iter{0};
        while (sampled_this_iter < keys_to_sample) {
            bucket_index_ = expire_ht->TraverseBucket(
              bucket_index_,
              [&sampled_this_iter, &expired_this_iter, now, service = service_](
                DataStructureService::ExpireHashTable::EntryPointer entry) {
                  assert(entry != nullptr);
                  ++sampled_this_iter;
                  if (entry->value > now) {
                      // TODO: Aggregate how long has it expired.
                      return;
                  }
                  auto key_sv = entry->key->StringView();
                  // TODO: Consolidate the erase method.
                  service->DataTable()->Erase(key_sv);
                  service->ExpireTable()->Erase(key_sv);
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
        stats_.expired_stale_perc.store(
          static_cast<uint32_t>(expired_rate), std::memory_order_relaxed);
        const auto elapsed = std::chrono::steady_clock::now() - start_time;
        stats_.elapsed_time.fetch_add(elapsed.count(), std::memory_order_relaxed);

        VLOG(2) << "ActiveExpire loop | sampled:" << sampled_this_iter
                << " expired:" << expired_this_iter << " expired rate:" << expired_rate
                << " elapsed_time:" << elapsed.count();

        if (expired_rate <= static_cast<double>(threshold_percentage_)) {
            VLOG(2) << "ActiveExpire quits because expired rate is below " << threshold_percentage_;
            break;
        }

        if (elapsed >= time_limit) {
            stats_.expired_time_cap_reached_count.fetch_add(1, std::memory_order_relaxed);
            VLOG(2) << "ActiveExpire quits because timeout.";
            break;
        }

        if (sampled_keys == max_samples) {
            VLOG(2) << "ActiveExpire quits because max_samples reached";
            break;
        }
    }
    stats_.active_expired_keys.fetch_add(expired_keys, std::memory_order_relaxed);
}

} // namespace rdss
