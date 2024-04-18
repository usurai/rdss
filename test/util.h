#pragma once

#include <cstdlib>
#include <string>

namespace rdss::test {

static std::string GenRandomString(size_t len) {
    static const char alphanum[] = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += alphanum[static_cast<size_t>(std::rand()) % (sizeof(alphanum) - 1)];
    }
    return result;
}

} // namespace rdss::test
