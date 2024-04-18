#pragma once

#include <sys/uio.h>

#include <span>
#include <string_view>
#include <vector>

namespace rdss {

class Buffer;
struct Result;

bool NeedsGather(Result& result);

std::string_view ResultToStringView(Result& result, Buffer& buffer);

void ResultToIovecs(Result& result, Buffer& buffer, std::vector<iovec>& iovecs);

} // namespace rdss
