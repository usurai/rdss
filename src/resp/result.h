#pragma once

#include "data_structure/tracking_hash_table.h"
#include "error.h"

#include <vector>

namespace rdss {

/// For OK, Error, Nil, returns string_view over static string.
/// For Int, convert int to string at internal buffer, returns string_view over it.
/// For String, Strings, construct the number part at internal buffer, return span<iovecs>.
struct Result {
    enum class Type { kOk, kError, kNil, kInt, kString, kStrings };

    void SetOk() { type = Type::kOk; }

    void SetError(Error err);

    void SetNil() { type = Type::kNil; }

    void SetString(MTSPtr str);
    
    void AddString(MTSPtr str);

    void SetInt(int64_t val);

    void Reset();

    Type type = Type::kOk;
    Error error;
    int64_t int_value = 0;
    MTSPtr string_ptr = nullptr;
    std::vector<MTSPtr> strings;
};

} // namespace rdss
