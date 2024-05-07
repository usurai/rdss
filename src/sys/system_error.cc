// Copyright (c) usurai.
// Licensed under the MIT license.
#include "system_error.h"

namespace rdss {

std::error_code ErrnoToErrorCode(int err) {
    return std::make_error_code(static_cast<std::errc>(err));
}

} // namespace rdss
