#include "config.h"

#include "external/tortellini.hh"

#include <glog/logging.h>

#include <fstream>
#include <sstream>

namespace rdss {

MaxmemoryPolicy MaxmemoryPolicyStrToEnum(const std::string& str) {
    if (str == "noeviction") {
        return MaxmemoryPolicy::kNoEviction;
    }
    if (!str.compare("allkeys-random")) {
        return MaxmemoryPolicy::kAllKeysRandom;
    }
    if (!str.compare("allkeys-lru")) {
        return MaxmemoryPolicy::kAllKeysLru;
    }
    LOG(FATAL) << "Unknown maxmemory-policy: " << str;
}

std::string MaxmemoryPolicyEnumToStr(MaxmemoryPolicy policy) {
    switch (policy) {
    case MaxmemoryPolicy::kNoEviction:
        return "noeviction";
    case MaxmemoryPolicy::kAllKeysRandom:
        return "allkeys-random";
    case MaxmemoryPolicy::kAllKeysLru:
        return "allkeys-lru";
    default:
        return "Unknown policy";
    }
}

void Config::ReadFromFile(const std::string& file_name) {
    std::ifstream in(file_name);

    tortellini::ini ini;
    in >> ini;

    auto global_section = ini[""];

    port = static_cast<uint16_t>(global_section["port"] | 6379U);

    hz = global_section["hz"] | 10U;

    maxclients = global_section["maxclients"] | 10000U;
    if (maxclients == 0U) {
        maxclients = 10000U;
    }

    maxmemory = global_section["maxmemory"] | maxmemory;

    auto maxmemory_policy_str = global_section["maxmemory-policy"] | "noeviction";
    maxmemory_policy = MaxmemoryPolicyStrToEnum(maxmemory_policy_str);

    maxmemory_samples = global_section["maxmemory-samples"] | 5U;

    active_expire_cycle_time_percent = global_section["active_expire_cycle_time_percent"] | 25U;
    active_expire_acceptable_stale_percent
      = global_section["active_expire_acceptable_stale_percent"] | 10U;
    active_expire_keys_per_loop = global_section["active_expire_keys_per_loop"] | 20U;

    client_executors = global_section["client_executors"] | 2U;
    sqpoll = global_section["sqpoll"] | false;
    max_direct_fds_per_exr = global_section["max_direct_fds_per_exr"] | 4096U;

    use_ring_buffer = global_section["use_ring_buffer"] | true;

    wait_batch_size = global_section["wait_batch_size"] | 1U;

    submit_batch_size = global_section["submit_batch_size"] | 32U;
}

void Config::SanityCheck() {
    if (active_expire_cycle_time_percent == 0 || active_expire_cycle_time_percent > 40) {
        LOG(FATAL) << "active_expire_cycle_time_percent is out of range, it should be in [1, 40]";
    }
    if (active_expire_acceptable_stale_percent > 100) {
        LOG(FATAL)
          << "active_expire_acceptable_stale_percent is out of range, it should be in [0, 100]";
    }
}

std::string Config::ToString() const {
    std::stringstream stream;
    stream << "Configs: [";
    stream << "port:" << port << ", ";
    stream << "hz:" << hz << ", ";
    stream << "maxclients:" << maxclients << ", ";
    stream << "maxmemory:" << maxmemory << ", ";
    stream << "maxmemory-policy:" << MaxmemoryPolicyEnumToStr(maxmemory_policy) << ", ";
    stream << "maxmemory-samples:" << maxmemory_samples << ", ";
    stream << "active_expire_cycle_time_percent:" << active_expire_cycle_time_percent << ", ";
    stream << "active_expire_acceptable_stale_percent:" << active_expire_acceptable_stale_percent
           << ", ";
    stream << "active_expire_keys_per_loop:" << active_expire_keys_per_loop << ",";
    stream << "client_executors:" << client_executors << ", ";
    stream << "sqpoll:" << sqpoll << ", ";
    stream << "submit_batch_size:" << submit_batch_size << ", ";
    stream << "wait_batch_size:" << wait_batch_size;

    stream << "].";
    return stream.str();
}

} // namespace rdss
