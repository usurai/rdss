#pragma once

#include "tortellini.hh"

#include <glog/logging.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace rdss {

enum class MaxmemoryPolicy { kNoEviction, kAllKeysRandom, kAllKeysLru };

MaxmemoryPolicy MaxmemoryPolicyStrToEnum(const std::string& str);

std::string MaxmemoryPolicyEnumToStr(MaxmemoryPolicy policy);

struct Config {
    uint32_t port = 6379;
    uint32_t hz = 10;
    uint32_t maxclients = 10000;
    uint64_t maxmemory = 0;
    MaxmemoryPolicy maxmemory_policy = MaxmemoryPolicy::kNoEviction;
    uint32_t maxmemory_samples = 5;

    uint32_t active_expire_cycle_time_percent = 25;
    uint32_t active_expire_acceptable_stale_percent = 10;
    uint32_t active_expire_keys_per_loop = 20;

    // TODO: Limit this according to CPU number.
    size_t io_uring_wq_max_unbound_workers = 16;

    void ReadFromFile(const std::string& file_name);

    void SanityCheck();

    std::string ToString() const;
};

} // namespace rdss
