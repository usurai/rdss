#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace rdss {

class Command;

struct string_hash {
    using hash_type = std::hash<std::string_view>;
    using is_transparent = void;

    std::size_t operator()(const char* str) const { return hash_type{}(str); }
    std::size_t operator()(std::string_view str) const { return hash_type{}(str); }
    std::size_t operator()(std::string const& str) const { return hash_type{}(str); }
};

using CommandName = std::string;
using CommandDictionary = std::unordered_map<CommandName, Command, string_hash, std::equal_to<>>;

}; // namespace rdss
