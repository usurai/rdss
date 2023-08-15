#pragma once

#include "base/buffer.h"
#include "data_structure/tracking_hash_table.h"
#include "error.h"

#include <sys/uio.h>

#include <cassert>
#include <charconv>
#include <span>
#include <string>
#include <vector>

namespace rdss {

static const std::string kOkStr = "+OK\r\n";
static const std::string kNilStr = "$-1\r\n"; // TODO: This is for RESP2, complement for RESP3.

/// For OK, Error, Nil, returns string_view over static string.
/// For Int, convert int to string at internal buffer, returns string_view over it.
/// For String, Strings, construct the number part at internal buffer, return span<iovecs>.
struct Result {
    enum class Type { kOk, kError, kNil, kInt, kString, kStrings };

    void SetOk() { type = Type::kOk; }

    void SetError(Error err) {
        type = Type::kError;
        error = err;
    }

    void SetNil() { type = Type::kNil; }

    void SetString(MTSPtr str) {
        type = Type::kString;
        string_ptr = std::move(str);
    }

    void SetInt(int64_t val) {
        type = Type::kInt;
        int_value = val;
    }

    bool NeedsScatter() const { return type == Type::kString || type == Type::kStrings; }

    std::string_view AsStringView() {
        switch (type) {
        case Type::kOk:
            return kOkStr;
        case Type::kNil:
            return kNilStr;
        case Type::kError:
            return ErrorToStringView(error);
        case Type::kInt: {
            buffer.EnsureAvailable(64, false);
            auto sink = buffer.Sink();
            sink[0] = ':';
            auto res = std::to_chars(sink.data() + 1, sink.data() + sink.size(), int_value);
            assert(res.ec == std::errc{});
            assert(res.ptr - sink.data() + 2 <= sink.size());
            size_t cursor = res.ptr - sink.data();
            *(sink.data() + cursor) = '\r';
            *(sink.data() + cursor + 1) = '\n';
            cursor += 2;
            buffer.Produce(cursor);
            return buffer.Source();
        }
        default:
            LOG(FATAL) << "Unsupported type";
        }
    }

    std::span<iovec> AsIovecs() {
        if (type == Type::kString) {
            buffer.EnsureAvailable(64, false);
            auto sink = buffer.Sink();
            sink[0] = '$';
            auto res = std::to_chars(
              sink.data() + 1, sink.data() + sink.size(), string_ptr->size());
            assert(res.ec == std::errc{});
            assert(res.ptr - sink.data() + 2 <= sink.size());
            size_t cursor = res.ptr - sink.data();
            *(sink.data() + cursor) = '\r';
            *(sink.data() + cursor + 1) = '\n';
            const auto crlf_offset = cursor;
            cursor += 2;

            iovecs.reserve(3);
            iovecs.emplace_back(iovec{.iov_base = sink.data(), .iov_len = cursor});
            iovecs.emplace_back(
              iovec{.iov_base = string_ptr->data(), .iov_len = string_ptr->size()});
            iovecs.emplace_back(iovec{.iov_base = sink.data() + crlf_offset, .iov_len = 2});

        } else {
            LOG(FATAL) << "Unimplemented";
        }

        return iovecs;
    }

    void Reset() {
        type = Type::kOk;
        buffer.Reset();
        string_ptr.reset();
        strings.clear();
        iovecs.clear();
    }

    Type type = Type::kOk;
    Buffer buffer;

    Error error;
    int64_t int_value = 0;

    MTSPtr string_ptr = nullptr;
    std::vector<MTSPtr> strings;
    std::vector<iovec> iovecs;
};

} // namespace rdss
