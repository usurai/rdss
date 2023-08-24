#pragma once

#include <system_error>

namespace rdss {

std::error_code ErrnoToErrorCode(int err);

}
