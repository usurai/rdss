#pragma once

#include "tortellini.hh"

#include <glog/logging.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace rdss {

enum class MaxmemoryPolicy { kNoEviction, kAllKeysRandom };

MaxmemoryPolicy MaxmemoryPolicyStrToEnum(const std::string& str) {
    if (str == "noeviction") {
        return MaxmemoryPolicy::kNoEviction;
    }
    if (!str.compare("allkeys-random")) {
        return MaxmemoryPolicy::kAllKeysRandom;
    }
    LOG(FATAL) << "Unknown maxmemory-policy: " << str;
}

std::string MaxmemoryPolicyEnumToStr(MaxmemoryPolicy policy) {
    switch (policy) {
    case MaxmemoryPolicy::kNoEviction:
        return "noeviction";
    case MaxmemoryPolicy::kAllKeysRandom:
        return "allkeys-random";
    default:
        return "Unknown policy";
    }
}

struct Config {
    uint32_t port = 6379;
    uint64_t maxmemory = 0;
    MaxmemoryPolicy maxmemory_policy = MaxmemoryPolicy::kNoEviction;

    void ReadFromFile(const std::string& file_name) {
        std::ifstream in(file_name);

        tortellini::ini ini;
        in >> ini;

        auto global_section = ini[""];

        port = global_section["port"] | 6379;

        maxmemory = global_section["maxmemory"] | maxmemory;

        auto maxmemory_policy_str = global_section["maxmemory-policy"] | "noeviction";
        maxmemory_policy = MaxmemoryPolicyStrToEnum(maxmemory_policy_str);
    }

    std::string ToString() const {
        std::stringstream stream;
        stream << "Configs: [";
        stream << "port:" << port << ", ";
        stream << "maxmemory:" << maxmemory << ", ";
        stream << "maxmemory-policy:" << MaxmemoryPolicyEnumToStr(maxmemory_policy);
        stream << "].";
        return stream.str();
    }
};

} // namespace rdss
