#include "config.h"

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
}

std::string Config::ToString() const {
    std::stringstream stream;
    stream << "Configs: [";
    stream << "port:" << port << ", ";
    stream << "maxclients:" << maxclients << ", ";
    stream << "maxmemory:" << maxmemory << ", ";
    stream << "maxmemory-policy:" << MaxmemoryPolicyEnumToStr(maxmemory_policy) << ", ";
    stream << "maxmemory-samples:" << maxmemory_samples;
    stream << "].";
    return stream.str();
}

} // namespace rdss
