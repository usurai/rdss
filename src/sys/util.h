// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <cstdint>

namespace rdss {

bool SetNofileLimit(uint32_t limit);

int CreateListeningSocket(uint16_t port);

} // namespace rdss
