// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <cstdint>
#include <vector>

namespace rdss {

bool SetNofileLimit(uint32_t limit);

int CreateListeningSocket(uint16_t port);

void SetThreadAffinity(std::vector<size_t> cpus);

} // namespace rdss
