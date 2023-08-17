#include "replier.h"

#include "base/buffer.h"
#include "resp/result.h"

#include <charconv>

namespace rdss {

using Type = Result::Type;

static const std::string kOkStr = "+OK\r\n";
static const std::string kNilStr = "$-1\r\n"; // TODO: This is for RESP2, complement for RESP3.

namespace detail {

size_t IntToChars(int32_t val, Buffer::SinkType sink) {
    assert(sink.size() >= 13); // 13 is the most digits of int32_t + 2 (for CRLF).

    auto res = std::to_chars(sink.data(), sink.data() + sink.size(), val);
    assert(res.ec == std::errc{});
    assert(res.ptr - sink.data() + 2 <= sink.size());
    *(res.ptr) = '\r';
    *(res.ptr + 1) = '\n';
    return res.ptr + 2 - sink.data();
}

size_t StrToIovecs(MTSPtr& str, Buffer::SinkType sink, std::vector<iovec>& iovecs) {
    if (str == nullptr) {
        iovecs.emplace_back(
          iovec{.iov_base = const_cast<char*>(kNilStr.data()), .iov_len = kNilStr.size()});
        return 0;
    }
    sink[0] = '$';
    auto offset = IntToChars(str->size(), sink.subspan(1));
    ++offset;
    iovecs.emplace_back(iovec{.iov_base = sink.data(), .iov_len = offset});
    iovecs.emplace_back(iovec{.iov_base = str->data(), .iov_len = str->size()});
    iovecs.emplace_back(iovec{.iov_base = sink.data() + offset - 2, .iov_len = 2});
    return offset;
}

} // namespace detail

bool NeedsScatter(Result& result) {
    return result.type == Result::Type::kString || result.type == Result::Type::kStrings;
}

std::string_view ResultToStringView(Result& result, Buffer& buffer) {
    switch (result.type) {
    case Type::kOk:
        return kOkStr;
    case Type::kNil:
        return kNilStr;
    case Type::kError:
        return ErrorToStringView(result.error);
    case Type::kInt: {
        buffer.EnsureAvailable(32, false);
        auto sink = buffer.Sink();
        sink[0] = ':';
        const auto offset = detail::IntToChars(
          result.int_value, Buffer::SinkType(sink.data() + 1, sink.size() - 1));
        buffer.Produce(offset + 1);
        return buffer.Source();
    }
    default:
        LOG(FATAL) << "Unsupported type";
    }
}

void ResultToIovecs(Result& result, Buffer& buffer, std::vector<iovec>& iovecs) {
    if (result.type == Type::kString) {
        buffer.EnsureAvailable(64, false);
        detail::StrToIovecs(result.string_ptr, buffer.Sink(), iovecs);
        return;
    }

    assert(result.type = Type::kStrings);

    buffer.EnsureAvailable(result.strings.size() * 32, false);
    iovecs.reserve(1 + result.strings.size() * 3);

    auto sink = buffer.Sink();

    sink[0] = '*';
    auto cursor = detail::IntToChars(
      result.strings.size(), Buffer::SinkType(sink.data() + 1, sink.size() - 1));
    ++cursor;
    iovecs.emplace_back(iovec{.iov_base = sink.data(), .iov_len = cursor});

    for (auto& str : result.strings) {
        cursor += detail::StrToIovecs(str, sink.subspan(cursor), iovecs);
    }
}

} // namespace rdss
