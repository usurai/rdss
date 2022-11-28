#pragma once

#include "tortellini.hh"

#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>

namespace rdss {

struct Config {
    uint64_t maxmemory = 0;

    void ReadFromFile(const std::string& file_name) {
        std::ifstream in(file_name);

        tortellini::ini ini;
        in >> ini;

        auto global_section = ini[""];
        maxmemory = global_section["maxmemory"] | maxmemory;
    }

    std::string ToString() const {
        std::stringstream stream;
        stream << "maxmemory:" << maxmemory;
        return stream.str();
    }
};

} // namespace rdss
