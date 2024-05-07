// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <cstdint>
#include <string>

namespace rdss {

enum class MaxmemoryPolicy { kNoEviction, kAllKeysRandom, kAllKeysLru };

MaxmemoryPolicy MaxmemoryPolicyStrToEnum(const std::string& str);

std::string MaxmemoryPolicyEnumToStr(MaxmemoryPolicy policy);

struct Config {
    /// Redis config
    uint16_t port = 6379U;
    uint32_t hz = 10U;
    uint32_t maxclients = 10000U;
    uint64_t maxmemory = 0UL;
    MaxmemoryPolicy maxmemory_policy = MaxmemoryPolicy::kNoEviction;
    uint32_t maxmemory_samples = 5U;
    uint32_t active_expire_cycle_time_percent = 25U;
    uint32_t active_expire_acceptable_stale_percent = 10U;
    uint32_t active_expire_keys_per_loop = 20U;

    /// rdss-specific config
    // TODO: sanity check
    uint32_t client_executors = 2;
    bool sqpoll = false;
    uint32_t max_direct_fds_per_exr = 4096;
    bool use_ring_buffer = true;
    uint32_t submit_batch_size = 32;
    uint32_t wait_batch_size = 1;

    void ReadFromFile(const std::string& file_name);

    void SanityCheck();

    std::string ToString() const;

    /// Clones a config based on the given one but turns off sqpoll. Used for creating I/O
    /// executors.
    static Config DisableSqpoll(const Config&);
};

} // namespace rdss
