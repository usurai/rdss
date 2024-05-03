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

    auto redis_section = ini["redis"];

    port = static_cast<uint16_t>(redis_section["port"] | 6379U);

    hz = redis_section["hz"] | 10U;

    maxclients = redis_section["maxclients"] | 10000U;
    if (maxclients == 0U) {
        maxclients = 10000U;
    }

    maxmemory = redis_section["maxmemory"] | maxmemory;

    auto maxmemory_policy_str = redis_section["maxmemory-policy"] | "noeviction";
    maxmemory_policy = MaxmemoryPolicyStrToEnum(maxmemory_policy_str);

    maxmemory_samples = redis_section["maxmemory-samples"] | 5U;

    active_expire_cycle_time_percent = redis_section["active_expire_cycle_time_percent"] | 25U;
    active_expire_acceptable_stale_percent = redis_section["active_expire_acceptable_stale_percent"]
                                             | 10U;
    active_expire_keys_per_loop = redis_section["active_expire_keys_per_loop"] | 20U;

    auto rdss_section = ini["rdss"];

    client_executors = rdss_section["client_executors"] | 2U;
    sqpoll = rdss_section["sqpoll"] | false;
    max_direct_fds_per_exr = rdss_section["max_direct_fds_per_exr"] | 4096U;

    use_ring_buffer = rdss_section["use_ring_buffer"] | true;

    wait_batch_size = rdss_section["wait_batch_size"] | 1U;

    submit_batch_size = rdss_section["submit_batch_size"] | 32U;
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
    stream << "max_direct_fds_per_exr:" << max_direct_fds_per_exr << ", ";
    stream << "use_ring_buffer:" << use_ring_buffer << ", ";
    stream << "submit_batch_size:" << submit_batch_size << ", ";
    stream << "wait_batch_size:" << wait_batch_size;

    stream << "].";
    return stream.str();
}

// static
Config Config::DisableSqpoll(const Config& src) {
    Config result{src};
    result.sqpoll = false;
    return result;
}

} // namespace rdss
