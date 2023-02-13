#pragma once

#include <cassert>
#include <string>
#include <vector>

namespace rdss {
// TODO: Needs overhaul
// - Add types and make this a vector of pointer to type.
// - Add reference to key / value
// - Add fast error
struct Result {
    enum class Type { kString, kInteger };

    Type type = Type::kString;
    std::vector<std::string> data;
    std::vector<int32_t> ints;
    std::vector<bool> is_null;

    void Add(std::string s) {
        data.push_back(std::move(s));
        is_null.push_back(false);
    }

    void Add(int32_t i) {
        if (type == Type::kString) {
            assert(data.empty());
            type = Type::kInteger;
        }
        ints.push_back(i);
        is_null.push_back(false);
    }

    void AddNull() {
        data.push_back({});
        is_null.push_back(true);
    }

    size_t Size() const { return (type == Type::kString) ? data.size() : ints.size(); }

    void Reset() {
        data.clear();
        ints.clear();
        is_null.clear();
    }
};

} // namespace rdss
