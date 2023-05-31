#pragma once

#include "hash_table.h"
#include "memory.h"

#include <cassert>
#include <memory>
#include <string>

namespace rdss {

// MTS for Memory Tracked String.
using MTS = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;
using MTSPtr = std::shared_ptr<MTS>;
using MTSHashTable = HashTable<MTSPtr>;

MTSPtr CreateMTSPtr(std::string_view sv);

} // namespace rdss
