#pragma once

#include "hash_table.h"
#include "memory.h"

#include <cassert>
#include <memory>
#include <string>

namespace rdss {

using TrackingString = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;
using TrackingStringPtr = std::shared_ptr<TrackingString>;

struct TSPComparator {
    bool operator()(const TrackingStringPtr& lhs, const TrackingStringPtr& rhs) const {
        assert(lhs != nullptr && rhs != nullptr);
        return lhs->compare(*rhs) == 0;
    }

    bool operator()(const TrackingStringPtr& lhs, const std::string_view& rhs) const {
        if (lhs == nullptr) {
            return rhs.empty();
        }
        return lhs->compare(rhs) == 0;
    }
};

struct TSPHashAdapter {
    const TrackingString& operator()(const TrackingStringPtr& ptr) { return *ptr; }
};

// TODO: Using TrackingStringPtr makes the mem usage of shared_ptr it self out of tracking of
// MemoryTracker.
using TrackingMap = HashTable<TrackingString>;

} // namespace rdss
