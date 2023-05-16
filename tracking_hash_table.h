#pragma once

#include "hash_table.h"
#include "memory.h"

#include <cassert>
#include <memory>
#include <string>

namespace rdss {

using TrackingString = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;
using TrackingStringPtr = std::shared_ptr<TrackingString>;
using TrackingMap = HashTable<TrackingString>;

} // namespace rdss
