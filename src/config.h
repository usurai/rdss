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

    void ReadFromFile(const std::string& file_name);

    std::string ToString() const;
};

} // namespace rdss
