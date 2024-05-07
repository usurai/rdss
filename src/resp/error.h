// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <stdint.h>
#include <string_view>

namespace rdss {

enum class Error : uint8_t {
    kProtocol,
    kUnknownCommand,
    kOOM,
    kWrongArgNum,
    kSyntaxError,
    kNotAnInt,
};

std::string_view ErrorToStringView(Error error);

} // namespace rdss
