// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include <system_error>

namespace rdss {

std::error_code ErrnoToErrorCode(int err);

} // namespace rdss
