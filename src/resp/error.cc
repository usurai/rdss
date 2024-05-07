// Copyright (c) usurai.
// Licensed under the MIT license.
#include "error.h"

#include <array>
#include <string>

namespace rdss {

static const std::array<const std::string, 20> kErrorStr = {
  "-ERR Protocol error\r\n",
  "-ERR unknown command\r\n",
  "-OOM command not allowed when used memory > 'maxmemory'.\r\n",
  "-ERR wrong number of arguments.\r\n",
  "-ERR syntax error\r\n",
  "-ERR value is not an integer or out of range\r\n",
};

std::string_view ErrorToStringView(Error error) { return kErrorStr[static_cast<size_t>(error)]; }

} // namespace rdss
