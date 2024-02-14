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

    port = global_section["port"] | 6379;

    hz = global_section["hz"] | 10;

    maxclients = global_section["maxclients"] | 10000;
    if (maxclients == 0) {
        maxclients = 10000;
    }

    maxmemory = global_section["maxmemory"] | maxmemory;

    auto maxmemory_policy_str = global_section["maxmemory-policy"] | "noeviction";
    maxmemory_policy = MaxmemoryPolicyStrToEnum(maxmemory_policy_str);

    maxmemory_samples = global_section["maxmemory-samples"] | 5;

    active_expire_cycle_time_percent = global_section["active_expire_cycle_time_percent"] | 25;
    active_expire_acceptable_stale_percent
      = global_section["active_expire_acceptable_stale_percent"] | 10;
    active_expire_keys_per_loop = global_section["active_expire_keys_per_loop"] | 20;

    io_uring_wq_max_unbound_workers = global_section["io_uring_wq_max_unbound_workers"] | 16;
    client_executors = global_section["client_executors"] | 2;
    sqpoll = global_section["sqpoll"] | false;
    max_direct_fds_per_exr = global_section["max_direct_fds_per_exr"] | 4096;
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
    stream << "io_uring_wq_max_unbound_workers:" << io_uring_wq_max_unbound_workers << ", ";
    stream << "client_executors:" << client_executors << ", ";
    stream << "sqpoll:" << sqpoll;

    stream << "].";
    return stream.str();
}

} // namespace rdss
